// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    container::ComponentIdentity,
    logs::{debuglog, error::StreamError, message::MessageWithStats, stats::LogStreamStats},
};
use diagnostics_data::Severity;
use diagnostics_message::message::{LoggerMessage, METADATA_SIZE};
use fuchsia_zircon as zx;
use std::{convert::TryInto, sync::Arc};
use tracing::warn;

#[derive(Debug)]
pub struct StoredMessage {
    // TODO(miguelfrde): move timestamp and severity to MessageBytes when needed, fetch when
    // already present in the core structs.
    bytes: MessageBytes,
    stats: Option<Arc<LogStreamStats>>,
}

#[derive(Debug)]
enum MessageBytes {
    Legacy(LoggerMessage),
    Structured { bytes: Box<[u8]>, severity: Severity, timestamp: i64 },
    DebugLog { msg: zx::sys::zx_log_record_t, severity: Severity, size: usize },
}

impl StoredMessage {
    pub fn legacy(buf: &[u8], stats: Arc<LogStreamStats>) -> Result<Self, StreamError> {
        let msg: LoggerMessage = buf.try_into()?;
        Ok(StoredMessage { bytes: MessageBytes::Legacy(msg), stats: Some(stats) })
    }

    pub fn structured(buf: &[u8], stats: Arc<LogStreamStats>) -> Result<Self, StreamError> {
        let (timestamp, severity) =
            diagnostics_message::message::parse_basic_structured_info(&buf)?;
        // TODO(fxbug.dev/66656): remove copy. `buf.into()` calls into
        // https://doc.rust-lang.org/std/boxed/struct.Box.html#method.from-1 which allocates on the
        // heap and copies.
        Ok(StoredMessage {
            bytes: MessageBytes::Structured { bytes: buf.into(), severity, timestamp },
            stats: Some(stats),
        })
    }

    pub fn debuglog(record: zx::sys::zx_log_record_t) -> Option<Self> {
        let data_len = record.datalen as usize;

        let mut contents = match std::str::from_utf8(&record.data[0..data_len]) {
            Err(e) => {
                warn!(?e, "Received non-UTF8 from the debuglog.");
                return None;
            }
            Ok(utf8) => {
                let boxed_utf8: Box<str> = utf8.into();
                boxed_utf8.into_string()
            }
        };
        if let Some(b'\n') = contents.bytes().last() {
            contents.pop();
        }

        // TODO(fxbug.dev/32998): Once we support structured logs we won't need this
        // hack to match a string in klogs.
        const MAX_STRING_SEARCH_SIZE: usize = 100;
        let last = contents
            .char_indices()
            .nth(MAX_STRING_SEARCH_SIZE)
            .map(|(i, _)| i)
            .unwrap_or(contents.len());

        // Don't look beyond the 100th character in the substring to limit the cost
        // of the substring search operation.
        let early_contents = &contents[..last];

        let severity = if early_contents.contains("ERROR:") {
            Severity::Error
        } else if early_contents.contains("WARNING:") {
            Severity::Warn
        } else {
            Severity::Info
        };

        let size = METADATA_SIZE + 5 /*'klog' tag*/ + contents.len() + 1;
        Some(StoredMessage {
            bytes: MessageBytes::DebugLog { msg: record, severity, size },
            stats: None,
        })
    }

    pub fn size(&self) -> usize {
        match &self.bytes {
            MessageBytes::Legacy(msg) => msg.size_bytes,
            MessageBytes::Structured { bytes, .. } => bytes.len(),
            MessageBytes::DebugLog { size, .. } => *size,
        }
    }

    pub fn has_stats(&self) -> bool {
        self.stats.is_some()
    }

    pub fn with_stats(&mut self, stats: Arc<LogStreamStats>) {
        self.stats = Some(stats)
    }

    pub fn severity(&self) -> Severity {
        match &self.bytes {
            MessageBytes::Legacy(msg) => msg.severity,
            MessageBytes::Structured { severity, .. } => *severity,
            MessageBytes::DebugLog { severity, .. } => *severity,
        }
    }

    pub fn timestamp(&self) -> i64 {
        match &self.bytes {
            MessageBytes::Legacy(msg) => msg.timestamp,
            MessageBytes::Structured { timestamp, .. } => *timestamp,
            MessageBytes::DebugLog { msg, .. } => msg.timestamp,
        }
    }
}

impl Drop for StoredMessage {
    fn drop(&mut self) {
        if let Some(stats) = &self.stats {
            stats.increment_dropped(&*self);
        }
    }
}

impl StoredMessage {
    pub fn parse(&self, source: &ComponentIdentity) -> Result<MessageWithStats, StreamError> {
        match &self.bytes {
            MessageBytes::Legacy(msg) => Ok(MessageWithStats::from_logger(source, msg.clone())),
            MessageBytes::Structured { bytes, .. } => {
                MessageWithStats::from_structured(source, &bytes).map_err(|er| er.into())
            }
            MessageBytes::DebugLog { msg, .. } => {
                debuglog::convert_debuglog_to_log_message(&msg).ok_or(StreamError::DebugLogMessage)
            }
        }
    }
}
