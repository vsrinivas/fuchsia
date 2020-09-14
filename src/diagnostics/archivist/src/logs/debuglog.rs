// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Read debug logs, convert them to LogMessages and serve them.

use super::message::{LogsField, LogsHierarchy, LogsProperty, Message, Severity, METADATA_SIZE};
use anyhow::Error;
use async_trait::async_trait;
use byteorder::{ByteOrder, LittleEndian};
use fidl_fuchsia_boot::ReadOnlyLogMarker;
use fuchsia_async as fasync;
use fuchsia_component::client::connect_to_service;
use fuchsia_zircon as zx;
use futures::stream::{unfold, Stream, TryStreamExt};
use log::error;

const KERNEL_URL: &str = "fuchsia-boot://kernel";

#[async_trait]
pub trait DebugLog {
    /// Reads a single entry off the debug log into `buffer`.  Any existing
    /// contents in `buffer` are overwritten.
    async fn read(&self, buffer: &'_ mut Vec<u8>) -> Result<(), zx::Status>;

    /// Returns a future that completes when there is another log to read.
    async fn ready_signal(&self) -> Result<(), zx::Status>;
}

pub struct KernelDebugLog {
    debuglogger: zx::DebugLog,
}

#[async_trait]
impl DebugLog for KernelDebugLog {
    async fn read(&self, buffer: &'_ mut Vec<u8>) -> Result<(), zx::Status> {
        self.debuglogger.read(buffer)
    }

    async fn ready_signal(&self) -> Result<(), zx::Status> {
        fasync::OnSignals::new(&self.debuglogger, zx::Signals::LOG_READABLE).await.map(|_| ())
    }
}

impl KernelDebugLog {
    /// Connects to `fuchsia.boot.ReadOnlyLog` to retrieve a handle.
    pub async fn new() -> Result<Self, Error> {
        let boot_log = connect_to_service::<ReadOnlyLogMarker>()?;
        let debuglogger = boot_log.get().await?;
        Ok(KernelDebugLog { debuglogger })
    }
}

pub struct DebugLogBridge<K: DebugLog> {
    debug_log: K,
    buf: Vec<u8>,
}

impl<K: DebugLog> DebugLogBridge<K> {
    pub fn create(debug_log: K) -> Self {
        DebugLogBridge { debug_log, buf: Vec::with_capacity(zx::sys::ZX_LOG_RECORD_MAX) }
    }

    async fn read_log(&mut self) -> Result<Message, zx::Status> {
        loop {
            self.debug_log.read(&mut self.buf).await?;
            if let Some(message) = convert_debuglog_to_log_message(self.buf.as_slice()) {
                return Ok(message);
            }
        }
    }

    pub async fn existing_logs<'a>(&'a mut self) -> Result<Vec<Message>, zx::Status> {
        unfold(self, move |klogger| async move {
            match klogger.read_log().await {
                Err(zx::Status::SHOULD_WAIT) => None,
                x => Some((x, klogger)),
            }
        })
        .try_collect::<Vec<_>>()
        .await
    }

    pub fn listen(self) -> impl Stream<Item = Result<Message, zx::Status>> {
        unfold((true, self), move |(mut is_readable, mut klogger)| async move {
            loop {
                if !is_readable {
                    if let Err(e) = klogger.debug_log.ready_signal().await {
                        break Some((Err(e), (is_readable, klogger)));
                    }
                }
                is_readable = true;
                match klogger.read_log().await {
                    Err(zx::Status::SHOULD_WAIT) => {
                        is_readable = false;
                        continue;
                    }
                    x => break Some((x, (is_readable, klogger))),
                }
            }
        })
    }
}

