// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Read debug logs, convert them to LogMessages and serve them.

use crate::{
    events::types::ComponentIdentifier,
    identity::ComponentIdentity,
    logs::{error::LogsError, stored_message::StoredMessage},
};
use async_trait::async_trait;
use diagnostics_data::{BuilderArgs, LogsData, LogsDataBuilder, Severity};
use fidl::prelude::*;
use fidl_fuchsia_boot::ReadOnlyLogMarker;
use fuchsia_async as fasync;
use fuchsia_component::client::connect_to_protocol;
use fuchsia_zircon as zx;
use futures::stream::{unfold, Stream, TryStreamExt};
use lazy_static::lazy_static;

pub const KERNEL_URL: &str = "fuchsia-boot://kernel";
lazy_static! {
    pub static ref KERNEL_IDENTITY: ComponentIdentity = {
        ComponentIdentity::from_identifier_and_url(
            ComponentIdentifier::parse_from_moniker("./klog").unwrap(),
            KERNEL_URL,
        )
    };
}

#[async_trait]
pub trait DebugLog {
    /// Reads a single entry off the debug log into `buffer`.  Any existing
    /// contents in `buffer` are overwritten.
    async fn read(&self) -> Result<zx::sys::zx_log_record_t, zx::Status>;

    /// Returns a future that completes when there is another log to read.
    async fn ready_signal(&self) -> Result<(), zx::Status>;
}

pub struct KernelDebugLog {
    debuglogger: zx::DebugLog,
}

#[async_trait]
impl DebugLog for KernelDebugLog {
    async fn read(&self) -> Result<zx::sys::zx_log_record_t, zx::Status> {
        self.debuglogger.read()
    }

    async fn ready_signal(&self) -> Result<(), zx::Status> {
        fasync::OnSignals::new(&self.debuglogger, zx::Signals::LOG_READABLE).await.map(|_| ())
    }
}

impl KernelDebugLog {
    /// Connects to `fuchsia.boot.ReadOnlyLog` to retrieve a handle.
    pub async fn new() -> Result<Self, LogsError> {
        let boot_log = connect_to_protocol::<ReadOnlyLogMarker>().map_err(|source| {
            LogsError::ConnectingToService { protocol: ReadOnlyLogMarker::PROTOCOL_NAME, source }
        })?;
        let debuglogger =
            boot_log.get().await.map_err(|source| LogsError::RetrievingDebugLog { source })?;
        Ok(KernelDebugLog { debuglogger })
    }
}

pub struct DebugLogBridge<K: DebugLog> {
    debug_log: K,
}

impl<K: DebugLog> DebugLogBridge<K> {
    pub fn create(debug_log: K) -> Self {
        DebugLogBridge { debug_log }
    }

    async fn read_log(&mut self) -> Result<StoredMessage, zx::Status> {
        loop {
            let record = self.debug_log.read().await?;
            if let Some(bytes) = StoredMessage::debuglog(record) {
                return Ok(bytes);
            }
        }
    }

    pub async fn existing_logs(&mut self) -> Result<Vec<StoredMessage>, zx::Status> {
        unfold(self, move |klogger| async move {
            match klogger.read_log().await {
                Err(zx::Status::SHOULD_WAIT) => None,
                x => Some((x, klogger)),
            }
        })
        .try_collect::<Vec<_>>()
        .await
    }

