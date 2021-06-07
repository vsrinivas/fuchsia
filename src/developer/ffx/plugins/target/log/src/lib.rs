// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Error, Result},
    async_trait::async_trait,
    blocking::Unblock,
    chrono::{DateTime, Local, TimeZone, Utc},
    diagnostics_data::{LogsData, Severity, Timestamp},
    errors::{ffx_bail, ffx_error},
    ffx_config::{get, get_sdk},
    ffx_core::ffx_plugin,
    ffx_log_args::{DumpCommand, LogCommand, LogSubCommand, TimeFormat, WatchCommand},
    ffx_log_data::{EventType, LogData, LogEntry},
    ffx_log_utils::{
        run_logging_pipeline, symbolizer::is_current_sdk_root_registered, OrderedBatchPipeline,
    },
    fidl::endpoints::{create_proxy, ServerEnd},
    fidl_fuchsia_developer_bridge::{
        DaemonDiagnosticsStreamParameters, DaemonProxy, DiagnosticsStreamError, StreamMode,
    },
    fidl_fuchsia_developer_remotecontrol::{
        ArchiveIteratorError, ArchiveIteratorMarker, RemoteControlProxy,
    },
    fidl_fuchsia_diagnostics::ComponentSelector,
    fuchsia_async::futures::{AsyncWrite, AsyncWriteExt},
    fuchsia_async::Timer,
    selectors::{match_moniker_against_component_selectors, parse_path_to_moniker},
    std::sync::Arc,
    std::{
        iter::Iterator,
        time::{Duration, SystemTime},
    },
    termion::{color, style},
};

type ArchiveIteratorResult = Result<LogEntry, ArchiveIteratorError>;
const PIPELINE_SIZE: usize = 20;
const COLOR_CONFIG_NAME: &str = "target_log.color";
const SYMBOLIZE_ENABLED_CONFIG: &str = "proactive_log.symbolize.enabled";
const NO_STREAM_ERROR: &str = "\
The proactive logger isn't connected to this target.

Verify that the target is up with `ffx target list` and retry \
in a few seconds.";
const NANOS_IN_SECOND: i64 = 1_000_000_000;
const TIMESTAMP_FORMAT: &str = "%Y-%m-%d %H:%M:%S.%3f";
const RETRY_TIMEOUT_MILLIS: u64 = 1000;

fn get_timestamp() -> Result<Timestamp> {
    Ok(Timestamp::from(
        SystemTime::now()
            .duration_since(SystemTime::UNIX_EPOCH)
            .context("system time before Unix epoch")?
            .as_nanos() as i64,
    ))
}

fn timestamp_to_partial_secs(ts: Timestamp) -> f64 {
    let u_ts: i64 = ts.into();
    u_ts as f64 / NANOS_IN_SECOND as f64
}

fn severity_to_color_str(s: Severity) -> String {
    match s {
        Severity::Error => color::Fg(color::Red).to_string(),
        Severity::Warn => color::Fg(color::Yellow).to_string(),
        _ => "".to_string(),
    }
}
fn format_ffx_event(msg: &str, timestamp: Option<Timestamp>) -> String {
    let ts: i64 = timestamp.unwrap_or_else(|| get_timestamp().unwrap()).into();
    let dt = Local
        .timestamp(ts / NANOS_IN_SECOND, (ts % NANOS_IN_SECOND) as u32)
        .format(TIMESTAMP_FORMAT)
        .to_string();
    format!("[{}][<ffx>]: {}", dt, msg)
}

struct LogFilterCriteria {
    monikers: Vec<Arc<ComponentSelector>>,
    exclude_monikers: Vec<Arc<ComponentSelector>>,
    min_severity: Severity,
    filters: Vec<String>,
    excludes: Vec<String>,
}

impl LogFilterCriteria {
    fn new(
        monikers: Vec<Arc<ComponentSelector>>,
        exclude_monikers: Vec<Arc<ComponentSelector>>,
        min_severity: Severity,
        filters: Vec<String>,
        excludes: Vec<String>,
    ) -> Self {
        Self { monikers, exclude_monikers, min_severity: min_severity, filters, excludes }
    }

    fn matches_filter(f: &str, log: &LogsData) -> bool {
        return log.msg().as_ref().unwrap_or(&"").contains(f)
            || log.metadata.component_url.contains(f)
            || log.moniker.contains(f);
    }

    fn matches(&self, entry: &LogEntry) -> bool {
        match entry {
            LogEntry { data: LogData::TargetLog(data), .. } => {
                if self.filters.len() > 0
                    && !self.filters.iter().any(|m| Self::matches_filter(m, &data))
                {
                    return false;
                }

                if self.excludes.iter().any(|m| Self::matches_filter(m, &data)) {
                    return false;
                }

                if data.metadata.severity < self.min_severity {
                    return false;
                }

                let parsed_moniker = match parse_path_to_moniker(&data.moniker) {
                    Ok(m) => m,
                    Err(e) => {
                        log::warn!(
                            "got a broken moniker '{}' in a log entry: {}",
                            &data.moniker,
                            e
                        );
                        return true;
                    }
                };

                let pos_match_results =
                    match_moniker_against_component_selectors(&parsed_moniker, &self.monikers);
                if let Err(ref e) = pos_match_results {
                    log::warn!("got a bad selector in log matcher: {}", e);
                } else if !self.monikers.is_empty() && pos_match_results.unwrap().is_empty() {
                    return false;
                }

                let neg_match_results = match_moniker_against_component_selectors(
                    &parsed_moniker,
                    &self.exclude_monikers,
                );
                if let Err(ref e) = neg_match_results {
                    log::warn!("got a bad selector in log matcher: {}", e);
                } else if !(self.exclude_monikers.is_empty()
                    || neg_match_results.unwrap().is_empty())
                {
                    return false;
                }
                true
            }
            _ => true,
        }
    }
}

