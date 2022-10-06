// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::target::Target,
    anyhow::{anyhow, bail, Context, Result},
    async_channel::bounded,
    diagnostics_data::{LogsData, Timestamp},
    ffx_config::get,
    ffx_log_data::{EventType, LogData, LogEntry},
    ffx_log_utils::{
        run_logging_pipeline,
        symbolizer::{is_symbolizer_context_marker, LogSymbolizer, Symbolizer},
        OrderedBatchPipeline,
    },
    fidl::endpoints::create_proxy,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_developer_remotecontrol::{
        ArchiveIteratorMarker, BridgeStreamParameters, DiagnosticsData, InlineData,
        RemoteDiagnosticsBridgeMarker,
    },
    fidl_fuchsia_diagnostics::ClientSelectorConfiguration,
    futures::{AsyncReadExt, StreamExt, TryFutureExt},
    selectors::{parse_selector, VerboseError},
    std::convert::TryInto,
    std::future::Future,
    std::rc::Weak,
    std::sync::Arc,
    std::time::SystemTime,
    streamer::GenericDiagnosticsStreamer,
};

pub mod streamer;

const BRIDGE_SELECTOR: &str =
    "core/remote-diagnostics-bridge:expose:fuchsia.developer.remotecontrol.RemoteDiagnosticsBridge";
const ENABLED_CONFIG: &str = "proactive_log.enabled";
const SYMBOLIZE_ENABLED_CONFIG: &str = "proactive_log.symbolize.enabled";
const SYMBOLIZE_ARGS_CONFIG: &str = "proactive_log.symbolize.extra_args";
const PIPELINE_SIZE: usize = 20;

fn get_timestamp() -> Result<Timestamp> {
    Ok(Timestamp::from(
        SystemTime::now()
            .duration_since(SystemTime::UNIX_EPOCH)
            .context("system time before Unix epoch")?
            .as_nanos() as i64,
    ))
}