    pub fn listen(self) -> impl Stream<Item = Result<StoredMessage, zx::Status>> {
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
pub fn convert_debuglog_to_log_message(record: &zx::sys::zx_log_record_t) -> Option<LogsData> {
    let data_len = record.datalen as usize;

    let mut contents = match std::str::from_utf8(&record.data[0..data_len]) {
        Err(_) => {
            format!(
                "INVALID UTF-8 SEE https://fxbug.dev/88259, message may be corrupted: {}",
                String::from_utf8_lossy(&record.data[0..data_len])
            )
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

    Some(
        LogsDataBuilder::new(BuilderArgs {
            timestamp_nanos: record.timestamp.into(),
            component_url: Some(KERNEL_IDENTITY.url.to_string()),
            moniker: KERNEL_IDENTITY.to_string(),
            severity,
        })
        .set_pid(record.pid)
        .set_tid(record.tid)
        .add_tag("klog".to_string())
        .set_dropped(0)
        .set_message(contents)
        .build(),
    )
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::logs::testing::*;

    use fidl_fuchsia_diagnostics as fdiagnostics;
    use fidl_fuchsia_logger::LogMessage;
    use futures::stream::{StreamExt, TryStreamExt};

    #[fuchsia::test]
    fn convert_debuglog_to_log_message_test() {
        let klog = TestDebugEntry::new("test log".as_bytes());
        let data = convert_debuglog_to_log_message(&klog.record).unwrap();
        assert_eq!(
            data,
            LogsDataBuilder::new(BuilderArgs {
                timestamp_nanos: klog.record.timestamp.into(),
                component_url: Some(KERNEL_IDENTITY.url.clone()),
                moniker: KERNEL_IDENTITY.to_string(),
                severity: Severity::Info,
            })
            .set_pid(klog.record.pid)
            .set_tid(klog.record.tid)
            .add_tag("klog")
            .set_message("test log".to_string())
            .build()
        );
        // make sure the `klog` tag still shows up for legacy listeners
        let log_message: LogMessage = data.into();
        assert_eq!(
            log_message,
            LogMessage {
                pid: klog.record.pid,
                tid: klog.record.tid,
                time: klog.record.timestamp,
                severity: fdiagnostics::Severity::Info.into_primitive() as i32,
                dropped_logs: 0,
                tags: vec!["klog".to_string()],
                msg: "test log".to_string(),
            }
        );

        // maximum allowed klog size
        let klog = TestDebugEntry::new(&vec![b'a'; zx::sys::ZX_LOG_RECORD_DATA_MAX]);
        let data = convert_debuglog_to_log_message(&klog.record).unwrap();
        assert_eq!(
            data,
            LogsDataBuilder::new(BuilderArgs {
                timestamp_nanos: klog.record.timestamp.into(),
                component_url: Some(KERNEL_IDENTITY.url.clone()),
                moniker: KERNEL_IDENTITY.to_string(),
                severity: Severity::Info,
            })
            .set_pid(klog.record.pid)
            .set_tid(klog.record.tid)
            .add_tag("klog")
            .set_message(String::from_utf8(vec![b'a'; zx::sys::ZX_LOG_RECORD_DATA_MAX]).unwrap())
            .build()
        );

        // empty message
        let klog = TestDebugEntry::new(&[]);
        let data = convert_debuglog_to_log_message(&klog.record).unwrap();
        assert_eq!(
            data,
            LogsDataBuilder::new(BuilderArgs {
                timestamp_nanos: klog.record.timestamp.into(),
                component_url: Some(KERNEL_IDENTITY.url.clone()),
                moniker: KERNEL_IDENTITY.to_string(),
                severity: Severity::Info,
            })
            .set_pid(klog.record.pid)
            .set_tid(klog.record.tid)
            .add_tag("klog")
            .set_message("".to_string())
            .build()
        );

        // invalid utf-8
        let klog = TestDebugEntry::new(b"\x00\x9f\x92");
        assert!(convert_debuglog_to_log_message(&klog.record)
            .unwrap()
            .msg()
            .unwrap()
            .contains("INVALID UTF-8 SEE https://fxbug.dev/88259, message may be corrupted: "));
    }

    #[fasync::run_until_stalled(test)]
    async fn logger_existing_logs_test() {
        let debug_log = TestDebugLog::default();
        let klog = TestDebugEntry::new("test log".as_bytes());
        debug_log.enqueue_read_entry(&klog).await;
        debug_log.enqueue_read_fail(zx::Status::SHOULD_WAIT).await;
        let mut log_bridge = DebugLogBridge::create(debug_log);

        assert_eq!(
            log_bridge
                .existing_logs()
                .await
                .unwrap()
                .into_iter()
                .map(|m| m.parse(&KERNEL_IDENTITY).unwrap())
                .collect::<Vec<_>>(),
            vec![LogsDataBuilder::new(BuilderArgs {
                timestamp_nanos: klog.record.timestamp.into(),
                component_url: Some(KERNEL_IDENTITY.url.clone()),
                moniker: KERNEL_IDENTITY.to_string(),
                severity: Severity::Info,
            })
            .set_pid(klog.record.pid)
            .set_tid(klog.record.tid)
            .add_tag("klog")
            .set_message("test log".to_string())
            .build()]
        );

        // Unprocessable logs should be skipped.
        let debug_log = TestDebugLog::default();
        // This is a malformed record because the message contains invalid UTF8.
        let malformed_klog = TestDebugEntry::new(b"\x80");
        debug_log.enqueue_read_entry(&malformed_klog).await;

        debug_log.enqueue_read_fail(zx::Status::SHOULD_WAIT).await;
        let mut log_bridge = DebugLogBridge::create(debug_log);
        assert!(!log_bridge.existing_logs().await.unwrap().is_empty());
    }

    #[fasync::run_until_stalled(test)]
    async fn logger_keep_listening_after_exhausting_initial_contents_test() {
        let debug_log = TestDebugLog::default();
        debug_log.enqueue_read_entry(&TestDebugEntry::new("test log".as_bytes())).await;
        debug_log.enqueue_read_fail(zx::Status::SHOULD_WAIT).await;
        debug_log.enqueue_read_entry(&TestDebugEntry::new("second test log".as_bytes())).await;
        let log_bridge = DebugLogBridge::create(debug_log);
        let mut log_stream =
            Box::pin(log_bridge.listen()).map(|r| r.unwrap().parse(&KERNEL_IDENTITY));
        let log_message = log_stream.try_next().await.unwrap().unwrap();
        assert_eq!(log_message.msg().unwrap(), "test log");
        let log_message = log_stream.try_next().await.unwrap().unwrap();
        assert_eq!(log_message.msg().unwrap(), "second test log");

        // Unprocessable logs should NOT be skipped.
        let debug_log = TestDebugLog::default();
        // This is a malformed record because the message contains invalid UTF8.
        let malformed_klog = TestDebugEntry::new(b"\x80");
        debug_log.enqueue_read_entry(&malformed_klog).await;

        debug_log.enqueue_read_entry(&TestDebugEntry::new("test log".as_bytes())).await;
        let log_bridge = DebugLogBridge::create(debug_log);
        let mut log_stream = Box::pin(log_bridge.listen());
        let log_message =
            log_stream.try_next().await.unwrap().unwrap().parse(&KERNEL_IDENTITY).unwrap();
        assert_eq!(
            log_message.msg().unwrap(),
            "INVALID UTF-8 SEE https://fxbug.dev/88259, message may be corrupted: �"
        );
    }

    #[fasync::run_until_stalled(test)]
    async fn severity_parsed_from_log() {
        let debug_log = TestDebugLog::default();
        debug_log.enqueue_read_entry(&TestDebugEntry::new("ERROR: first log".as_bytes())).await;
        // We look for the string 'ERROR:' to label this as a Severity::Error.
        debug_log.enqueue_read_entry(&TestDebugEntry::new("first log error".as_bytes())).await;
        debug_log.enqueue_read_entry(&TestDebugEntry::new("WARNING: second log".as_bytes())).await;
        debug_log.enqueue_read_entry(&TestDebugEntry::new("INFO: third log".as_bytes())).await;
        debug_log.enqueue_read_entry(&TestDebugEntry::new("fourth log".as_bytes())).await;
        // Create a string prefixed with multi-byte UTF-8 characters. This entry will be labeled as
        // Info rather than Error because the string "ERROR:" only appears after the
        // MAX_STRING_SEARCH_SIZE. It's crucial that we use multi-byte UTF-8 characters because we
        // want to verify that the search is character oriented rather than byte oriented and that
        // it can handle the MAX_STRING_SEARCH_SIZE boundary falling in the middle of a multi-byte
        // character.
        let long_padding = (0..100).map(|_| "\u{10FF}").collect::<String>();
        let long_log = format!("{}ERROR: fifth log", long_padding);
        debug_log.enqueue_read_entry(&TestDebugEntry::new(long_log.as_bytes())).await;

        let log_bridge = DebugLogBridge::create(debug_log);
        let mut log_stream =
            Box::pin(log_bridge.listen()).map(|r| r.unwrap().parse(&KERNEL_IDENTITY));

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

        // TODO(fxbug.dev/74601): Once 74601 is resolved, uncomment the lines below. Prior to 74601
        // being resolved, the follow case may fail because the line is very long, may be truncated,
        // and if it is truncated, may no longer be valid UTF8 because the truncation may occur in
        // the middle of a multi-byte character.
        //
        // let log_message = log_stream.try_next().await.unwrap().unwrap();
        // assert_eq!(log_message.msg().unwrap(), &long_log);
        // assert_eq!(log_message.metadata.severity, Severity::Info);
    }
}