impl From<&LogCommand> for LogFilterCriteria {
    fn from(cmd: &LogCommand) -> Self {
        LogFilterCriteria::new(
            cmd.moniker.clone().into_iter().map(|m| Arc::new(m)).collect(),
            cmd.exclude_moniker.clone().into_iter().map(|m| Arc::new(m)).collect(),
            cmd.min_severity,
            cmd.filter.clone(),
            cmd.exclude.clone(),
        )
    }
}

#[async_trait(?Send)]
pub trait LogFormatter {
    async fn push_log(&mut self, log_entry: ArchiveIteratorResult) -> Result<()>;
    fn set_boot_timestamp(&mut self, boot_ts_nanos: i64);
}

struct DefaultLogFormatter<'a> {
    writer: Box<dyn AsyncWrite + Unpin + 'a>,
    has_previous_log: bool,
    filters: LogFilterCriteria,
    color: bool,
    time_format: TimeFormat,
    boot_ts_nanos: Option<i64>,
}

#[async_trait(?Send)]
impl<'a> LogFormatter for DefaultLogFormatter<'_> {
    fn set_boot_timestamp(&mut self, boot_ts_nanos: i64) {
        self.boot_ts_nanos.replace(boot_ts_nanos);
    }
    async fn push_log(&mut self, log_entry_result: ArchiveIteratorResult) -> Result<()> {
        let mut s = match log_entry_result {
            Ok(log_entry) => {
                if !self.filters.matches(&log_entry) {
                    return Ok(());
                }

                match log_entry {
                    LogEntry { data: LogData::TargetLog(data), .. } => {
                        self.format_target_log_data(data, None)
                    }
                    LogEntry { data: LogData::SymbolizedTargetLog(data, symbolized), .. } => {
                        if symbolized.is_empty() {
                            return Ok(());
                        }

                        self.format_target_log_data(data, Some(symbolized))
                    }
                    LogEntry { data: LogData::MalformedTargetLog(raw), .. } => {
                        format!("malformed target log: {}", raw)
                    }
                    LogEntry { data: LogData::FfxEvent(etype), timestamp, .. } => match etype {
                        EventType::LoggingStarted => {
                            let mut s = String::from("logger started.");
                            if self.has_previous_log {
                                s.push_str(" Logs before this may have been dropped if they were not cached on the target. There may be a brief delay while we catch up...");
                            }
                            format_ffx_event(&s, Some(timestamp))
                        }
                        EventType::TargetDisconnected => format_ffx_event(
                            "Logger lost connection to target. Retrying...",
                            Some(timestamp),
                        ),
                    },
                }
            }
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
    fn new(
        filters: LogFilterCriteria,
        color: bool,
        time_format: TimeFormat,
        writer: impl AsyncWrite + Unpin + 'a,
    ) -> Self {
        Self {
            filters,
            color,
            writer: Box::new(writer),
            has_previous_log: false,
            time_format,
            boot_ts_nanos: None,
        }
    }

    fn format_target_timestamp(&self, ts: Timestamp) -> String {
        let mut abs_ts = 0;
        let time_format = match self.boot_ts_nanos {
            Some(boot_ts) => {
                abs_ts = boot_ts + *ts;
                self.time_format.clone()
            }
            None => TimeFormat::Monotonic,
        };

        match time_format {
            TimeFormat::Monotonic => format!("{:05.3}", timestamp_to_partial_secs(ts)),
            TimeFormat::Local => Local
                .timestamp(abs_ts / NANOS_IN_SECOND, (abs_ts % NANOS_IN_SECOND) as u32)
                .format(TIMESTAMP_FORMAT)
                .to_string(),
            TimeFormat::Utc => Utc
                .timestamp(abs_ts / NANOS_IN_SECOND, (abs_ts % NANOS_IN_SECOND) as u32)
                .format(TIMESTAMP_FORMAT)
                .to_string(),
        }
    }

    fn format_target_log_data(&self, data: LogsData, symbolized_msg: Option<String>) -> String {
        let ts = self.format_target_timestamp(data.metadata.timestamp);
        let color_str = if self.color {
            severity_to_color_str(data.metadata.severity)
        } else {
            String::default()
        };
        let msg = symbolized_msg.unwrap_or(data.msg().unwrap_or("<missing message>").to_string());

        let severity_str = &format!("{}", data.metadata.severity)[..1];
        msg.lines()
            .map(|l| {
                format!(
                    "[{}][{}][{}{}{}] {}{}{}",
                    ts,
                    data.moniker,
                    color_str,
                    severity_str,
                    style::Reset,
                    color_str,
                    l,
                    style::Reset
                )
            })
            .collect::<Vec<_>>()
            .join("\n")
    }
}

fn should_color(config_color: bool, cmd_color: Option<bool>) -> bool {
    if let Some(c) = cmd_color {
        return c;
    }

    return config_color;
}

fn calculate_monotonic_time(
    boot_ts: Duration,
    dt: Option<DateTime<Local>>,
    monotonic: Option<Duration>,
) -> Option<Duration> {
    match (dt, monotonic) {
        (Some(dt), None) => Duration::from_secs(dt.timestamp() as u64).checked_sub(boot_ts),
        (None, Some(m)) => Some(m),
        (None, None) => None,
        _ => panic!("can only accept either a datetime bound or a monotonic one."),
    }
}

async fn print_symbolizer_warning(err: Error, should_sleep: bool) {
    println!(
        "Warning: attempting to get the symbolizer binary failed.
This likely means that your logs will not be symbolized."
    );
    println!("\nThe failure was: {}", err);

    let sdk_type: Result<String, _> = get("sdk.type").await;
    if sdk_type.is_err() || sdk_type.unwrap() == "" {
        println!("If you are working in-tree, ensure that the sdk.type config setting is set accordingly:");
        println!("  ffx config set sdk.type in-tree");
    }

    println!(
        "\nYou can silence this warning by passing `--ignore-symbolizer-failure`: \
                            \n  `ffx target log --ignore-symbolizer-failure <subcommand>`",
    );

    if should_sleep {
        Timer::new(Duration::from_secs(10)).await;
    }
}

#[ffx_plugin("proactive_log.enabled")]
pub async fn log(
    daemon_proxy: DaemonProxy,
    rcs_proxy: RemoteControlProxy,
    cmd: LogCommand,
) -> Result<()> {
    let config_color: bool = get(COLOR_CONFIG_NAME).await?;

    let mut stdout = Unblock::new(std::io::stdout());
    let mut formatter = DefaultLogFormatter::new(
        LogFilterCriteria::from(&cmd),
        should_color(config_color, cmd.color),
        cmd.time.clone(),
        &mut stdout,
    );

    if get(SYMBOLIZE_ENABLED_CONFIG).await.unwrap_or(true) {
        match get_sdk().await {
            Ok(s) => match s.get_host_tool("symbolizer") {
                Ok(_) => {
                    let registered_result = is_current_sdk_root_registered().await;
                    if let Ok(false) = registered_result {
                        let sdk_type: Result<String, _> = get("sdk.type").await;
                        println!(
                            "It looks like there is no symbol index for your sdk root registered."
                        );
                        println!("If you want symbolization to work correctly, run the following from your checkout:");

                        if sdk_type.is_ok() && sdk_type.unwrap() == "in-tree".to_string() {
                            println!("  fx symbol-index register");
                        } else {
                            let symbol_index = s.get_host_tool("symbol-index");
                            match symbol_index {
                                Ok(path) => {
                                    println!("  {} register", path.to_string_lossy().to_string());
                                }
                                Err(e) => {
                                    println!("We could not find the path to the symbol-index host-tool: {}", e);
                                }
                            }
                        }

                        println!("\nSilence this message in the future by disabling the `proactive_log.symbolize.enabled` config setting, \
                                    or passing the '--ignore-symbolizer-failure' flag to this command.");
                    } else if registered_result.is_err() {
                        log::warn!(
                            "checking the registration of the SDK root failed: {}",
                            registered_result.as_ref().unwrap_err()
                        )
                    }

                    if !registered_result.unwrap_or(false) && !cmd.ignore_symbolizer_failure {
                        Timer::new(Duration::from_secs(10)).await;
                    }
                }
                Err(e) => {
                    print_symbolizer_warning(e, !cmd.ignore_symbolizer_failure).await;
                }
            },
            Err(e) => print_symbolizer_warning(e, !cmd.ignore_symbolizer_failure).await,
        };
    }

    log_cmd(daemon_proxy, rcs_proxy, &mut formatter, cmd).await
}

async fn setup_daemon_stream(
    daemon_proxy: &DaemonProxy,
    target_str: &str,
    server: ServerEnd<ArchiveIteratorMarker>,
    stream_mode: StreamMode,
    from_bound: Option<Duration>,
) -> Result<Result<(), DiagnosticsStreamError>> {
    let params = DaemonDiagnosticsStreamParameters {
        stream_mode: Some(stream_mode),
        min_target_timestamp_nanos: from_bound.map(|f| f.as_nanos() as u64),
        ..DaemonDiagnosticsStreamParameters::EMPTY
    };
    daemon_proxy
        .stream_diagnostics(Some(&target_str), params, server)
        .await
        .context("connecting to daemon")
}

pub async fn log_cmd(
    daemon_proxy: DaemonProxy,
    rcs: RemoteControlProxy,
    log_formatter: &mut impl LogFormatter,
    cmd: LogCommand,
) -> Result<()> {
    let stream_mode = match cmd.cmd {
        LogSubCommand::Watch(WatchCommand { dump }) => {
            if dump {
                StreamMode::SnapshotRecentThenSubscribe
            } else {
                StreamMode::Subscribe
            }
        }
        LogSubCommand::Dump(DumpCommand { .. }) => StreamMode::SnapshotAll,
    };
    let target_info_result = rcs.identify_host().await?;
    let target_info =
        target_info_result.map_err(|e| anyhow!("failed to get target info: {:?}", e))?;
    let target_boot_time_nanos = match target_info.boot_timestamp_nanos {
        Some(t) => {
            log_formatter.set_boot_timestamp(t as i64);
            Duration::from_nanos(t)
        }
        None => Duration::new(0, 0),
    };

    let nodename = target_info.nodename.context("missing nodename")?;
    let (from_bound, to_bound) = match cmd.cmd {
        LogSubCommand::Dump(DumpCommand { from, from_monotonic, to, to_monotonic }) => {
            if !(from.is_none() || from_monotonic.is_none()) {
                ffx_bail!("only one of --from or --from-monotonic may be provided at once.");
            }
            if !(to.is_none() || to_monotonic.is_none()) {
                ffx_bail!("only one of --to or --to-monotonic may be provided at once.");
            }

            if target_info.boot_timestamp_nanos.is_none() && (from.is_some() || to.is_some()) {
                println!(
                    "{}target timestamp not available - from/to filters will not be applied.{}",
                    color::Fg(color::Red),
                    style::Reset
                );
                (None, None)
            } else {
                (
                    calculate_monotonic_time(target_boot_time_nanos, from, from_monotonic),
                    calculate_monotonic_time(target_boot_time_nanos, to, to_monotonic),
                )
            }
        }
        _ => (None, None),
    };

    let (mut proxy, server) =
        create_proxy::<ArchiveIteratorMarker>().context("failed to create endpoints")?;
    setup_daemon_stream(&daemon_proxy, &nodename, server, stream_mode, from_bound).await?.map_err(
        |e| match e {
            DiagnosticsStreamError::NoStreamForTarget => {
                anyhow!(ffx_error!("{}", NO_STREAM_ERROR))
            }
            _ => anyhow!("failure setting up diagnostics stream: {:?}", e),
        },
    )?;

    let mut requests = OrderedBatchPipeline::new(PIPELINE_SIZE);
    'request_loop: loop {
        let (get_next_results, terminal_err) = run_logging_pipeline(&mut requests, &proxy).await;

        for result in get_next_results.into_iter() {
            if let Err(e) = result {
                log::warn!("got an error from the daemon {:?}", e);
                log_formatter.push_log(Err(e)).await?;
                continue;
            }

            let entries = result.unwrap().into_iter().filter_map(|e| e.data);

            for entry in entries {
                let parsed: LogEntry = serde_json::from_str(&entry)?;

                match (&parsed.data, to_bound) {
                    (LogData::TargetLog(log_data), Some(t)) => {
                        let ts: i64 = log_data.metadata.timestamp.into();
                        if ts as u128 > t.as_nanos() {
                            return Ok(());
                        }
                    }
                    (LogData::FfxEvent(EventType::TargetDisconnected), _) => {
                        log_formatter.push_log(Ok(parsed)).await?;
                        loop {
                            let (new_proxy, server) = create_proxy::<ArchiveIteratorMarker>()
                                .context("failed to create endpoints")?;
                            match setup_daemon_stream(
                                &daemon_proxy,
                                &nodename,
                                server,
                                stream_mode,
                                from_bound,
                            )
                            .await?
                            {
                                Ok(()) => {
                                    proxy = new_proxy;
                                    continue 'request_loop;
                                }
                                Err(e) => {
                                    match e {
                                        DiagnosticsStreamError::NoMatchingTargets => {
                                            println!(
                                                "{}",
                                                format_ffx_event(
                                                    &format!("{} isn't up. Retrying...", &nodename),
                                                    None
                                                )
                                            );
                                        }
                                        DiagnosticsStreamError::NoStreamForTarget => {
                                            println!("{}" , format_ffx_event(&format!("{} is up, but the logger hasn't started yet. Retrying...", &nodename), None));
                                        }
                                        _ => {
                                            println!(
                                                "{}",
                                                format_ffx_event(
                                                    &format!(
                                                        "Retry failed ({:?}). Trying again...",
                                                        e
                                                    ),
                                                    None
                                                )
                                            );
                                        }
                                    }
                                    Timer::new(Duration::from_millis(RETRY_TIMEOUT_MILLIS)).await;
                                    continue;
                                }
                            }
                        }
                    }
                    _ => {}
                }

                log_formatter.push_log(Ok(parsed)).await?;
            }
        }

        if let Some(err) = terminal_err {
            log::info!("log command got a terminal error: {}", err);
            return Ok(());
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use {
        super::*,
        diagnostics_data::Timestamp,
        errors::ResultExt as _,
        ffx_log_test_utils::{setup_fake_archive_iterator, FakeArchiveIteratorResponse},
        fidl_fuchsia_developer_bridge::DaemonRequest,
        fidl_fuchsia_developer_remotecontrol::{
            ArchiveIteratorError, IdentifyHostResponse, RemoteControlRequest,
        },
        selectors::parse_component_selector,
        std::sync::Arc,
    };

    const DEFAULT_TS_NANOS: u64 = 1615535969000000000;
    const BOOT_TS: u64 = 98765432000000000;
    const FAKE_START_TIMESTAMP: i64 = 1614669138;
    // FAKE_START_TIMESTAMP - BOOT_TS
    const START_TIMESTAMP_FOR_DAEMON: u64 = 1515903706000000000;
    const NODENAME: &str = "some-nodename";

    fn default_ts() -> Duration {
        Duration::from_nanos(DEFAULT_TS_NANOS)
    }
    struct FakeLogFormatter {
        pushed_logs: Vec<ArchiveIteratorResult>,
    }

    #[async_trait(?Send)]
    impl LogFormatter for FakeLogFormatter {
        async fn push_log(&mut self, log_entry: ArchiveIteratorResult) -> Result<()> {
            self.pushed_logs.push(log_entry);
            Ok(())
        }

        fn set_boot_timestamp(&mut self, boot_ts_nanos: i64) {
            assert_eq!(boot_ts_nanos, BOOT_TS as i64)
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

    fn setup_fake_rcs() -> RemoteControlProxy {
        setup_fake_rcs_proxy(move |req| match req {
            RemoteControlRequest::IdentifyHost { responder } => {
                responder
                    .send(&mut Ok(IdentifyHostResponse {
                        boot_timestamp_nanos: Some(BOOT_TS),
                        nodename: Some(NODENAME.to_string()),
                        ..IdentifyHostResponse::EMPTY
                    }))
                    .context("sending identify host response")
                    .unwrap();
            }
            _ => assert!(false),
        })
    }

    fn setup_fake_daemon_server(
        expected_parameters: DaemonDiagnosticsStreamParameters,
        expected_responses: Arc<Vec<FakeArchiveIteratorResponse>>,
    ) -> DaemonProxy {
        setup_fake_daemon_proxy(move |req| match req {
            DaemonRequest::StreamDiagnostics { target: _, parameters, iterator, responder } => {
                assert_eq!(parameters, expected_parameters);
                setup_fake_archive_iterator(iterator, expected_responses.clone()).unwrap();
                responder.send(&mut Ok(())).context("error sending response").expect("should send")
            }
            _ => assert!(false),
        })
    }

    fn make_log_entry(log_data: LogData) -> LogEntry {
        LogEntry {
            version: 1,
            timestamp: Timestamp::from(default_ts().as_nanos() as i64),
            data: log_data,
        }
    }

    fn empty_log_command(sub_cmd: LogSubCommand) -> LogCommand {
        LogCommand {
            cmd: sub_cmd,
            ignore_symbolizer_failure: false,
            filter: vec![],
            exclude: vec![],
            time: TimeFormat::Monotonic,
            color: None,
            moniker: vec![],
            exclude_moniker: vec![],
            min_severity: Severity::Info,
        }
    }

    fn empty_dump_subcommand() -> LogSubCommand {
        LogSubCommand::Dump(DumpCommand {
            from: None,
            from_monotonic: None,
            to: None,
            to_monotonic: None,
        })
    }

    fn empty_dump_command() -> LogCommand {
        empty_log_command(empty_dump_subcommand())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dump_empty() {
        let mut formatter = FakeLogFormatter::new();
        let cmd = empty_dump_command();
        let params = DaemonDiagnosticsStreamParameters {
            stream_mode: Some(StreamMode::SnapshotAll),
            ..DaemonDiagnosticsStreamParameters::EMPTY
        };
        let expected_responses = vec![];

        log_cmd(
            setup_fake_daemon_server(params, Arc::new(expected_responses)),
            setup_fake_rcs(),
            &mut formatter,
            cmd,
        )
        .await
        .unwrap();

        formatter.assert_same_logs(vec![])
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_watch() {
        let mut formatter = FakeLogFormatter::new();
        let cmd = empty_log_command(LogSubCommand::Watch(WatchCommand { dump: true }));
        let params = DaemonDiagnosticsStreamParameters {
            stream_mode: Some(StreamMode::SnapshotRecentThenSubscribe),
            ..DaemonDiagnosticsStreamParameters::EMPTY
        };
        let log1 = make_log_entry(LogData::FfxEvent(EventType::LoggingStarted));
        let log2 = make_log_entry(LogData::MalformedTargetLog("text".to_string()));
        let log3 = make_log_entry(LogData::MalformedTargetLog("text2".to_string()));

        let expected_responses = vec![
            FakeArchiveIteratorResponse::new_with_values(vec![
                serde_json::to_string(&log1).unwrap(),
                serde_json::to_string(&log2).unwrap(),
            ]),
            FakeArchiveIteratorResponse::new_with_values(vec![
                serde_json::to_string(&log3).unwrap()
            ]),
        ];

        log_cmd(
            setup_fake_daemon_server(params, Arc::new(expected_responses)),
            setup_fake_rcs(),
            &mut formatter,
            cmd,
        )
        .await
        .unwrap();

        formatter.assert_same_logs(vec![Ok(log1), Ok(log2), Ok(log3)])
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_watch_no_dump_with_error() {
        let mut formatter = FakeLogFormatter::new();
        let cmd = empty_log_command(LogSubCommand::Watch(WatchCommand { dump: false }));
        let params = DaemonDiagnosticsStreamParameters {
            stream_mode: Some(StreamMode::Subscribe),
            ..DaemonDiagnosticsStreamParameters::EMPTY
        };
        let log1 = make_log_entry(LogData::FfxEvent(EventType::LoggingStarted));
        let log2 = make_log_entry(LogData::MalformedTargetLog("text".to_string()));
        let log3 = make_log_entry(LogData::MalformedTargetLog("text2".to_string()));

        let expected_responses = vec![
            FakeArchiveIteratorResponse::new_with_values(vec![
                serde_json::to_string(&log1).unwrap(),
                serde_json::to_string(&log2).unwrap(),
            ]),
            FakeArchiveIteratorResponse::new_with_error(ArchiveIteratorError::GenericError),
            FakeArchiveIteratorResponse::new_with_values(vec![
                serde_json::to_string(&log3).unwrap()
            ]),
        ];

        log_cmd(
            setup_fake_daemon_server(params, Arc::new(expected_responses)),
            setup_fake_rcs(),
            &mut formatter,
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

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dump_with_to_timestamp() {
        let mut formatter = FakeLogFormatter::new();
        let cmd = empty_log_command(LogSubCommand::Dump(DumpCommand {
            from: None,
            from_monotonic: None,
            to_monotonic: Some(default_ts()),
            to: None,
        }));
        let params = DaemonDiagnosticsStreamParameters {
            stream_mode: Some(StreamMode::SnapshotAll),
            ..DaemonDiagnosticsStreamParameters::EMPTY
        };
        let log1 = make_log_entry(
            diagnostics_data::LogsDataBuilder::new(diagnostics_data::BuilderArgs {
                timestamp_nanos: Timestamp::from(
                    (default_ts() - Duration::from_nanos(1)).as_nanos() as i64,
                ),
                component_url: String::default(),
                moniker: String::default(),
                severity: diagnostics_data::Severity::Info,
                size_bytes: 1,
            })
            .set_message("log1")
            .build()
            .into(),
        );
        let log2 = make_log_entry(
            diagnostics_data::LogsDataBuilder::new(diagnostics_data::BuilderArgs {
                timestamp_nanos: Timestamp::from(default_ts().as_nanos() as i64),
                component_url: String::default(),
                moniker: String::default(),
                severity: diagnostics_data::Severity::Info,
                size_bytes: 1,
            })
            .set_message("log2")
            .build()
            .into(),
        );
        let log3 = make_log_entry(
            diagnostics_data::LogsDataBuilder::new(diagnostics_data::BuilderArgs {
                timestamp_nanos: Timestamp::from(
                    (default_ts() + Duration::from_nanos(1)).as_nanos() as i64,
                ),
                component_url: String::default(),
                moniker: String::default(),
                severity: diagnostics_data::Severity::Info,
                size_bytes: 1,
            })
            .set_message("log3")
            .build()
            .into(),
        );

        let expected_responses = vec![FakeArchiveIteratorResponse::new_with_values(vec![
            serde_json::to_string(&log1).unwrap(),
            serde_json::to_string(&log2).unwrap(),
            serde_json::to_string(&log3).unwrap(),
        ])];

        assert!(log_cmd(
            setup_fake_daemon_server(params, Arc::new(expected_responses)),
            setup_fake_rcs(),
            &mut formatter,
            cmd,
        )
        .await
        .is_ok());

        formatter.assert_same_logs(vec![Ok(log1), Ok(log2)])
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_criteria_moniker_message_and_severity_matches() {
        let cmd = LogCommand {
            filter: vec!["included".to_string()],
            exclude: vec!["not this".to_string()],
            min_severity: Severity::Error,
            ..empty_dump_command()
        };
        let criteria = LogFilterCriteria::from(&cmd);

        assert!(criteria.matches(&make_log_entry(
            diagnostics_data::LogsDataBuilder::new(diagnostics_data::BuilderArgs {
                timestamp_nanos: 0.into(),
                component_url: String::default(),
                moniker: "included/moniker".to_string(),
                severity: diagnostics_data::Severity::Error,
                size_bytes: 1,
            })
            .set_message("included message")
            .build()
            .into()
        )));
        assert!(criteria.matches(&make_log_entry(
            diagnostics_data::LogsDataBuilder::new(diagnostics_data::BuilderArgs {
                timestamp_nanos: 0.into(),
                component_url: String::default(),
                moniker: "included/moniker".to_string(),
                severity: diagnostics_data::Severity::Fatal,
                size_bytes: 1,
            })
            .set_message("included message")
            .build()
            .into()
        )));
        assert!(!criteria.matches(&make_log_entry(
            diagnostics_data::LogsDataBuilder::new(diagnostics_data::BuilderArgs {
                timestamp_nanos: 0.into(),
                component_url: String::default(),
                moniker: "not/this/moniker".to_string(),
                severity: diagnostics_data::Severity::Error,
                size_bytes: 1,
            })
            .set_message("different message")
            .build()
            .into()
        )));
        assert!(!criteria.matches(&make_log_entry(
            diagnostics_data::LogsDataBuilder::new(diagnostics_data::BuilderArgs {
                timestamp_nanos: 0.into(),
                component_url: String::default(),
                moniker: "included/moniker".to_string(),
                severity: diagnostics_data::Severity::Warn,
                size_bytes: 1,
            })
            .set_message("included message")
            .build()
            .into()
        )));
        assert!(!criteria.matches(&make_log_entry(
            diagnostics_data::LogsDataBuilder::new(diagnostics_data::BuilderArgs {
                timestamp_nanos: 0.into(),
                component_url: String::default(),
                moniker: "other/moniker".to_string(),
                severity: diagnostics_data::Severity::Error,
                size_bytes: 1,
            })
            .set_message("not this message")
            .build()
            .into()
        )));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_empty_criteria() {
        let cmd = empty_dump_command();
        let criteria = LogFilterCriteria::from(&cmd);

        assert!(criteria.matches(&make_log_entry(
            diagnostics_data::LogsDataBuilder::new(diagnostics_data::BuilderArgs {
                timestamp_nanos: 0.into(),
                component_url: String::default(),
                moniker: "included/moniker".to_string(),
                severity: diagnostics_data::Severity::Error,
                size_bytes: 1,
            })
            .set_message("included message")
            .build()
            .into()
        )));
        assert!(criteria.matches(&make_log_entry(
            diagnostics_data::LogsDataBuilder::new(diagnostics_data::BuilderArgs {
                timestamp_nanos: 0.into(),
                component_url: String::default(),
                moniker: "included/moniker".to_string(),
                severity: diagnostics_data::Severity::Info,
                size_bytes: 1,
            })
            .set_message("different message")
            .build()
            .into()
        )));
        assert!(!criteria.matches(&make_log_entry(
            diagnostics_data::LogsDataBuilder::new(diagnostics_data::BuilderArgs {
                timestamp_nanos: 0.into(),
                component_url: String::default(),
                moniker: "other/moniker".to_string(),
                severity: diagnostics_data::Severity::Debug,
                size_bytes: 1,
            })
            .set_message("included message")
            .build()
            .into()
        )));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_criteria_multiple_moniker_matches() {
        let cmd = LogCommand {
            moniker: vec![
                parse_component_selector(&"included/*".to_string()).unwrap(),
                parse_component_selector(&"als*/moniker".to_string()).unwrap(),
            ],
            ..empty_dump_command()
        };
        let criteria = LogFilterCriteria::from(&cmd);

        assert!(criteria.matches(&make_log_entry(
            diagnostics_data::LogsDataBuilder::new(diagnostics_data::BuilderArgs {
                timestamp_nanos: 0.into(),
                component_url: String::default(),
                moniker: "included/moniker".to_string(),
                severity: diagnostics_data::Severity::Error,
                size_bytes: 1,
            })
            .set_message("included message")
            .build()
            .into()
        )));
        assert!(criteria.matches(&make_log_entry(
            diagnostics_data::LogsDataBuilder::new(diagnostics_data::BuilderArgs {
                timestamp_nanos: 0.into(),
                component_url: String::default(),
                moniker: "also/moniker".to_string(),
                severity: diagnostics_data::Severity::Error,
                size_bytes: 1,
            })
            .set_message("different message")
            .build()
            .into()
        )));
        assert!(!criteria.matches(&make_log_entry(
            diagnostics_data::LogsDataBuilder::new(diagnostics_data::BuilderArgs {
                timestamp_nanos: 0.into(),
                component_url: String::default(),
                moniker: "other/moniker".to_string(),
                severity: diagnostics_data::Severity::Error,
                size_bytes: 1,
            })
            .set_message("included message")
            .build()
            .into()
        )));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_criteria_multiple_matches() {
        let cmd = LogCommand {
            filter: vec!["included".to_string(), "also".to_string()],
            ..empty_dump_command()
        };
        let criteria = LogFilterCriteria::from(&cmd);

        assert!(criteria.matches(&make_log_entry(
            diagnostics_data::LogsDataBuilder::new(diagnostics_data::BuilderArgs {
                timestamp_nanos: 0.into(),
                component_url: String::default(),
                moniker: "moniker".to_string(),
                severity: diagnostics_data::Severity::Error,
                size_bytes: 1,
            })
            .set_message("included message")
            .build()
            .into()
        )));
        assert!(criteria.matches(&make_log_entry(
            diagnostics_data::LogsDataBuilder::new(diagnostics_data::BuilderArgs {
                timestamp_nanos: 0.into(),
                component_url: String::default(),
                moniker: "moniker".to_string(),
                severity: diagnostics_data::Severity::Info,
                size_bytes: 1,
            })
            .set_message("also message")
            .build()
            .into()
        )));
        assert!(criteria.matches(&make_log_entry(
            diagnostics_data::LogsDataBuilder::new(diagnostics_data::BuilderArgs {
                timestamp_nanos: 0.into(),
                component_url: String::default(),
                moniker: "included/moniker".to_string(),
                severity: diagnostics_data::Severity::Info,
                size_bytes: 1,
            })
            .set_message("not in there message")
            .build()
            .into()
        )));
        assert!(!criteria.matches(&make_log_entry(
            diagnostics_data::LogsDataBuilder::new(diagnostics_data::BuilderArgs {
                timestamp_nanos: 0.into(),
                component_url: String::default(),
                moniker: "moniker".to_string(),
                severity: diagnostics_data::Severity::Error,
                size_bytes: 1,
            })
            .set_message("different message")
            .build()
            .into()
        )));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_criteria_multiple_monikers_excluded() {
        let cmd = LogCommand {
            exclude_moniker: vec![
                parse_component_selector(&"included/*".to_string()).unwrap(),
                parse_component_selector(&"als*/moniker".to_string()).unwrap(),
            ],
            ..empty_dump_command()
        };
        let criteria = LogFilterCriteria::from(&cmd);

        assert!(!criteria.matches(&make_log_entry(
            diagnostics_data::LogsDataBuilder::new(diagnostics_data::BuilderArgs {
                timestamp_nanos: 0.into(),
                component_url: String::default(),
                moniker: "included/moniker.cmx:12345".to_string(),
                severity: diagnostics_data::Severity::Error,
                size_bytes: 1,
            })
            .set_message("included message")
            .build()
            .into()
        )));
        assert!(!criteria.matches(&make_log_entry(
            diagnostics_data::LogsDataBuilder::new(diagnostics_data::BuilderArgs {
                timestamp_nanos: 0.into(),
                component_url: String::default(),
                moniker: "also/moniker".to_string(),
                severity: diagnostics_data::Severity::Error,
                size_bytes: 1,
            })
            .set_message("different message")
            .build()
            .into()
        )));
        assert!(criteria.matches(&make_log_entry(
            diagnostics_data::LogsDataBuilder::new(diagnostics_data::BuilderArgs {
                timestamp_nanos: 0.into(),
                component_url: String::default(),
                moniker: "other/moniker".to_string(),
                severity: diagnostics_data::Severity::Error,
                size_bytes: 1,
            })
            .set_message("included message")
            .build()
            .into()
        )));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_criteria_multiple_excludes() {
        let cmd = LogCommand {
            exclude: vec![".cmx".to_string(), "also".to_string()],
            ..empty_dump_command()
        };
        let criteria = LogFilterCriteria::from(&cmd);

        assert!(!criteria.matches(&make_log_entry(
            diagnostics_data::LogsDataBuilder::new(diagnostics_data::BuilderArgs {
                timestamp_nanos: 0.into(),
                component_url: String::default(),
                moniker: "included/moniker.cmx:12345".to_string(),
                severity: diagnostics_data::Severity::Error,
                size_bytes: 1,
            })
            .set_message("included message")
            .build()
            .into()
        )));
        assert!(!criteria.matches(&make_log_entry(
            diagnostics_data::LogsDataBuilder::new(diagnostics_data::BuilderArgs {
                timestamp_nanos: 0.into(),
                component_url: String::default(),
                moniker: "also/moniker".to_string(),
                severity: diagnostics_data::Severity::Error,
                size_bytes: 1,
            })
            .set_message("different message")
            .build()
            .into()
        )));
        assert!(criteria.matches(&make_log_entry(
            diagnostics_data::LogsDataBuilder::new(diagnostics_data::BuilderArgs {
                timestamp_nanos: 0.into(),
                component_url: String::default(),
                moniker: "other/moniker".to_string(),
                severity: diagnostics_data::Severity::Error,
                size_bytes: 1,
            })
            .set_message("included message")
            .build()
            .into()
        )));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_criteria_matches_component_url() {
        let cmd = LogCommand {
            filter: vec!["fuchsia.com".to_string()],
            exclude: vec!["not-this-component.cmx".to_string()],
            ..empty_dump_command()
        };
        let criteria = LogFilterCriteria::from(&cmd);

        assert!(criteria.matches(&make_log_entry(
            diagnostics_data::LogsDataBuilder::new(diagnostics_data::BuilderArgs {
                timestamp_nanos: 0.into(),
                component_url: "fuchsia.com/this-component.cmx".to_string(),
                moniker: "any/moniker".to_string(),
                severity: diagnostics_data::Severity::Error,
                size_bytes: 1,
            })
            .set_message("message")
            .build()
            .into()
        )));
        assert!(!criteria.matches(&make_log_entry(
            diagnostics_data::LogsDataBuilder::new(diagnostics_data::BuilderArgs {
                timestamp_nanos: 0.into(),
                component_url: "fuchsia.com/not-this-component.cmx".to_string(),
                moniker: "any/moniker".to_string(),
                severity: diagnostics_data::Severity::Error,
                size_bytes: 1,
            })
            .set_message("message")
            .build()
            .into()
        )));
        assert!(!criteria.matches(&make_log_entry(
            diagnostics_data::LogsDataBuilder::new(diagnostics_data::BuilderArgs {
                timestamp_nanos: 0.into(),
                component_url: "some-other.com/component.cmx".to_string(),
                moniker: "any/moniker".to_string(),
                severity: diagnostics_data::Severity::Error,
                size_bytes: 1,
            })
            .set_message("message")
            .build()
            .into()
        )));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_from_time_passed_to_daemon() {
        let mut formatter = FakeLogFormatter::new();
        let cmd = LogCommand {
            cmd: LogSubCommand::Dump(DumpCommand {
                from: Some(Local.timestamp(FAKE_START_TIMESTAMP, 0)),
                from_monotonic: None,
                to: None,
                to_monotonic: None,
            }),
            ..empty_dump_command()
        };
        let params = DaemonDiagnosticsStreamParameters {
            stream_mode: Some(StreamMode::SnapshotAll),
            min_target_timestamp_nanos: Some(START_TIMESTAMP_FOR_DAEMON),
            ..DaemonDiagnosticsStreamParameters::EMPTY
        };

        log_cmd(
            setup_fake_daemon_server(params, Arc::new(vec![])),
            setup_fake_rcs(),
            &mut formatter,
            cmd,
        )
        .await
        .unwrap();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_from_monotonic_passed_to_daemon() {
        let mut formatter = FakeLogFormatter::new();
        let cmd = LogCommand {
            cmd: LogSubCommand::Dump(DumpCommand {
                from: None,
                from_monotonic: Some(default_ts()),
                to: None,
                to_monotonic: None,
            }),
            ..empty_dump_command()
        };
        let params = DaemonDiagnosticsStreamParameters {
            stream_mode: Some(StreamMode::SnapshotAll),
            min_target_timestamp_nanos: Some(default_ts().as_nanos() as u64),
            ..DaemonDiagnosticsStreamParameters::EMPTY
        };

        log_cmd(
            setup_fake_daemon_server(params, Arc::new(vec![])),
            setup_fake_rcs(),
            &mut formatter,
            cmd,
        )
        .await
        .unwrap();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_multiple_from_time_args_fails() {
        let mut formatter = FakeLogFormatter::new();
        let cmd = LogCommand {
            cmd: LogSubCommand::Dump(DumpCommand {
                from: Some(Local.timestamp(FAKE_START_TIMESTAMP, 0)),
                from_monotonic: Some(default_ts()),
                to: None,
                to_monotonic: None,
            }),
            ..empty_dump_command()
        };

        assert!(log_cmd(
            setup_fake_daemon_server(DaemonDiagnosticsStreamParameters::EMPTY, Arc::new(vec![])),
            setup_fake_rcs(),
            &mut formatter,
            cmd,
        )
        .await
        .unwrap_err()
        .ffx_error()
        .is_some());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_multiple_to_time_args_fails() {
        let mut formatter = FakeLogFormatter::new();
        let cmd = LogCommand {
            cmd: LogSubCommand::Dump(DumpCommand {
                to: Some(Local.timestamp(FAKE_START_TIMESTAMP, 0)),
                to_monotonic: Some(default_ts()),
                from: None,
                from_monotonic: None,
            }),
            ..empty_dump_command()
        };

        assert!(log_cmd(
            setup_fake_daemon_server(DaemonDiagnosticsStreamParameters::EMPTY, Arc::new(vec![])),
            setup_fake_rcs(),
            &mut formatter,
            cmd,
        )
        .await
        .unwrap_err()
        .ffx_error()
        .is_some());
    }
}
