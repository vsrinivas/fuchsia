// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::target::WeakTarget,
    anyhow::{anyhow, bail, Context, Result},
    diagnostics_data::{LogsData, Timestamp},
    ffx_config::get,
    fidl::endpoints::create_proxy,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_developer_remotecontrol::{
        ArchiveIteratorMarker, BridgeStreamParameters, RemoteDiagnosticsBridgeMarker,
    },
    futures::TryFutureExt,
    selectors::parse_selector,
    std::future::Future,
    std::sync::Arc,
    std::time::SystemTime,
    streamer::{EventType, GenericDiagnosticsStreamer, LogData, LogEntry},
};

pub mod streamer;

const BRIDGE_SELECTOR: &str =
    "core/remote-diagnostics-bridge:out:fuchsia.developer.remotecontrol.RemoteDiagnosticsBridge";

fn get_timestamp() -> Result<Timestamp> {
    Ok(Timestamp::from(
        SystemTime::now()
            .duration_since(SystemTime::UNIX_EPOCH)
            .context("system time before Unix epoch")?
            .as_nanos(),
    ))
}

fn write_logs_to_file<T: GenericDiagnosticsStreamer + 'static + Send + ?Sized>(
    streamer: Arc<T>,
) -> Result<(ServerEnd<ArchiveIteratorMarker>, impl Future<Output = Result<()>>)> {
    let (proxy, server) =
        create_proxy::<ArchiveIteratorMarker>().context("failed to create endpoints")?;

    let listener_fut = async move {
        let mut skip_timestamp = streamer.read_most_recent_timestamp().await?;
        streamer
            .append_logs(vec![LogEntry {
                data: LogData::FfxEvent(EventType::LoggingStarted),
                version: 1,
                timestamp: get_timestamp()?,
            }])
            .await?;

        loop {
            let result = proxy.get_next().await.context("waiting for new log")?;
            match result {
                Ok(logs) => {
                    if logs.is_empty() {
                        break;
                    }
                    let ts = Timestamp::from(get_timestamp()?);
                    let log_data = logs
                        .iter()
                        .map(|l| l.data.clone())
                        .filter(|l| l.is_some())
                        .map(|l| l.unwrap())
                        .map(|s| {
                            let data: LogData = match serde_json::from_str::<LogsData>(&s) {
                                Ok(data) => LogData::TargetLog(data),
                                Err(_) => LogData::MalformedTargetLog(s.clone()),
                            };

                            LogEntry { data: data, timestamp: ts, version: 1 }
                        })
                        .filter(|log| {
                            // TODO(jwing): use a monotonic ID instead of timestamp
                            // once fxbug.dev/61795 is resolved.
                            if let Some(ts) = skip_timestamp {
                                match &log.data {
                                    LogData::TargetLog(log_data) => {
                                        if log_data.metadata.timestamp > ts {
                                            skip_timestamp = None;
                                            true
                                        } else {
                                            false
                                        }
                                    }
                                    _ => true,
                                }
                            } else {
                                true
                            }
                        })
                        .collect();

                    streamer.append_logs(log_data).await?;
                }
                Err(e) => {
                    // TODO(jwing): consider exiting if we see a large number of successive errors
                    // from the diagnostics bridge.
                    log::warn!("got an error from diagnostics bridge {:?}", e);
                }
            }
        }
        let resp: Result<()> = Ok(());
        resp
    };

    return Ok((server, listener_fut));
}

pub struct Logger {
    target: WeakTarget,
    enabled: Option<bool>,
    streamer: Option<Arc<dyn GenericDiagnosticsStreamer + Send + Sync>>,
}

impl Logger {
    pub fn new(target: WeakTarget) -> Self {
        return Self { target: target, enabled: None, streamer: None };
    }

    #[cfg(test)]
    pub fn new_with_streamer_and_config(
        target: WeakTarget,
        streamer: impl GenericDiagnosticsStreamer + 'static + Send + Sync,
        enabled: bool,
    ) -> Self {
        return Self { target, enabled: Some(enabled), streamer: Some(Arc::new(streamer)) };
    }

    pub fn start(self) -> impl Future<Output = Result<(), String>> + Send {
        async move {
            let enabled = match self.enabled {
                Some(e) => e,
                None => get("proactive_log.enabled").await.unwrap_or(false),
            };
            if !enabled {
                log::info!("proactive logger disabled. exiting...");
                return Ok(());
            }

            self.run_logger()
                .map_err(|e| {
                    log::error!("error running logger: {:?}", e);
                    format!("{}", e)
                })
                .await
        }
    }