/// Parses a raw debug log read from the kernel.  Returns the parsed message and
/// its size in memory on success, and None if parsing fails.
pub fn convert_debuglog_to_log_message(buf: &[u8]) -> Option<Message> {
    if buf.len() < 32 {
        return None;
    }
    let data_len = LittleEndian::read_u16(&buf[4..6]) as usize;
    if buf.len() != 32 + data_len {
        return None;
    }

    let time = zx::Time::from_nanos(LittleEndian::read_i64(&buf[8..16]));
    let pid = LittleEndian::read_u64(&buf[16..24]);
    let tid = LittleEndian::read_u64(&buf[24..32]);

    let mut contents = match String::from_utf8(buf[32..(32 + data_len)].to_vec()) {
        Err(e) => {
            error!("logger: invalid log record: {:?}", e);
            return None;
        }
        Ok(s) => s,
    };
    if let Some(b'\n') = contents.bytes().last() {
        contents.pop();
    }

    // TODO(fxb/32998): Once we support structured logs we won't need this
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
    Some(Message::new(
        time,
        severity,
        size,
        0, // TODO(fxbug.dev/48548) dropped_logs
        "klog",
        KERNEL_URL,
        LogsHierarchy::new(
            "root",
            vec![
                LogsProperty::Uint(LogsField::ProcessId, pid),
                LogsProperty::Uint(LogsField::ThreadId, tid),
                LogsProperty::String(LogsField::Tag, "klog".to_string()),
                LogsProperty::String(LogsField::Msg, contents),
            ],
            vec![],
        ),
    ))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::logs::testing::*;

    use futures::stream::TryStreamExt;

    #[test]
    fn convert_debuglog_to_log_message_test() {
        let klog = TestDebugEntry::new("test log".as_bytes());
        let log_message = convert_debuglog_to_log_message(&klog.to_vec()).unwrap();
        assert_eq!(
            log_message,
            Message::new(
                klog.timestamp,
                Severity::Info,
                METADATA_SIZE + 6 + "test log".len(),
                0, // dropped logs
                "klog",
                KERNEL_URL,
                LogsHierarchy::new(
                    "root",
                    vec![
                        LogsProperty::Uint(LogsField::ProcessId, klog.pid),
                        LogsProperty::Uint(LogsField::ThreadId, klog.tid),
                        LogsProperty::String(LogsField::Tag, "klog".to_string()),
                        LogsProperty::String(LogsField::Msg, "test log".to_string())
                    ],
                    vec![]
                ),
            )
        );

        // maximum allowed klog size
        let klog = TestDebugEntry::new(&vec!['a' as u8; zx::sys::ZX_LOG_RECORD_MAX - 32]);
        let log_message = convert_debuglog_to_log_message(&klog.to_vec()).unwrap();
        assert_eq!(
            log_message,
            Message::new(
                klog.timestamp,
                Severity::Info,
                METADATA_SIZE + 6 + zx::sys::ZX_LOG_RECORD_MAX - 32,
                0, // dropped logs
                "klog",
                KERNEL_URL,
                LogsHierarchy::new(
                    "root",
                    vec![
                        LogsProperty::Uint(LogsField::ProcessId, klog.pid),
                        LogsProperty::Uint(LogsField::ThreadId, klog.tid),
                        LogsProperty::String(LogsField::Tag, "klog".to_string()),
                        LogsProperty::String(
                            LogsField::Msg,
                            String::from_utf8(vec!['a' as u8; zx::sys::ZX_LOG_RECORD_MAX - 32])
                                .unwrap()
                        )
                    ],
                    vec![]
                ),
            ),
        );

        // empty message
        let klog = TestDebugEntry::new(&vec![]);
        let log_message = convert_debuglog_to_log_message(&klog.to_vec()).unwrap();
        assert_eq!(
            log_message,
            Message::new(
                klog.timestamp,
                Severity::Info,
                METADATA_SIZE + 6,
                0, // dropped logs
                "klog",
                KERNEL_URL,
                LogsHierarchy::new(
                    "root",
                    vec![
                        LogsProperty::Uint(LogsField::ProcessId, klog.pid),
                        LogsProperty::Uint(LogsField::ThreadId, klog.tid),
                        LogsProperty::String(LogsField::Tag, "klog".to_string()),
                        LogsProperty::String(LogsField::Msg, "".to_string())
                    ],
                    vec![]
                ),
            ),
        );

        // truncated header
        let klog = vec![3u8; 4];
        assert!(convert_debuglog_to_log_message(&klog).is_none());

        // invalid utf-8
        let klog = TestDebugEntry::new(&vec![0, 159, 146, 150]);
        assert!(convert_debuglog_to_log_message(&klog.to_vec()).is_none());

        // malformed
        let klog = vec![0xffu8; 64];
        assert!(convert_debuglog_to_log_message(&klog).is_none());
    }

    #[fasync::run_until_stalled(test)]
    async fn logger_existing_logs_test() {
        let debug_log = TestDebugLog::new();
        let klog = TestDebugEntry::new("test log".as_bytes());
        debug_log.enqueue_read_entry(&klog);
        debug_log.enqueue_read_fail(zx::Status::SHOULD_WAIT);
        let mut log_bridge = DebugLogBridge::create(debug_log);

        assert_eq!(
            log_bridge.existing_logs().await.unwrap(),
            vec![Message::new(
                klog.timestamp,
                Severity::Info,
                METADATA_SIZE + 6 + "test log".len(),
                0, // dropped logs
                "klog",
                KERNEL_URL,
                LogsHierarchy::new(
                    "root",
                    vec![
                        LogsProperty::Uint(LogsField::ProcessId, klog.pid),
                        LogsProperty::Uint(LogsField::ThreadId, klog.tid),
                        LogsProperty::String(LogsField::Tag, "klog".to_string()),
                        LogsProperty::String(LogsField::Msg, "test log".to_string())
                    ],
                    vec![]
                ),
            )]
        );

        // unprocessable logs should be skipped.
        let debug_log = TestDebugLog::new();
        debug_log.enqueue_read(vec![]);
        debug_log.enqueue_read_fail(zx::Status::SHOULD_WAIT);
        let mut log_bridge = DebugLogBridge::create(debug_log);
        assert!(log_bridge.existing_logs().await.unwrap().is_empty());
    }

    #[fasync::run_until_stalled(test)]
    async fn logger_keep_listening_after_exhausting_initial_contents_test() {
        let debug_log = TestDebugLog::new();
        debug_log.enqueue_read_entry(&TestDebugEntry::new("test log".as_bytes()));
        debug_log.enqueue_read_fail(zx::Status::SHOULD_WAIT);
        debug_log.enqueue_read_entry(&TestDebugEntry::new("second test log".as_bytes()));
        let log_bridge = DebugLogBridge::create(debug_log);
        let mut log_stream = Box::pin(log_bridge.listen());
        let log_message = log_stream.try_next().await.unwrap().unwrap();
        assert_eq!(log_message.msg().unwrap(), "test log");
        let log_message = log_stream.try_next().await.unwrap().unwrap();
        assert_eq!(log_message.msg().unwrap(), "second test log");

        // unprocessable logs should be skipped.
        let debug_log = TestDebugLog::new();
        debug_log.enqueue_read(vec![]);
        debug_log.enqueue_read_entry(&TestDebugEntry::new("test log".as_bytes()));
        let log_bridge = DebugLogBridge::create(debug_log);
        let mut log_stream = Box::pin(log_bridge.listen());
        let log_message = log_stream.try_next().await.unwrap().unwrap();
        assert_eq!(log_message.msg().unwrap(), "test log");
    }

    #[fasync::run_until_stalled(test)]
    async fn severity_parsed_from_log() {
        let debug_log = TestDebugLog::new();
        debug_log.enqueue_read_entry(&TestDebugEntry::new("ERROR: first log".as_bytes()));
        // We look for the string 'ERROR:' to label this as a Severity::Error.
        debug_log.enqueue_read_entry(&TestDebugEntry::new("first log error".as_bytes()));
        debug_log.enqueue_read_entry(&TestDebugEntry::new("WARNING: second log".as_bytes()));
        debug_log.enqueue_read_entry(&TestDebugEntry::new("INFO: third log".as_bytes()));
        debug_log.enqueue_read_entry(&TestDebugEntry::new("fourth log".as_bytes()));
        // Create a string padded with UTF-8 codepoints at the beginning so it's not labeled
        // as an error log.
        let long_padding = (0..100).map(|_| "\u{10FF}").collect::<String>();
        let long_log = format!("{}ERROR: fifth log", long_padding);
        debug_log.enqueue_read_entry(&TestDebugEntry::new(long_log.as_bytes()));

        let log_bridge = DebugLogBridge::create(debug_log);
        let mut log_stream = Box::pin(log_bridge.listen());

        let log_message = log_stream.try_next().await.unwrap().unwrap();
        assert_eq!(log_message.msg().unwrap(), "ERROR: first log");
        assert_eq!(log_message.metadata.severity, Severity::Error);

        let log_message = log_stream.try_next().await.unwrap().unwrap();
        assert_eq!(log_message.msg().unwrap(), "first log error");
        assert_eq!(log_message.metadata.severity, Severity::Info);

        let log_message = log_stream.try_next().await.unwrap().unwrap();
        assert_eq!(log_message.msg().unwrap(), "WARNING: second log");
        assert_eq!(log_message.metadata.severity, Severity::Warn);

        let log_message = log_stream.try_next().await.unwrap().unwrap();
        assert_eq!(log_message.msg().unwrap(), "INFO: third log");
        assert_eq!(log_message.metadata.severity, Severity::Info);

        let log_message = log_stream.try_next().await.unwrap().unwrap();
        assert_eq!(log_message.msg().unwrap(), "fourth log");
        assert_eq!(log_message.metadata.severity, Severity::Info);

        let log_message = log_stream.try_next().await.unwrap().unwrap();
        assert_eq!(log_message.msg().unwrap(), &long_log);
        assert_eq!(log_message.metadata.severity, Severity::Info);
    }
}
