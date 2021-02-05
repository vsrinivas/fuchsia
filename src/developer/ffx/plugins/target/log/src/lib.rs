// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Result},
    async_std::{
        io::{stdout, Write},
        prelude::*,
    },
    async_trait::async_trait,
    diagnostics_data::Timestamp,
    ffx_core::ffx_plugin,
    ffx_log_args::{DumpCommand, LogCommand, LogSubCommand, WatchCommand},
    ffx_log_data::{EventType, LogData, LogEntry},
    fidl::endpoints::create_proxy,
    fidl_fuchsia_developer_bridge::{
        self as bridge, DaemonDiagnosticsStreamParameters, StreamMode,
    },
    fidl_fuchsia_developer_remotecontrol::{
        ArchiveIteratorEntry, ArchiveIteratorError, ArchiveIteratorMarker,
    },
};

type ArchiveIteratorResult = Result<LogEntry, ArchiveIteratorError>;

fn timestamp_to_partial_secs(ts: Timestamp) -> f64 {
    let u_ts: u64 = ts.into();
    u_ts as f64 / 1_000_000_000.0
}

#[async_trait(?Send)]
pub trait LogFormatter {
    async fn push_log(&mut self, log_entry: ArchiveIteratorResult) -> Result<()>;
}

struct DefaultLogFormatter<'a> {
    writer: Box<dyn Write + Unpin + 'a>,
    has_previous_log: bool,
}

#[async_trait(?Send)]
impl<'a> LogFormatter for DefaultLogFormatter<'_> {
    async fn push_log(&mut self, log_entry: ArchiveIteratorResult) -> Result<()> {
        let mut s = match log_entry {
            Ok(LogEntry { data, timestamp, .. }) => match data {
                LogData::TargetLog(data) => {
                    let ts = timestamp_to_partial_secs(data.metadata.timestamp);
                    format!("[{:05.3}][{}] {}", ts, data.moniker, data.msg().unwrap())
                }
                LogData::MalformedTargetLog(raw) => {
                    format!("malformed target log: {}", raw)
                }
                LogData::FfxEvent(etype) => match etype {
                    EventType::LoggingStarted => {
                        let mut s = format!(
                            "[{:05.3}][<ffx daemon>] logger started. ",
                            timestamp_to_partial_secs(timestamp)
                        );
                        if self.has_previous_log {
                            s.push_str("Logs before this may have been dropped if they were not cached on the target.");
                        }
                        s
                    }
                },
            },
            Err(e) => {
                format!("got an error fetching next log: {:?}", e)
            }
        };
        s.push('\n');

        self.has_previous_log = true;

        let s = self.writer.write(s.as_bytes());
        s.await.map(|_| ()).map_err(|e| anyhow!(e))
    }
}

impl<'a> DefaultLogFormatter<'a> {
    fn new(writer: impl Write + Unpin + 'a) -> Self {
        Self { writer: Box::new(writer), has_previous_log: false }
    }
}

#[ffx_plugin("proactive_log.enabled")]
pub async fn log(daemon_proxy: bridge::DaemonProxy, cmd: LogCommand) -> Result<()> {
    let mut formatter = DefaultLogFormatter::new(stdout());
    let ffx: ffx_lib_args::Ffx = argh::from_env();
    let target_str = ffx.target.unwrap_or(String::default());
    log_cmd(daemon_proxy, &mut formatter, target_str, cmd).await
}