    async fn run_logger(&self) -> Result<()> {
        let target = self.target.upgrade().context("lost parent Arc")?;

        log::info!("starting logger for {}", target.nodename_str().await);
        let remote_proxy = target.rcs().await.context("failed to get RCS")?.proxy;

        let (log_proxy, log_server_end) = create_proxy::<RemoteDiagnosticsBridgeMarker>()?;
        let selector = parse_selector(BRIDGE_SELECTOR).unwrap();

        match remote_proxy.connect(selector, log_server_end.into_channel()).await? {
            Ok(_) => {}
            Err(e) => {
                log::info!(
                    "attempt to connect to logger for {} failed. {:?}",
                    target.nodename_str().await,
                    e
                );
                bail!("{:?}", e);
            }
        };

        let streamer = if self.streamer.is_some() {
            self.streamer.as_ref().unwrap().clone()
        } else {
            target.stream_info()
        };

        let boot_timestamp = target
            .boot_timestamp_nanos()
            .await
            .ok_or(anyhow!("no boot timestamp for target {:?}", target.nodename_str().await))?;
        streamer.setup_stream(target.nodename_str().await, boot_timestamp).await?;

        let (listener_client, listener_fut) = write_logs_to_file(streamer.clone())?;
        let params = BridgeStreamParameters {
            stream_mode: Some(fidl_fuchsia_diagnostics::StreamMode::SnapshotThenSubscribe),
            data_type: Some(fidl_fuchsia_diagnostics::DataType::Logs),
            ..BridgeStreamParameters::EMPTY
        };
        let _ = log_proxy
            .stream_diagnostics(params, listener_client)
            .await?
            .map_err(|s| anyhow!("failure setting up diagnostics stream: {:?}", s))?;
        let _: () = listener_fut.await?;

        Ok(())
    }
}

#[cfg(test)]
mod test {
    use {
        super::*,
        crate::target::{RcsConnection, Target},
        crate::NodeId,
        async_std::sync::Mutex,
        async_trait::async_trait,
        diagnostics_data::{LogsField, Severity},
        diagnostics_hierarchy::{DiagnosticsHierarchy, Property},
        fidl::endpoints::RequestStream,
        fidl_fuchsia_developer_remotecontrol::{
            ArchiveIteratorEntry, ArchiveIteratorError, ArchiveIteratorMarker,
            ArchiveIteratorRequest, IdentifyHostResponse, RemoteControlMarker, RemoteControlProxy,
            RemoteControlRequest, RemoteDiagnosticsBridgeRequest,
            RemoteDiagnosticsBridgeRequestStream, ServiceMatch,
        },
        fidl_fuchsia_diagnostics::{DataType, StreamMode},
        futures::TryStreamExt,
    };

    const NODENAME: &str = "nodename-foo";
    const BOOT_TIME: u64 = 98765432123;

    #[derive(Default)]
    struct FakeDiagnosticsStreamerInner {
        nodename: String,
        boot_time: u64,
        log_buf: Arc<Mutex<Vec<LogEntry>>>,
        most_recent_ts: u64,
        expect_setup: bool,
    }
    struct FakeDiagnosticsStreamer {
        // This struct has to be Send + Sync to be compatible with the Logger implementation.
        inner: Mutex<FakeDiagnosticsStreamerInner>,
    }

    impl FakeDiagnosticsStreamer {
        fn new(most_recent_ts: u64, log_buf: Arc<Mutex<Vec<LogEntry>>>) -> Self {
            Self {
                inner: Mutex::new(FakeDiagnosticsStreamerInner {
                    most_recent_ts,
                    log_buf,
                    ..FakeDiagnosticsStreamerInner::default()
                }),
            }
        }

        async fn expect_setup(&self, nodename: &str, boot_time: u64) {
            let mut inner = self.inner.lock().await;
            inner.expect_setup = true;
            inner.nodename = nodename.to_string();
            inner.boot_time = boot_time;
        }
    }

    #[async_trait]
    impl GenericDiagnosticsStreamer for FakeDiagnosticsStreamer {
        async fn setup_stream(
            &self,
            target_nodename: String,
            target_boot_time_nanos: u64,
        ) -> Result<()> {
            let inner = self.inner.lock().await;
            if !inner.expect_setup {
                panic!("unexpected call to setup_stream");
            }
            assert_eq!(inner.nodename, target_nodename);
            assert_eq!(inner.boot_time, target_boot_time_nanos);
            Ok(())
        }

        async fn append_logs(&self, entries: Vec<LogEntry>) -> Result<()> {
            let inner = self.inner.lock().await;
            inner.log_buf.lock().await.extend(entries);
            Ok(())
        }