fn write_logs_to_file<T: GenericDiagnosticsStreamer + 'static + ?Sized>(
    streamer: Arc<T>,
    symbolizer_config: Option<&SymbolizerConfig>,
) -> Result<(ServerEnd<ArchiveIteratorMarker>, impl Future<Output = Result<()>> + '_)> {
    let (proxy, server) =
        create_proxy::<ArchiveIteratorMarker>().context("failed to create endpoints")?;

    let listener_fut = async move {
        let mut skip_timestamp = streamer.read_most_recent_target_timestamp().await?;
        streamer
            .append_logs(vec![LogEntry {
                data: LogData::FfxEvent(EventType::LoggingStarted),
                version: 1,
                timestamp: get_timestamp()?,
            }])
            .await?;

        let mut requests = OrderedBatchPipeline::new(PIPELINE_SIZE);
        let config;
        let symbolizer_config = match symbolizer_config {
            Some(config) => Some(config),
            None => match SymbolizerConfig::new().await {
                Ok(c) => {
                    config = c;
                    Some(&config)
                }
                Err(e) => {
                    tracing::warn!(
                        "constructing symbolizer config failed. \
                                Proactive logs will not be symbolized. Error was: {}",
                        e
                    );
                    None
                }
            },
        };

        let (sender_tx, sender_rx) = bounded(1);
        let (reader_tx, mut reader_rx) = bounded(1);
        let symbolizer = if let Some(config) = symbolizer_config {
            if config.enabled {
                match config
                    .symbolizer
                    .as_ref()
                    .context("missing symbolizer")?
                    .clone()
                    .start(sender_rx, reader_tx, config.symbolizer_args.clone())
                    .await
                {
                    Ok(()) => Some(config.symbolizer.clone()),
                    Err(e) => {
                        tracing::warn!(
                            "symbolizer failed to start. \
                                    Proactive logs will not be symbolized. Error was: {}",
                            e
                        );
                        None
                    }
                }
            } else {
                tracing::info!(
                    "proactive log symbolization disabled. \
                            proactive logs will not be symbolized"
                );
                None
            }
        } else {
            tracing::info!(
                "no symbolizer config. \
                        proactive logs will not be symbolized"
            );
            None
        };
        loop {
            let (get_next_results, terminal_err) =
                run_logging_pipeline(&mut requests, &proxy).await;

            if get_next_results.is_empty() {
                if let Some(err) = terminal_err {
                    streamer
                        .append_logs(vec![LogEntry::new(LogData::FfxEvent(
                            EventType::TargetDisconnected,
                        ))?])
                        .await?;
                    return Err(anyhow!(err));
                }
                continue;
            }

            let ts = Timestamp::from(get_timestamp()?);
            let log_data_futs = get_next_results
                .into_iter()
                .filter_map(|r| {
                    if let Err(e) = r {
                        // TODO(jwing): consider exiting if we see a large number of successive errors
                        // from the diagnostics bridge.
                        tracing::warn!("got an error from diagnostics bridge {:?}", e);
                    }
                    r.ok()
                })
                .flatten()
                .filter_map(|l| match l.diagnostics_data {
                    Some(d) => Some(d),
                    None => {
                        if let Some(data) = l.data {
                            Some(DiagnosticsData::Inline(InlineData {
                                data,
                                truncated_chars: l.truncated_chars.unwrap_or(0),
                            }))
                        } else {
                            None
                        }
                    }
                })
                .map(|diagnostics_data| async {
                    // There are two types of logs: small ones that fit inline in a message and long
                    // ones that must be transported via a socket.
                    // We deserialize the log data directly from the inline variant or we fetch the
                    // data by reading from the socket and then deserializing.
                    match diagnostics_data {
                        DiagnosticsData::Inline(inline) => {
                            // This is the small log side, we directly receive the data in the
                            // message.
                            let data: LogData = match serde_json::from_str::<LogsData>(&inline.data)
                            {
                                Ok(data) => LogData::TargetLog(data),
                                Err(_) => LogData::MalformedTargetLog(inline.data),
                            };

                            LogEntry { data, timestamp: ts, version: 1 }
                        }
                        DiagnosticsData::Socket(socket) => {
                            // This is the long log side, we must read the data from the socket.
                            let data = match read_target_log_from_socket(socket).await {
                                Ok(data) => LogData::TargetLog(data),
                                Err(data) => LogData::MalformedTargetLog(data),
                            };
                            LogEntry { data, timestamp: ts, version: 1 }
                        }
                    }
                });

            let log_data: Vec<LogEntry> = futures::future::join_all(log_data_futs)
                .await
                .into_iter()
                .filter(|log| {
                    // TODO(jwing): use a monotonic ID instead of timestamp
                    // once fxbug.dev/61795 is resolved.
                    if let Some(ts) = skip_timestamp {
                        match &log.data {
                            LogData::TargetLog(log_data) => {
                                if log_data.metadata.timestamp > *ts {
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

            let mut new_entries = vec![];
            if symbolizer.is_some() {
                for log in log_data.into_iter() {
                    match &log.data {
                        LogData::TargetLog(data) => {
                            let msg = data.msg();
                            if let Some(msg) = msg {
                                if !is_symbolizer_context_marker(msg) {
                                    new_entries.push(log);
                                    continue;
                                }

                                let mut with_newline = String::from(msg);
                                with_newline.push('\n');
                                match sender_tx.send(with_newline).await {
                                    Ok(_) => {}
                                    Err(e) => {
                                        tracing::info!(
                                            "writing to symbolizer channel failed: {}",
                                            e
                                        );
                                        new_entries.push(log);
                                        continue;
                                    }
                                }

                                let out = match reader_rx.next().await {
                                    Some(s) => s,
                                    None => {
                                        tracing::info!("symbolizer stream is empty");
                                        new_entries.push(log);
                                        continue;
                                    }
                                };

                                let mut new_log = log.clone();
                                let new_data = data.clone();

                                // Reconstruct the message by dropping the \n we added above.
                                let mut split: Vec<&str> = out.split('\n').collect();
                                if split.len() > 1 {
                                    split.remove(split.len() - 1);
                                }

                                new_log.data =
                                    LogData::SymbolizedTargetLog(new_data, split.join("\n"));
                                new_entries.push(new_log);
                            } else {
                                new_entries.push(log);
                            }
                        }
                        _ => new_entries.push(log),
                    }
                }
            } else {
                new_entries = log_data;
            }

            streamer.append_logs(new_entries).await?;
            if let Some(err) = terminal_err {
                streamer
                    .append_logs(vec![LogEntry::new(LogData::FfxEvent(
                        EventType::TargetDisconnected,
                    ))?])
                    .await?;
                return Err(anyhow!(err));
            }
        }
    };

    return Ok((server, listener_fut));
}

async fn read_target_log_from_socket(socket: fidl::Socket) -> Result<LogsData, String> {
    let mut socket = fidl::AsyncSocket::from_socket(socket)
        .map_err(|_| "failure to create async socket".to_owned())?;
    let mut result = Vec::new();
    let _ = socket
        .read_to_end(&mut result)
        .await
        .map_err(|_| "failure to read log from socket".to_owned())?;
    serde_json::from_slice::<LogsData>(&result)
        .map_err(|_| String::from_utf8_lossy(&result).into_owned())
}

#[derive(Default)]
pub struct SymbolizerConfig {
    pub symbolizer: Option<Arc<dyn Symbolizer + 'static>>,
    pub enabled: bool,
    pub symbolizer_args: Vec<String>,
}

impl SymbolizerConfig {
    async fn new() -> Result<Self> {
        if get(SYMBOLIZE_ENABLED_CONFIG).await? {
            Ok(Self {
                symbolizer: Some(Arc::new(LogSymbolizer::new())),
                enabled: true,
                symbolizer_args: get(SYMBOLIZE_ARGS_CONFIG).await.unwrap_or_default(),
            })
        } else {
            Ok(Self::default())
        }
    }

    #[cfg(test)]
    fn new_with_config(
        symbolizer: Arc<impl Symbolizer + 'static>,
        enabled: bool,
        symbolizer_args: Vec<String>,
    ) -> Self {
        Self { symbolizer: Some(symbolizer), enabled, symbolizer_args }
    }
}

pub struct Logger {
    target: Weak<Target>,
    enabled: Option<bool>,
    streamer: Option<Arc<dyn GenericDiagnosticsStreamer>>,
    symbolizer: Option<SymbolizerConfig>,
}

impl Logger {
    pub fn new(target: Weak<Target>) -> Self {
        return Self { target: target, enabled: None, streamer: None, symbolizer: None };
    }

    #[cfg(test)]
    pub fn new_with_streamer_and_config(
        target: Weak<Target>,
        streamer: impl GenericDiagnosticsStreamer + 'static,
        enabled: bool,
        symbolizer: SymbolizerConfig,
    ) -> Self {
        return Self {
            target,
            enabled: Some(enabled),
            streamer: Some(Arc::new(streamer)),
            symbolizer: Some(symbolizer),
        };
    }

    pub fn start(self) -> impl Future<Output = Result<(), String>> {
        async move {
            let enabled = match self.enabled {
                Some(e) => e,
                None => get(ENABLED_CONFIG).await.unwrap_or(false),
            };
            if !enabled {
                tracing::info!("proactive logger disabled. exiting...");
                return Ok(());
            }

            self.run_logger()
                .map_err(|e| {
                    tracing::error!("error running logger: {:?}", e);
                    format!("{}", e)
                })
                .await
        }
    }

    async fn run_logger(&self) -> Result<()> {
        let target = self.target.upgrade().context("lost parent Arc")?;

        tracing::info!("starting logger for {}", target.nodename_str());
        let remote_proxy = target.rcs().context("failed to get RCS")?.proxy;
        let nodename = target.nodename_str();

        let (log_proxy, log_server_end) = create_proxy::<RemoteDiagnosticsBridgeMarker>()?;
        let selector = parse_selector::<VerboseError>(BRIDGE_SELECTOR).unwrap();

        match remote_proxy.connect(selector, log_server_end.into_channel()).await? {
            Ok(_) => {}
            Err(e) => {
                tracing::info!("attempt to connect to logger for {} failed. {:?}", nodename, e);
                bail!("{:?}", e);
            }
        };

        let streamer = if self.streamer.is_some() {
            self.streamer.as_ref().unwrap().clone()
        } else {
            target.stream_info()
        };

        let nodename = target.nodename_str();
        let boot_timestamp: i64 = target
            .boot_timestamp_nanos()
            .with_context(|| format!("no boot timestamp for target {:?}", &nodename))?
            .try_into()?;
        streamer.setup_stream(nodename.clone(), boot_timestamp).await?;

        // Garbage collect old sessions before kicking off the log stream.
        match streamer.clean_sessions_for_target().await {
            Ok(()) => {}
            Err(e) => {
                tracing::warn!("cleaning sessions for {} failed: {}. logging will proceed anyway. \
                            If space usage is excessive, try restarting the ffx daemon or disabling proactive logging ('ffx config set proactive_log.enabled false').",
                            nodename, e);
            }
        }

        let (listener_client, listener_fut) =
            write_logs_to_file(streamer.clone(), self.symbolizer.as_ref())?;
        let params = BridgeStreamParameters {
            stream_mode: Some(fidl_fuchsia_diagnostics::StreamMode::SnapshotThenSubscribe),
            data_type: Some(fidl_fuchsia_diagnostics::DataType::Logs),
            client_selector_configuration: Some(ClientSelectorConfiguration::SelectAll(true)),
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
        crate::target::Target,
        async_channel::{Receiver, Sender},
        async_lock::Mutex,
        async_trait::async_trait,
        ffx_log_test_utils::{setup_fake_archive_iterator, FakeArchiveIteratorResponse},
        ffx_log_utils::symbolizer::FakeSymbolizerForTest,
        fidl::endpoints::RequestStream,
        fidl_fuchsia_developer_remotecontrol::{
            ArchiveIteratorError, IdentifyHostResponse, RemoteControlMarker, RemoteControlProxy,
            RemoteControlRequest, RemoteDiagnosticsBridgeRequest,
            RemoteDiagnosticsBridgeRequestStream, ServiceMatch,
        },
        fidl_fuchsia_diagnostics::DataType,
        fidl_fuchsia_overnet_protocol::NodeId,
        futures::TryStreamExt,
        hoist::Hoist,
        rcs::RcsConnection,
        std::rc::Rc,
        streamer::SessionStream,
    };

    const NODENAME: &str = "nodename-foo";
    const BOOT_TIME: i64 = 98765432123;
    const SYMBOLIZER_PREFIX: &str = "prefix: ";

    #[derive(Default)]
    struct FakeDiagnosticsStreamerInner {
        nodename: String,
        boot_time: i64,
        log_buf: Arc<Mutex<Vec<LogEntry>>>,
        most_recent_ts: i64,
        expect_setup: bool,
        cleaned_sessions: bool,
    }

    struct FakeDiagnosticsStreamer {
        // This struct has to be Send + Sync to be compatible with the Logger implementation.
        inner: Mutex<FakeDiagnosticsStreamerInner>,
    }

    impl FakeDiagnosticsStreamer {
        fn new(most_recent_ts: i64, log_buf: Arc<Mutex<Vec<LogEntry>>>) -> Self {
            Self {
                inner: Mutex::new(FakeDiagnosticsStreamerInner {
                    most_recent_ts,
                    log_buf,
                    ..FakeDiagnosticsStreamerInner::default()
                }),
            }
        }

        async fn expect_setup(&self, nodename: &str, boot_time: i64) {
            let mut inner = self.inner.lock().await;
            inner.expect_setup = true;
            inner.nodename = nodename.to_string();
            inner.boot_time = boot_time;
        }

        async fn assert_cleaned_sessions(&self) {
            let inner = self.inner.lock().await;
            assert!(inner.cleaned_sessions);
        }
    }

    #[async_trait(?Send)]
    impl GenericDiagnosticsStreamer for FakeDiagnosticsStreamer {
        async fn setup_stream(
            &self,
            target_nodename: String,
            target_boot_time_nanos: i64,
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
            self.assert_cleaned_sessions().await;
            let inner = self.inner.lock().await;
            inner.log_buf.lock().await.extend(entries);
            Ok(())
        }

        async fn read_most_recent_target_timestamp(&self) -> Result<Option<Timestamp>> {
            let inner = self.inner.lock().await;
            Ok(Some(Timestamp::from(inner.most_recent_ts)))
        }

        // TODO(jwing): add a new field for this
        async fn read_most_recent_entry_timestamp(&self) -> Result<Option<Timestamp>> {
            let inner = self.inner.lock().await;
            Ok(Some(Timestamp::from(inner.most_recent_ts)))
        }

        async fn clean_sessions_for_target(&self) -> Result<()> {
            let mut inner = self.inner.lock().await;
            inner.cleaned_sessions = true;
            Ok(())
        }

        async fn stream_entries(
            &self,
            _stream_mode: fidl_fuchsia_developer_ffx::StreamMode,
            _start_ts: Option<Timestamp>,
        ) -> Result<SessionStream> {
            panic!("unexpected stream_entries call");
        }
    }

    struct DisabledSymbolizer {}

    impl DisabledSymbolizer {
        fn new() -> Self {
            Self {}
        }
    }

    #[async_trait(?Send)]
    impl Symbolizer for DisabledSymbolizer {
        async fn start(
            &self,
            _rx: Receiver<String>,
            _tx: Sender<String>,
            _extra_args: Vec<String>,
        ) -> Result<()> {
            panic!("called start on a disabled symbolizer");
        }
    }

    impl DisabledSymbolizer {
        fn config() -> SymbolizerConfig {
            SymbolizerConfig::new_with_config(Arc::new(DisabledSymbolizer::new()), false, vec![])
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
                "mismatched data. \ngot: {:?}\n\nexpected: {:?}",
                got.data, expected.data
            );
            assert_eq!(
                got.version, expected.version,
                "mismatched version. got: {:?}\n\nexpected: {:?}",
                got.version, expected.version
            );
        }
    }

    fn setup_fake_archive_accessor(
        chan: fidl::Channel,
        responses: Arc<Vec<FakeArchiveIteratorResponse>>,
        legacy_format: bool,
    ) -> Result<()> {
        let mut stream = RemoteDiagnosticsBridgeRequestStream::from_channel(
            fidl::AsyncChannel::from_channel(chan)?,
        );
        fuchsia_async::Task::local(async move {
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
                            fidl_fuchsia_diagnostics::StreamMode::SnapshotThenSubscribe
                        );
                        setup_fake_archive_iterator(iterator, responses.clone(), legacy_format)
                            .unwrap();
                        responder.send(&mut Ok(())).unwrap();
                    }
                    _ => panic!("called unexpected diagnostic bridge method"),
                }
            }
        })
        .detach();
        Ok(())
    }

    fn setup_fake_remote_control_service(
        responses: Arc<Vec<FakeArchiveIteratorResponse>>,
        legacy_format: bool,
    ) -> RemoteControlProxy {
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<RemoteControlMarker>().unwrap();

        fuchsia_async::Task::local(async move {
            while let Ok(Some(req)) = stream.try_next().await {
                match req {
                    RemoteControlRequest::Connect { selector: _, service_chan, responder } => {
                        setup_fake_archive_accessor(service_chan, responses.clone(), legacy_format)
                            .unwrap();
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
                                boot_timestamp_nanos: Some(BOOT_TIME.try_into().unwrap()),
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
            timestamp: Timestamp::from(0),
            version: 1,
        }
    }

    fn target_disconnected_entry() -> LogEntry {
        LogEntry {
            data: LogData::FfxEvent(EventType::TargetDisconnected),
            timestamp: Timestamp::from(0),
            version: 1,
        }
    }

    fn malformed_log(s: &str) -> LogEntry {
        LogEntry {
            data: LogData::MalformedTargetLog(s.to_string()),
            timestamp: Timestamp::from(0),
            version: 1,
        }
    }

    fn valid_log(data: LogsData) -> LogEntry {
        LogEntry { data: LogData::TargetLog(data), timestamp: Timestamp::from(0), version: 1 }
    }

    fn symbolized_log(data: LogsData, msg: &str) -> LogEntry {
        LogEntry {
            data: LogData::SymbolizedTargetLog(data, msg.to_string()),
            timestamp: Timestamp::from(0),
            version: 1,
        }
    }

    fn target_log(timestamp: i64, msg: &str) -> LogsData {
        diagnostics_data::LogsDataBuilder::new(diagnostics_data::BuilderArgs {
            timestamp_nanos: timestamp.into(),
            component_url: Some(String::default()),
            moniker: String::default(),
            severity: diagnostics_data::Severity::Info,
        })
        .set_message(msg)
        .build()
    }

    async fn make_default_target_with_format(
        hoist: &Hoist,
        expected_logs: Vec<FakeArchiveIteratorResponse>,
        legacy_format: bool,
    ) -> Rc<Target> {
        let conn = RcsConnection::new_with_proxy(
            hoist,
            setup_fake_remote_control_service(Arc::new(expected_logs), legacy_format),
            &NodeId { id: 1234 },
        );
        Target::from_rcs_connection(conn).await.unwrap()
    }
    async fn make_default_target(
        hoist: &Hoist,
        expected_logs: Vec<FakeArchiveIteratorResponse>,
    ) -> Rc<Target> {
        make_default_target_with_format(hoist, expected_logs, false).await
    }

    async fn run_logger_to_completion(logger: Logger) {
        match logger.start().await {
            Err(e) => assert!(e.contains("PEER_CLOSED")),
            _ => panic!("should have exited with PEER_CLOSED, got ok"),
        };
    }

    async fn fake_symbolizer(extra_args: Vec<String>) -> SymbolizerConfig {
        SymbolizerConfig {
            symbolizer_args: extra_args.clone(),
            enabled: true,
            symbolizer: Some(Arc::new(FakeSymbolizerForTest::new(SYMBOLIZER_PREFIX, extra_args))),
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_disabled() -> Result<()> {
        let local_hoist = Hoist::new().unwrap();

        let target = make_default_target(&local_hoist, vec![]).await;
        let t = Rc::downgrade(&target);
        ();

        let streamer = FakeDiagnosticsStreamer::new(1, Arc::new(Mutex::new(vec![])));
        let logger =
            Logger::new_with_streamer_and_config(t, streamer, false, DisabledSymbolizer::config());
        logger.start().await.unwrap();
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_multiple_malformed_logs_in_series() -> Result<()> {
        let local_hoist = Hoist::new().unwrap();

        let target = make_default_target(
            &local_hoist,
            vec![
                FakeArchiveIteratorResponse::new_with_values(vec![
                    "log1".to_string(),
                    "log2".to_string(),
                ]),
                FakeArchiveIteratorResponse::new_with_values(vec![
                    "log3".to_string(),
                    "log4".to_string(),
                ]),
            ],
        )
        .await;
        let t = Rc::downgrade(&target);
        ();

        let log_buf = Arc::new(Mutex::new(vec![]));
        let streamer = FakeDiagnosticsStreamer::new(0, log_buf.clone());
        streamer.expect_setup(NODENAME, BOOT_TIME).await;
        let fs = fake_symbolizer(vec![]).await;
        let logger = Logger::new_with_streamer_and_config(t, streamer, true, fs);
        run_logger_to_completion(logger).await;

        verify_logged(
            log_buf.clone(),
            vec![
                logging_started_entry(),
                malformed_log("log1"),
                malformed_log("log2"),
                malformed_log("log3"),
                malformed_log("log4"),
                target_disconnected_entry(),
            ],
        )
        .await;
        Ok(())
    }

    async fn test_multiple_valid_logs_in_series_base_test(legacy_format: bool) -> Result<()> {
        let local_hoist = Hoist::new().unwrap();

        let log1 = target_log(1, "log1");
        let log2 = target_log(2, "log2");
        let log3 = target_log(3, "log3");
        let log4 = target_log(4, "log4");
        let target = make_default_target_with_format(
            &local_hoist,
            vec![
                FakeArchiveIteratorResponse::new_with_values(vec![
                    serde_json::to_string(&log1)?,
                    serde_json::to_string(&log2)?,
                ]),
                FakeArchiveIteratorResponse::new_with_values(vec![
                    serde_json::to_string(&log3)?,
                    serde_json::to_string(&log4)?,
                ]),
            ],
            legacy_format,
        )
        .await;
        let t = Rc::downgrade(&target);
        ();

        let log_buf = Arc::new(Mutex::new(vec![]));

        let streamer = FakeDiagnosticsStreamer::new(0, log_buf.clone());
        streamer.expect_setup(NODENAME, BOOT_TIME).await;
        let fs = fake_symbolizer(vec![]).await;
        let logger = Logger::new_with_streamer_and_config(t, streamer, true, fs);
        run_logger_to_completion(logger).await;

        verify_logged(
            log_buf.clone(),
            vec![
                logging_started_entry(),
                valid_log(log1),
                valid_log(log2),
                valid_log(log3),
                valid_log(log4),
                target_disconnected_entry(),
            ],
        )
        .await;

        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_multiple_valid_logs_in_series() -> Result<()> {
        test_multiple_valid_logs_in_series_base_test(false).await
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_multiple_valid_logs_in_series_legacy_format() -> Result<()> {
        test_multiple_valid_logs_in_series_base_test(true).await
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_skips_old_logs() -> Result<()> {
        let local_hoist = Hoist::new().unwrap();

        let log1 = target_log(1, "log1");
        let log2 = target_log(2, "log2");
        let target = make_default_target(
            &local_hoist,
            vec![
                FakeArchiveIteratorResponse::new_with_values(vec![serde_json::to_string(&log1)?]),
                FakeArchiveIteratorResponse::new_with_values(vec![serde_json::to_string(&log2)?]),
            ],
        )
        .await;
        let t = Rc::downgrade(&target);
        ();

        let log_buf = Arc::new(Mutex::new(vec![]));

        let streamer = FakeDiagnosticsStreamer::new(1, log_buf.clone());
        streamer.expect_setup(NODENAME, BOOT_TIME).await;
        let fs = fake_symbolizer(vec![]).await;
        let logger = Logger::new_with_streamer_and_config(t, streamer, true, fs);
        run_logger_to_completion(logger).await;

        verify_logged(
            log_buf.clone(),
            vec![logging_started_entry(), valid_log(log2), target_disconnected_entry()],
        )
        .await;
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_continues_after_error() -> Result<()> {
        let local_hoist = Hoist::new().unwrap();

        let log1 = target_log(1, "log1");
        let log2 = target_log(2, "log2");
        let target = make_default_target(
            &local_hoist,
            vec![
                FakeArchiveIteratorResponse::new_with_values(vec![serde_json::to_string(&log1)?]),
                FakeArchiveIteratorResponse::new_with_error(ArchiveIteratorError::DataReadFailed),
                FakeArchiveIteratorResponse::new_with_values(vec![serde_json::to_string(&log2)?]),
            ],
        )
        .await;
        let t = Rc::downgrade(&target);
        ();

        let log_buf = Arc::new(Mutex::new(vec![]));

        let streamer = FakeDiagnosticsStreamer::new(0, log_buf.clone());
        streamer.expect_setup(NODENAME, BOOT_TIME).await;
        let fs = fake_symbolizer(vec![]).await;
        let logger = Logger::new_with_streamer_and_config(t, streamer, true, fs);
        run_logger_to_completion(logger).await;

        verify_logged(
            log_buf.clone(),
            vec![
                logging_started_entry(),
                valid_log(log1),
                valid_log(log2),
                target_disconnected_entry(),
            ],
        )
        .await;
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_fidl_error_with_real_logs() -> Result<()> {
        let local_hoist = Hoist::new().unwrap();

        let log1 = target_log(1, "log1");
        let log2 = target_log(2, "log2");
        let target = make_default_target(
            &local_hoist,
            vec![
                FakeArchiveIteratorResponse::new_with_values(vec![serde_json::to_string(&log1)?]),
                FakeArchiveIteratorResponse::new_with_values(vec![serde_json::to_string(&log2)?]),
                FakeArchiveIteratorResponse::new_with_fidl_error(),
            ],
        )
        .await;
        let t = Rc::downgrade(&target);
        ();

        let log_buf = Arc::new(Mutex::new(vec![]));

        let streamer = FakeDiagnosticsStreamer::new(0, log_buf.clone());
        streamer.expect_setup(NODENAME, BOOT_TIME).await;
        let fs = fake_symbolizer(vec![]).await;
        let logger = Logger::new_with_streamer_and_config(t, streamer, true, fs);
        run_logger_to_completion(logger).await;

        verify_logged(
            log_buf.clone(),
            vec![
                logging_started_entry(),
                valid_log(log1),
                valid_log(log2),
                target_disconnected_entry(),
            ],
        )
        .await;
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_fidl_error_with_no_real_logs() -> Result<()> {
        let local_hoist = Hoist::new().unwrap();

        let target = make_default_target(
            &local_hoist,
            vec![FakeArchiveIteratorResponse::new_with_fidl_error()],
        )
        .await;
        let t = Rc::downgrade(&target);
        ();

        let log_buf = Arc::new(Mutex::new(vec![]));

        let streamer = FakeDiagnosticsStreamer::new(0, log_buf.clone());
        streamer.expect_setup(NODENAME, BOOT_TIME).await;
        let fs = fake_symbolizer(vec![]).await;
        let logger = Logger::new_with_streamer_and_config(t, streamer, true, fs);
        run_logger_to_completion(logger).await;

        verify_logged(log_buf.clone(), vec![logging_started_entry(), target_disconnected_entry()])
            .await;
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_symbolizable_log_disabled_symbolizer() -> Result<()> {
        let local_hoist = Hoist::new().unwrap();

        let log1 = target_log(1, "{{{reset}}}");
        let target = make_default_target(
            &local_hoist,
            vec![FakeArchiveIteratorResponse::new_with_values(vec![serde_json::to_string(&log1)?])],
        )
        .await;
        let t = Rc::downgrade(&target);
        ();

        let log_buf = Arc::new(Mutex::new(vec![]));

        let streamer = FakeDiagnosticsStreamer::new(0, log_buf.clone());
        streamer.expect_setup(NODENAME, BOOT_TIME).await;
        let logger =
            Logger::new_with_streamer_and_config(t, streamer, true, DisabledSymbolizer::config());
        run_logger_to_completion(logger).await;

        verify_logged(
            log_buf.clone(),
            vec![logging_started_entry(), valid_log(log1), target_disconnected_entry()],
        )
        .await;
        Ok(())
    }

    // NOTE: this test has strings that look like symbolizer data! If you use `fx test` to run this and it fails,
    // your output will be incredibly confusing and mostly useless. In that case, run the test binary directly
    // to see the real output.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_symbolized_logs() -> Result<()> {
        let local_hoist = Hoist::new().unwrap();

        let log1 = target_log(1, "{{{reset}}}");
        let log2 = target_log(2, "{{{mmap:something}}}");
        let log3 = target_log(3, "don't symbolize this");
        let log4 = target_log(4, "{{{bt:file}}}");
        let target = make_default_target(
            &local_hoist,
            vec![
                FakeArchiveIteratorResponse::new_with_values(vec![
                    serde_json::to_string(&log1)?,
                    serde_json::to_string(&log2)?,
                ]),
                FakeArchiveIteratorResponse::new_with_values(vec![
                    serde_json::to_string(&log3)?,
                    serde_json::to_string(&log4)?,
                ]),
            ],
        )
        .await;
        let t = Rc::downgrade(&target);
        ();

        let log_buf = Arc::new(Mutex::new(vec![]));

        let streamer = FakeDiagnosticsStreamer::new(0, log_buf.clone());
        streamer.expect_setup(NODENAME, BOOT_TIME).await;
        let fs = fake_symbolizer(vec!["--some-arg".to_string(), "some-value".to_string()]).await;
        let logger = Logger::new_with_streamer_and_config(t, streamer, true, fs);
        run_logger_to_completion(logger).await;

        verify_logged(
            log_buf.clone(),
            vec![
                logging_started_entry(),
                symbolized_log(log1, "prefix: {{{reset}}}"),
                symbolized_log(log2, "prefix: {{{mmap:something}}}"),
                valid_log(log3),
                symbolized_log(log4, "prefix: {{{bt:file}}}"),
                target_disconnected_entry(),
            ],
        )
        .await;

        Ok(())
    }
}