pub async fn log_cmd(
    daemon_proxy: bridge::DaemonProxy,
    log_formatter: &mut impl LogFormatter,
    target_str: String,
    cmd: LogCommand,
) -> Result<()> {
    let (proxy, server) =
        create_proxy::<ArchiveIteratorMarker>().context("failed to create endpoints")?;

    let stream_mode = match cmd.cmd {
        LogSubCommand::Watch(WatchCommand { dump }) => {
            if dump {
                StreamMode::SnapshotRecentThenSubscribe
            } else {
                StreamMode::Subscribe
            }
        }
        LogSubCommand::Dump(DumpCommand {}) => StreamMode::SnapshotAll,
    };

    let params = DaemonDiagnosticsStreamParameters {
        stream_mode: Some(stream_mode),
        ..DaemonDiagnosticsStreamParameters::EMPTY
    };
    let _ = daemon_proxy
        .stream_diagnostics(Some(&target_str), params, server)
        .await?
        .map_err(|s| anyhow!("failure setting up diagnostics stream: {:?}", s))?;

    loop {
        let next = proxy.get_next().await.context("waiting for new log")?;
        let vec = match next {
            Ok(l) => l,
            Err(e) => {
                log_formatter.push_log(Err(e)).await?;
                continue;
            }
        };

        if vec.is_empty() {
            break;
        }

        for ArchiveIteratorEntry { data, .. } in vec.iter() {
            let parsed: LogEntry = serde_json::from_str(data.as_ref().unwrap())?;
            log_formatter.push_log(Ok(parsed)).await?;
        }
    }

    Ok(())
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use {
        super::*,
        async_std::sync::Arc,
        diagnostics_data::Timestamp,
        fidl::endpoints::ServerEnd,
        fidl_fuchsia_developer_remotecontrol::{
            ArchiveIteratorEntry, ArchiveIteratorError, ArchiveIteratorRequest,
        },
        futures::TryStreamExt,
    };

    const DEFAULT_TS: u64 = 1234567;
    const DEFAULT_TARGET_STR: &str = "target-target";

    struct FakeLogFormatter {
        pushed_logs: Vec<ArchiveIteratorResult>,
    }

    #[async_trait(?Send)]
    impl LogFormatter for FakeLogFormatter {
        async fn push_log(&mut self, log_entry: ArchiveIteratorResult) -> Result<()> {
            self.pushed_logs.push(log_entry);
            Ok(())
        }
    }

    impl FakeLogFormatter {
        fn new() -> Self {
            Self { pushed_logs: vec![] }
        }

        fn assert_same_logs(&self, expected: Vec<ArchiveIteratorResult>) {
            assert_eq!(
                self.pushed_logs.len(),
                expected.len(),
                "got different number of log entries. \ngot: {:?}\nexpected: {:?}",
                self.pushed_logs,
                expected
            );
            for (got, expected_log) in self.pushed_logs.iter().zip(expected.iter()) {
                assert_eq!(
                    got, expected_log,
                    "got different log entries. \ngot: {:?}\nexpected: {:?}\n",
                    got, expected_log
                );
            }
        }
    }
    struct FakeArchiveIteratorResponse {
        values: Vec<String>,
        iterator_error: Option<ArchiveIteratorError>,
    }

    fn setup_fake_archive_iterator(
        server_end: ServerEnd<ArchiveIteratorMarker>,
        responses: Arc<Vec<FakeArchiveIteratorResponse>>,
    ) -> Result<()> {
        let mut stream = server_end.into_stream()?;
        fuchsia_async::Task::spawn(async move {
            let mut iter = responses.iter();
            while let Ok(Some(req)) = stream.try_next().await {
                match req {
                    ArchiveIteratorRequest::GetNext { responder } => {
                        let next = iter.next();
                        match next {
                            Some(FakeArchiveIteratorResponse { values, iterator_error }) => {
                                if let Some(err) = iterator_error {
                                    responder.send(&mut Err(*err)).unwrap();
                                } else {
                                    responder
                                        .send(&mut Ok(values
                                            .iter()
                                            .map(|s| ArchiveIteratorEntry {
                                                data: Some(s.clone()),
                                                truncated_chars: Some(0),
                                                ..ArchiveIteratorEntry::EMPTY
                                            })
                                            .collect()))
                                        .unwrap()
                                }
                            }
                            None => responder.send(&mut Ok(vec![])).unwrap(),
                        }
                    }
                }
            }
        })
        .detach();
        Ok(())
    }

    fn setup_fake_daemon_server(
        expected_parameters: DaemonDiagnosticsStreamParameters,
        expected_responses: Arc<Vec<FakeArchiveIteratorResponse>>,
    ) -> bridge::DaemonProxy {
        setup_fake_daemon_proxy(move |req| match req {
            bridge::DaemonRequest::StreamDiagnostics {
                target,
                parameters,
                iterator,
                responder,
            } => {
                assert_eq!(parameters, expected_parameters);
                assert_eq!(target, Some(DEFAULT_TARGET_STR.to_string()));
                setup_fake_archive_iterator(iterator, expected_responses.clone()).unwrap();
                responder.send(&mut Ok(())).context("error sending response").expect("should send");
            }
            _ => assert!(false),
        })
    }

    fn make_log_entry(log_data: LogData) -> LogEntry {
        LogEntry { version: 1, timestamp: Timestamp::from(DEFAULT_TS), data: log_data }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dump_empty() {
        let mut formatter = FakeLogFormatter::new();
        let cmd = LogCommand { cmd: LogSubCommand::Dump(DumpCommand {}) };
        let params = DaemonDiagnosticsStreamParameters {
            stream_mode: Some(StreamMode::SnapshotAll),
            ..DaemonDiagnosticsStreamParameters::EMPTY
        };
        let expected_responses = vec![];

        log_cmd(
            setup_fake_daemon_server(params, Arc::new(expected_responses)),
            &mut formatter,
            String::from(DEFAULT_TARGET_STR),
            cmd,
        )
        .await
        .unwrap();

        formatter.assert_same_logs(vec![])
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_watch() {
        let mut formatter = FakeLogFormatter::new();
        let cmd = LogCommand { cmd: LogSubCommand::Watch(WatchCommand { dump: true }) };
        let params = DaemonDiagnosticsStreamParameters {
            stream_mode: Some(StreamMode::SnapshotRecentThenSubscribe),
            ..DaemonDiagnosticsStreamParameters::EMPTY
        };
        let log1 = make_log_entry(LogData::FfxEvent(EventType::LoggingStarted));
        let log2 = make_log_entry(LogData::MalformedTargetLog("text".to_string()));
        let log3 = make_log_entry(LogData::MalformedTargetLog("text2".to_string()));

        let expected_responses = vec![
            FakeArchiveIteratorResponse {
                values: vec![
                    serde_json::to_string(&log1).unwrap(),
                    serde_json::to_string(&log2).unwrap(),
                ],
                iterator_error: None,
            },
            FakeArchiveIteratorResponse {
                values: vec![serde_json::to_string(&log3).unwrap()],
                iterator_error: None,
            },
        ];

        log_cmd(
            setup_fake_daemon_server(params, Arc::new(expected_responses)),
            &mut formatter,
            String::from(DEFAULT_TARGET_STR),
            cmd,
        )
        .await
        .unwrap();

        formatter.assert_same_logs(vec![Ok(log1), Ok(log2), Ok(log3)])
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_watch_no_dump_with_error() {
        let mut formatter = FakeLogFormatter::new();
        let cmd = LogCommand { cmd: LogSubCommand::Watch(WatchCommand { dump: false }) };
        let params = DaemonDiagnosticsStreamParameters {
            stream_mode: Some(StreamMode::Subscribe),
            ..DaemonDiagnosticsStreamParameters::EMPTY
        };
        let log1 = make_log_entry(LogData::FfxEvent(EventType::LoggingStarted));
        let log2 = make_log_entry(LogData::MalformedTargetLog("text".to_string()));
        let log3 = make_log_entry(LogData::MalformedTargetLog("text2".to_string()));

        let expected_responses = vec![
            FakeArchiveIteratorResponse {
                values: vec![
                    serde_json::to_string(&log1).unwrap(),
                    serde_json::to_string(&log2).unwrap(),
                ],
                iterator_error: None,
            },
            FakeArchiveIteratorResponse {
                values: vec![],
                iterator_error: Some(ArchiveIteratorError::GenericError),
            },
            FakeArchiveIteratorResponse {
                values: vec![serde_json::to_string(&log3).unwrap()],
                iterator_error: None,
            },
        ];

        log_cmd(
            setup_fake_daemon_server(params, Arc::new(expected_responses)),
            &mut formatter,
            String::from(DEFAULT_TARGET_STR),
            cmd,
        )
        .await
        .unwrap();

        formatter.assert_same_logs(vec![
            Ok(log1),
            Ok(log2),
            Err(ArchiveIteratorError::GenericError),
            Ok(log3),
        ])
    }
}