        async fn read_most_recent_timestamp(&self) -> Result<Option<Timestamp>> {
            let inner = self.inner.lock().await;
            Ok(Some(Timestamp::from(inner.most_recent_ts)))
        }
    }

    async fn verify_logged(got: Arc<Mutex<Vec<LogEntry>>>, expected: Vec<LogEntry>) {
        let logs = got.lock().await;

        assert_eq!(
            logs.len(),
            expected.len(),
            "length mismatch: \ngot: {:?}\nexpected: {:?}",
            logs,
            expected
        );

        for (got, expected) in logs.iter().zip(expected.iter()) {
            assert_eq!(
                got.data, expected.data,
                "mismatched data. got: {:?}\nexpected: {:?}",
                got.data, expected.data
            );
            assert_eq!(
                got.version, expected.version,
                "mismatched version. got: {:?}\nexpected: {:?}",
                got.version, expected.version
            );
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

    fn setup_fake_archive_accessor(
        chan: fidl::Channel,
        responses: Arc<Vec<FakeArchiveIteratorResponse>>,
    ) -> Result<()> {
        let mut stream = RemoteDiagnosticsBridgeRequestStream::from_channel(
            fidl::AsyncChannel::from_channel(chan)?,
        );
        fuchsia_async::Task::spawn(async move {
            while let Ok(Some(req)) = stream.try_next().await {
                match req {
                    RemoteDiagnosticsBridgeRequest::StreamDiagnostics {
                        responder,
                        iterator,
                        parameters,
                    } => {
                        assert_eq!(parameters.data_type.unwrap(), DataType::Logs);
                        assert_eq!(
                            parameters.stream_mode.unwrap(),
                            StreamMode::SnapshotThenSubscribe
                        );
                        setup_fake_archive_iterator(iterator, responses.clone()).unwrap();
                        responder.send(&mut Ok(())).unwrap();
                    }
                    _ => assert!(false),
                }
            }
        })
        .detach();
        Ok(())
    }

    fn setup_fake_remote_control_service(
        responses: Arc<Vec<FakeArchiveIteratorResponse>>,
    ) -> RemoteControlProxy {
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<RemoteControlMarker>().unwrap();

        fuchsia_async::Task::spawn(async move {
            while let Ok(Some(req)) = stream.try_next().await {
                match req {
                    RemoteControlRequest::Connect { selector: _, service_chan, responder } => {
                        setup_fake_archive_accessor(service_chan, responses.clone()).unwrap();
                        responder
                            .send(&mut Ok(ServiceMatch {
                                moniker: vec![],
                                subdir: String::default(),
                                service: String::default(),
                            }))
                            .unwrap();
                    }
                    RemoteControlRequest::IdentifyHost { responder } => {
                        responder
                            .send(&mut Ok(IdentifyHostResponse {
                                nodename: Some(NODENAME.to_string()),
                                addresses: None,
                                boot_timestamp_nanos: Some(BOOT_TIME),
                                ..IdentifyHostResponse::EMPTY
                            }))
                            .context("sending testing response")
                            .unwrap();
                    }
                    r => assert!(false, "{:?}", r),
                }
            }
        })
        .detach();

        proxy
    }

    fn logging_started_entry() -> LogEntry {
        LogEntry {
            data: LogData::FfxEvent(EventType::LoggingStarted),
            timestamp: Timestamp::from(0u64),
            version: 1,
        }
    }

    fn malformed_log(s: &str) -> LogEntry {
        LogEntry {
            data: LogData::MalformedTargetLog(s.to_string()),
            timestamp: Timestamp::from(0u64),
            version: 1,
        }
    }

    fn valid_log(data: LogsData) -> LogEntry {
        LogEntry { data: LogData::TargetLog(data), timestamp: Timestamp::from(0u64), version: 1 }
    }

    fn target_log(timestamp: u64, msg: &str) -> LogsData {
        let hierarchy = DiagnosticsHierarchy::new(
            "root",
            vec![Property::String(LogsField::Msg, msg.to_string())],
            vec![],
        );
        LogsData::for_logs(
            String::from("test/moniker"),
            Some(hierarchy),
            timestamp,
            String::from("fake-url"),
            Severity::Error,
            1,
            vec![],
        )
    }

    async fn make_default_target(expected_logs: Vec<FakeArchiveIteratorResponse>) -> Target {
        let ascendd = Arc::new(crate::onet::create_ascendd().await.unwrap());
        let conn = RcsConnection::new_with_proxy(
            ascendd,
            setup_fake_remote_control_service(Arc::new(expected_logs)),
            &NodeId { id: 1234 },
        );
        Target::from_rcs_connection(conn).await.unwrap()
    }
    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_disabled() -> Result<()> {
        let target = make_default_target(vec![]).await;
        let t = target.downgrade();

        let streamer = FakeDiagnosticsStreamer::new(1, Arc::new(Mutex::new(vec![])));
        let logger = Logger::new_with_streamer_and_config(t, streamer, false);
        logger.start().await.unwrap();
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_multiple_malformed_logs_in_series() -> Result<()> {
        let target = make_default_target(vec![
            FakeArchiveIteratorResponse {
                values: vec!["log1".to_string(), "log2".to_string()],
                iterator_error: None,
            },
            FakeArchiveIteratorResponse {
                values: vec!["log3".to_string(), "log4".to_string()],
                iterator_error: None,
            },
        ])
        .await;
        let t = target.downgrade();

        let log_buf = Arc::new(Mutex::new(vec![]));
        let streamer = FakeDiagnosticsStreamer::new(0, log_buf.clone());
        streamer.expect_setup(NODENAME, BOOT_TIME).await;
        let logger = Logger::new_with_streamer_and_config(t, streamer, true);
        logger.start().await.unwrap();

        verify_logged(
            log_buf.clone(),
            vec![
                logging_started_entry(),
                malformed_log("log1"),
                malformed_log("log2"),
                malformed_log("log3"),
                malformed_log("log4"),
            ],
        )
        .await;
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_multiple_valid_logs_in_series() -> Result<()> {
        let log1 = target_log(1, "log1");
        let log2 = target_log(2, "log2");
        let log3 = target_log(3, "log3");
        let log4 = target_log(4, "log4");
        let target = make_default_target(vec![
            FakeArchiveIteratorResponse {
                values: vec![serde_json::to_string(&log1)?, serde_json::to_string(&log2)?],
                iterator_error: None,
            },
            FakeArchiveIteratorResponse {
                values: vec![serde_json::to_string(&log3)?, serde_json::to_string(&log4)?],
                iterator_error: None,
            },
        ])
        .await;
        let t = target.downgrade();

        let log_buf = Arc::new(Mutex::new(vec![]));

        let streamer = FakeDiagnosticsStreamer::new(0, log_buf.clone());
        streamer.expect_setup(NODENAME, BOOT_TIME).await;
        let logger = Logger::new_with_streamer_and_config(t, streamer, true);
        logger.start().await.unwrap();

        verify_logged(
            log_buf.clone(),
            vec![
                logging_started_entry(),
                valid_log(log1),
                valid_log(log2),
                valid_log(log3),
                valid_log(log4),
            ],
        )
        .await;
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_skips_old_logs() -> Result<()> {
        let log1 = target_log(1, "log1");
        let log2 = target_log(2, "log2");
        let target = make_default_target(vec![
            FakeArchiveIteratorResponse {
                values: vec![serde_json::to_string(&log1)?],
                iterator_error: None,
            },
            FakeArchiveIteratorResponse {
                values: vec![serde_json::to_string(&log2)?],
                iterator_error: None,
            },
        ])
        .await;
        let t = target.downgrade();

        let log_buf = Arc::new(Mutex::new(vec![]));

        let streamer = FakeDiagnosticsStreamer::new(1, log_buf.clone());
        streamer.expect_setup(NODENAME, BOOT_TIME).await;
        let logger = Logger::new_with_streamer_and_config(t, streamer, true);
        logger.start().await.unwrap();

        verify_logged(log_buf.clone(), vec![logging_started_entry(), valid_log(log2)]).await;
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_continues_after_error() -> Result<()> {
        let log1 = target_log(1, "log1");
        let log2 = target_log(2, "log2");
        let target = make_default_target(vec![
            FakeArchiveIteratorResponse {
                values: vec![serde_json::to_string(&log1)?],
                iterator_error: None,
            },
            FakeArchiveIteratorResponse {
                values: vec![],
                iterator_error: Some(ArchiveIteratorError::DataReadFailed),
            },
            FakeArchiveIteratorResponse {
                values: vec![serde_json::to_string(&log2)?],
                iterator_error: None,
            },
        ])
        .await;
        let t = target.downgrade();

        let log_buf = Arc::new(Mutex::new(vec![]));

        let streamer = FakeDiagnosticsStreamer::new(0, log_buf.clone());
        streamer.expect_setup(NODENAME, BOOT_TIME).await;
        let logger = Logger::new_with_streamer_and_config(t, streamer, true);
        logger.start().await.unwrap();

        verify_logged(
            log_buf.clone(),
            vec![logging_started_entry(), valid_log(log1), valid_log(log2)],
        )
        .await;
        Ok(())
    }
}
