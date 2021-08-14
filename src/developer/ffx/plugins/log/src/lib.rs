// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Error, Result},
    async_trait::async_trait,
    blocking::Unblock,
    chrono::{DateTime, Local, TimeZone, Utc},
    diagnostics_data::{LogsData, Severity, Timestamp},
    errors::ffx_bail,
    ffx_config::{get, get_sdk},
    ffx_core::ffx_plugin,
    ffx_log_args::{LogCommand, TimeFormat},
    ffx_log_data::{EventType, LogData, LogEntry},
    ffx_log_frontend::{exec_log_cmd, LogCommandParameters, LogFormatter},
    ffx_log_utils::symbolizer::is_current_sdk_root_registered,
    fidl_fuchsia_developer_bridge::{DaemonProxy, StreamMode},
    fidl_fuchsia_developer_remotecontrol::{ArchiveIteratorError, RemoteControlProxy},
    fidl_fuchsia_diagnostics::ComponentSelector,
    fuchsia_async::futures::{AsyncWrite, AsyncWriteExt},
    selectors::{match_moniker_against_component_selectors, parse_path_to_moniker},
    std::{
        iter::Iterator,
        time::{Duration, SystemTime},
    },
    termion::{color, style},
};

type ArchiveIteratorResult = Result<LogEntry, ArchiveIteratorError>;
const COLOR_CONFIG_NAME: &str = "log_cmd.color";
const SYMBOLIZE_ENABLED_CONFIG: &str = "proactive_log.symbolize.enabled";
const NANOS_IN_SECOND: i64 = 1_000_000_000;
const TIMESTAMP_FORMAT: &str = "%Y-%m-%d %H:%M:%S.%3f";

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
    monikers: Vec<ComponentSelector>,
    exclude_monikers: Vec<ComponentSelector>,
    min_severity: Severity,
    filters: Vec<String>,
    excludes: Vec<String>,
    tags: Vec<String>,
    exclude_tags: Vec<String>,
}

impl Default for LogFilterCriteria {
    fn default() -> Self {
        Self {
            monikers: vec![],
            exclude_monikers: vec![],
            min_severity: Severity::Info,
            filters: vec![],
            excludes: vec![],
            tags: vec![],
            exclude_tags: vec![],
        }
    }
}

impl LogFilterCriteria {
    fn new(
        monikers: Vec<ComponentSelector>,
        exclude_monikers: Vec<ComponentSelector>,
        min_severity: Severity,
        filters: Vec<String>,
        excludes: Vec<String>,
        tags: Vec<String>,
        exclude_tags: Vec<String>,
    ) -> Self {
        Self {
            monikers,
            exclude_monikers,
            min_severity: min_severity,
            filters,
            excludes,
            tags,
            exclude_tags,
        }
    }

    fn matches_filter_string(filter_string: &str, message: &str, log: &LogsData) -> bool {
        return message.contains(filter_string)
            || log.moniker.contains(filter_string)
            || log.metadata.component_url.as_ref().map_or(false, |s| s.contains(filter_string));
    }

    fn match_filters_to_log_data(&self, data: &LogsData, msg: &str) -> bool {
        if data.metadata.severity < self.min_severity {
            return false;
        }

        if !self.filters.is_empty()
            && !self.filters.iter().any(|f| Self::matches_filter_string(f, msg, &data))
        {
            return false;
        }

        if self.excludes.iter().any(|f| Self::matches_filter_string(f, msg, &data)) {
            return false;
        }

        if !self.tags.is_empty()
            && !self.tags.iter().any(|f| data.tags().map(|t| t.contains(f)).unwrap_or(false))
        {
            return false;
        }

        if self.exclude_tags.iter().any(|f| data.tags().map(|t| t.contains(f)).unwrap_or(false)) {
            return false;
        }

        let parsed_moniker = match parse_path_to_moniker(&data.moniker) {
            Ok(m) => m,
            Err(e) => {
                log::warn!("got a broken moniker '{}' in a log entry: {}", &data.moniker, e);
                return true;
            }
        };

        let pos_match_results =
            match_moniker_against_component_selectors(&parsed_moniker, &self.monikers);
        match pos_match_results {
            Err(ref e) => {
                log::warn!("got a bad selector in log matcher: {}", e)
            }
            Ok(result) => {
                if !self.monikers.is_empty() && result.is_empty() {
                    return false;
                }
            }
        }

        let neg_match_results =
            match_moniker_against_component_selectors(&parsed_moniker, &self.exclude_monikers);
        match neg_match_results {
            Err(ref e) => {
                log::warn!("got a bad selector in log matcher: {}", e)
            }
            Ok(result) => {
                if !(self.exclude_monikers.is_empty() || result.is_empty()) {
                    return false;
                }
            }
        }

        true
    }

    fn matches(&self, entry: &LogEntry) -> bool {
        match entry {
            LogEntry { data: LogData::TargetLog(data), .. } => {
                self.match_filters_to_log_data(data, data.msg().unwrap_or(""))
            }
            LogEntry { data: LogData::SymbolizedTargetLog(data, message), .. } => {
                self.match_filters_to_log_data(data, message)
            }
            _ => true,
        }
    }
}

impl From<&LogCommand> for LogFilterCriteria {
    fn from(cmd: &LogCommand) -> Self {
        let mut monikers: Vec<ComponentSelector> = cmd.moniker.clone();
        if cmd.kernel {
            monikers.push(selectors::parse_component_selector("klog").unwrap());
        }

        LogFilterCriteria::new(
            monikers,
            cmd.exclude_moniker.clone(),
            cmd.severity,
            cmd.contains.clone(),
            cmd.exclude.clone(),
            cmd.tags.clone(),
            cmd.exclude_tags.clone(),
        )
    }
}

pub struct LogFormatterOptions {
    color: bool,
    time_format: TimeFormat,
    show_metadata: bool,
    no_symbols: bool,
    show_tags: bool,
}

pub struct DefaultLogFormatter<'a> {
    writer: Box<dyn AsyncWrite + Unpin + 'a>,
    has_previous_log: bool,
    filters: LogFilterCriteria,
    boot_ts_nanos: Option<i64>,
    options: LogFormatterOptions,
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
                        if !self.options.no_symbols && symbolized.is_empty() {
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
            Err(e) => format!("got an error fetching next log: {:?}", e),
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
        writer: impl AsyncWrite + Unpin + 'a,
        options: LogFormatterOptions,
    ) -> Self {
        Self {
            filters,
            writer: Box::new(writer),
            has_previous_log: false,
            boot_ts_nanos: None,
            options,
        }
    }

    fn format_target_timestamp(&self, ts: Timestamp) -> String {
        let mut abs_ts = 0;
        let time_format = match self.boot_ts_nanos {
            Some(boot_ts) => {
                abs_ts = boot_ts + *ts;
                self.options.time_format.clone()
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

    pub fn format_target_log_data(&self, data: LogsData, symbolized_msg: Option<String>) -> String {
        let symbolized_msg = if self.options.no_symbols { None } else { symbolized_msg };

        let ts = self.format_target_timestamp(data.metadata.timestamp);
        let color_str = if self.options.color {
            severity_to_color_str(data.metadata.severity)
        } else {
            String::default()
        };

        let msg = symbolized_msg.unwrap_or(data.msg().unwrap_or("<missing message>").to_string());

        let process_info_str = if self.options.show_metadata {
            format!("[{}][{}]", data.pid().unwrap_or(0), data.tid().unwrap_or(0))
        } else {
            String::default()
        };

        let tags_str = if self.options.show_tags {
            format!("[{}]", data.tags().map(|t| t.join(",")).unwrap_or(String::default()))
        } else {
            String::default()
        };

        let severity_str = &format!("{}", data.metadata.severity)[..1];
        msg.lines()
            .map(|l| {
                format!(
                    "[{}]{}[{}]{}[{}{}{}] {}{}{}",
                    ts,
                    process_info_str,
                    data.moniker,
                    tags_str,
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

fn should_color(config_color: bool, cmd_no_color: bool) -> bool {
    if cmd_no_color {
        return false;
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

async fn print_symbolizer_warning(err: Error) {
    eprintln!(
        "Warning: attempting to get the symbolizer binary failed.
This likely means that your logs will not be symbolized."
    );
    eprintln!("\nThe failure was: {}", err);

    let sdk_type: Result<String, _> = get("sdk.type").await;
    if sdk_type.is_err() || sdk_type.unwrap() == "" {
        eprintln!("If you are working in-tree, ensure that the sdk.type config setting is set accordingly:");
        eprintln!("  ffx config set sdk.type in-tree");
    }
}

#[ffx_plugin("proactive_log.enabled")]
pub async fn log(
    daemon_proxy: DaemonProxy,
    rcs_proxy: RemoteControlProxy,
    cmd: LogCommand,
) -> Result<()> {
    log_impl(daemon_proxy, rcs_proxy, cmd, &mut std::io::stdout()).await
}

pub async fn log_impl<W: std::io::Write>(
    daemon_proxy: DaemonProxy,
    rcs_proxy: RemoteControlProxy,
    cmd: LogCommand,
    writer: &mut W,
) -> Result<()> {
    let config_color: bool = get(COLOR_CONFIG_NAME).await?;

    let mut stdout = Unblock::new(std::io::stdout());
    let mut formatter = DefaultLogFormatter::new(
        LogFilterCriteria::from(&cmd),
        &mut stdout,
        LogFormatterOptions {
            color: should_color(config_color, cmd.no_color),
            time_format: cmd.clock.clone(),
            show_metadata: cmd.show_metadata,
            no_symbols: cmd.no_symbols,
            show_tags: cmd.show_tags,
        },
    );

    if get(SYMBOLIZE_ENABLED_CONFIG).await.unwrap_or(true) {
        match get_sdk().await {
            Ok(s) => match s.get_host_tool("symbolizer") {
                Ok(_) => {
                    let registered_result = is_current_sdk_root_registered().await;
                    if let Ok(false) = registered_result {
                        let sdk_type: Result<String, _> = get("sdk.type").await;
                        eprintln!(
                            "It looks like there is no symbol index for your sdk root registered."
                        );
                        eprintln!("If you want symbolization to work correctly, run the following from your checkout:");

                        if sdk_type.is_ok() && sdk_type.unwrap() == "in-tree".to_string() {
                            eprintln!("  fx symbol-index register");
                        } else {
                            let symbol_index = s.get_host_tool("symbol-index");
                            match symbol_index {
                                Ok(path) => {
                                    eprintln!("  {} register", path.to_string_lossy().to_string());
                                }
                                Err(e) => {
                                    eprintln!("We could not find the path to the symbol-index host-tool: {}", e);
                                }
                            }
                        }

                        eprintln!("\nSilence this message in the future by disabling the `proactive_log.symbolize.enabled` config setting, \
                                    or passing the '--ignore-symbolizer-failure' flag to this command.");
                    } else if registered_result.is_err() {
                        log::warn!(
                            "checking the registration of the SDK root failed: {}",
                            registered_result.as_ref().unwrap_err()
                        )
                    }
                }
                Err(e) => {
                    print_symbolizer_warning(e).await;
                }
            },
            Err(e) => {
                print_symbolizer_warning(e).await;
            }
        };
    }

    log_cmd(daemon_proxy, rcs_proxy, &mut formatter, cmd, writer).await
}

pub async fn log_cmd<W: std::io::Write>(
    daemon_proxy: DaemonProxy,
    rcs: RemoteControlProxy,
    log_formatter: &mut impl LogFormatter,
    cmd: LogCommand,
    writer: &mut W,
) -> Result<()> {
    let stream_mode = if cmd.dump {
        if cmd.no_dump_recent {
            ffx_bail!("--no-dump-recent may not be used in combination with --dump");
        }

        StreamMode::SnapshotAll
    } else if cmd.no_dump_recent {
        StreamMode::Subscribe
    } else {
        StreamMode::SnapshotRecentThenSubscribe
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
    let (from_bound, to_bound) = if cmd.dump {
        if !(cmd.since.is_none() || cmd.since_monotonic.is_none()) {
            ffx_bail!("only one of --from or --from-monotonic may be provided at once.");
        }
        if !(cmd.until.is_none() || cmd.until_monotonic.is_none()) {
            ffx_bail!("only one of --to or --to-monotonic may be provided at once.");
        }

        if target_info.boot_timestamp_nanos.is_none()
            && (cmd.since.is_some() || cmd.until.is_some())
        {
            writeln!(
                writer,
                "{}target timestamp not available - since/until filters will not be applied.{}",
                color::Fg(color::Red),
                style::Reset
            )?;
            (None, None)
        } else {
            (
                calculate_monotonic_time(target_boot_time_nanos, cmd.since, cmd.since_monotonic),
                calculate_monotonic_time(target_boot_time_nanos, cmd.until, cmd.until_monotonic),
            )
        }
    } else if cmd.since.is_some()
        || cmd.since_monotonic.is_some()
        || cmd.until.is_some()
        || cmd.until_monotonic.is_some()
    {
        ffx_bail!("--since and --until may only be used in combination with --dump.");
    } else {
        (None, None)
    };
    exec_log_cmd(
        LogCommandParameters {
            target_identifier: nodename,
            session_timestamp: target_boot_time_nanos,
            target_from_bound: from_bound,
            target_to_bound: to_bound,
            stream_mode,
        },
        daemon_proxy,
        log_formatter,
        writer,
    )
    .await
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
        fidl_fuchsia_developer_bridge::{DaemonDiagnosticsStreamParameters, DaemonRequest},
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
                setup_fake_archive_iterator(iterator, expected_responses.clone(), false).unwrap();
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

    fn empty_log_command() -> LogCommand {
        LogCommand {
            contains: vec![],
            exclude: vec![],
            tags: vec![],
            exclude_tags: vec![],
            show_tags: false,
            clock: TimeFormat::Monotonic,
            no_color: false,
            moniker: vec![],
            exclude_moniker: vec![],
            kernel: false,
            severity: Severity::Info,
            show_metadata: false,
            no_dump_recent: false,
            no_symbols: false,
            dump: false,
            since: None,
            since_monotonic: None,
            until: None,
            until_monotonic: None,
        }
    }

    fn empty_dump_command() -> LogCommand {
        LogCommand { dump: true, ..empty_log_command() }
    }

    fn logs_data() -> LogsData {
        diagnostics_data::LogsDataBuilder::new(diagnostics_data::BuilderArgs {
            timestamp_nanos: Timestamp::from(default_ts().as_nanos() as i64),
            component_url: Some("component_url".to_string()),
            moniker: "some/moniker".to_string(),
            severity: diagnostics_data::Severity::Warn,
            size_bytes: 1,
        })
        .set_message("message")
        .add_tag("tag1")
        .add_tag("tag2")
        .set_pid(1)
        .set_tid(2)
        .build()
    }

    fn default_log_formatter_options() -> LogFormatterOptions {
        LogFormatterOptions {
            color: false,
            time_format: TimeFormat::Monotonic,
            show_metadata: false,
            no_symbols: false,
            show_tags: false,
        }
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

        let mut writer = Vec::new();
        log_cmd(
            setup_fake_daemon_server(params, Arc::new(expected_responses)),
            setup_fake_rcs(),
            &mut formatter,
            cmd,
            &mut writer,
        )
        .await
        .unwrap();

        let output = String::from_utf8(writer).unwrap();
        assert!(output.is_empty());
        formatter.assert_same_logs(vec![])
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_watch() {
        let mut formatter = FakeLogFormatter::new();
        let cmd = empty_log_command();
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

        let mut writer = Vec::new();
        log_cmd(
            setup_fake_daemon_server(params, Arc::new(expected_responses)),
            setup_fake_rcs(),
            &mut formatter,
            cmd,
            &mut writer,
        )
        .await
        .unwrap();

        let output = String::from_utf8(writer).unwrap();
        assert!(output.is_empty());
        formatter.assert_same_logs(vec![Ok(log1), Ok(log2), Ok(log3)])
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_watch_no_dump_with_error() {
        let mut formatter = FakeLogFormatter::new();
        let cmd = LogCommand { no_dump_recent: true, ..empty_log_command() };
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

        let mut writer = Vec::new();
        log_cmd(
            setup_fake_daemon_server(params, Arc::new(expected_responses)),
            setup_fake_rcs(),
            &mut formatter,
            cmd,
            &mut writer,
        )
        .await
        .unwrap();

        let output = String::from_utf8(writer).unwrap();
        assert!(output.is_empty());
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
        let cmd = LogCommand { until_monotonic: Some(default_ts()), ..empty_dump_command() };
        let params = DaemonDiagnosticsStreamParameters {
            stream_mode: Some(StreamMode::SnapshotAll),
            ..DaemonDiagnosticsStreamParameters::EMPTY
        };
        let log1 = make_log_entry(
            diagnostics_data::LogsDataBuilder::new(diagnostics_data::BuilderArgs {
                timestamp_nanos: Timestamp::from(
                    (default_ts() - Duration::from_nanos(1)).as_nanos() as i64,
                ),
                component_url: Some(String::default()),
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
                component_url: Some(String::default()),
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
                component_url: Some(String::default()),
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

        let mut writer = Vec::new();
        assert!(log_cmd(
            setup_fake_daemon_server(params, Arc::new(expected_responses)),
            setup_fake_rcs(),
            &mut formatter,
            cmd,
            &mut writer,
        )
        .await
        .is_ok());

        let output = String::from_utf8(writer).unwrap();
        assert!(output.is_empty());
        formatter.assert_same_logs(vec![Ok(log1), Ok(log2)])
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_criteria_moniker_message_and_severity_matches() {
        let cmd = LogCommand {
            contains: vec!["included".to_string()],
            exclude: vec!["not this".to_string()],
            severity: Severity::Error,
            ..empty_dump_command()
        };
        let criteria = LogFilterCriteria::from(&cmd);

        assert!(criteria.matches(&make_log_entry(
            diagnostics_data::LogsDataBuilder::new(diagnostics_data::BuilderArgs {
                timestamp_nanos: 0.into(),
                component_url: Some(String::default()),
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
                component_url: Some(String::default()),
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
                component_url: Some(String::default()),
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
                component_url: Some(String::default()),
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
                component_url: Some(String::default()),
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
    async fn test_criteria_message_severity_symbolized_log() {
        let cmd = LogCommand {
            contains: vec!["included".to_string()],
            exclude: vec!["not this".to_string()],
            severity: Severity::Error,
            ..empty_dump_command()
        };
        let criteria = LogFilterCriteria::from(&cmd);

        assert!(criteria.matches(&make_log_entry(LogData::SymbolizedTargetLog(
            diagnostics_data::LogsDataBuilder::new(diagnostics_data::BuilderArgs {
                timestamp_nanos: 0.into(),
                component_url: Some(String::default()),
                moniker: "included/moniker".to_string(),
                severity: diagnostics_data::Severity::Error,
                size_bytes: 1,
            })
            .set_message("not this")
            .build(),
            "included".to_string()
        ))));

        assert!(criteria.matches(&make_log_entry(LogData::SymbolizedTargetLog(
            diagnostics_data::LogsDataBuilder::new(diagnostics_data::BuilderArgs {
                timestamp_nanos: 0.into(),
                component_url: Some(String::default()),
                moniker: "included/moniker".to_string(),
                severity: diagnostics_data::Severity::Error,
                size_bytes: 1,
            })
            .set_message("some message")
            .build(),
            "some message".to_string()
        ))));

        assert!(!criteria.matches(&make_log_entry(LogData::SymbolizedTargetLog(
            diagnostics_data::LogsDataBuilder::new(diagnostics_data::BuilderArgs {
                timestamp_nanos: 0.into(),
                component_url: Some(String::default()),
                moniker: "included/moniker".to_string(),
                severity: diagnostics_data::Severity::Warn,
                size_bytes: 1,
            })
            .set_message("not this")
            .build(),
            "included".to_string()
        ))));
        assert!(!criteria.matches(&make_log_entry(LogData::SymbolizedTargetLog(
            diagnostics_data::LogsDataBuilder::new(diagnostics_data::BuilderArgs {
                timestamp_nanos: 0.into(),
                component_url: Some(String::default()),
                moniker: "included/moniker".to_string(),
                severity: diagnostics_data::Severity::Error,
                size_bytes: 1,
            })
            .set_message("included")
            .build(),
            "not this".to_string()
        ))));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_empty_criteria() {
        let cmd = empty_dump_command();
        let criteria = LogFilterCriteria::from(&cmd);

        assert!(criteria.matches(&make_log_entry(
            diagnostics_data::LogsDataBuilder::new(diagnostics_data::BuilderArgs {
                timestamp_nanos: 0.into(),
                component_url: Some(String::default()),
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
                component_url: Some(String::default()),
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
                component_url: Some(String::default()),
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
                component_url: Some(String::default()),
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
                component_url: Some(String::default()),
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
                component_url: Some(String::default()),
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
            contains: vec!["included".to_string(), "also".to_string()],
            ..empty_dump_command()
        };
        let criteria = LogFilterCriteria::from(&cmd);

        assert!(criteria.matches(&make_log_entry(
            diagnostics_data::LogsDataBuilder::new(diagnostics_data::BuilderArgs {
                timestamp_nanos: 0.into(),
                component_url: Some(String::default()),
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
                component_url: Some(String::default()),
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
                component_url: Some(String::default()),
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
                component_url: Some(String::default()),
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
                component_url: Some(String::default()),
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
                component_url: Some(String::default()),
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
                component_url: Some(String::default()),
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
                component_url: Some(String::default()),
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
                component_url: Some(String::default()),
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
                component_url: Some(String::default()),
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
    async fn test_criteria_tag_filter() {
        let cmd = LogCommand {
            tags: vec!["tag1".to_string()],
            exclude_tags: vec!["tag3".to_string()],
            ..empty_dump_command()
        };
        let criteria = LogFilterCriteria::from(&cmd);

        assert!(criteria.matches(&make_log_entry(
            diagnostics_data::LogsDataBuilder::new(diagnostics_data::BuilderArgs {
                timestamp_nanos: 0.into(),
                component_url: Some(String::default()),
                moniker: String::default(),
                severity: diagnostics_data::Severity::Error,
                size_bytes: 1,
            })
            .set_message("included")
            .add_tag("tag1")
            .add_tag("tag2")
            .build()
            .into()
        )));

        assert!(!criteria.matches(&make_log_entry(
            diagnostics_data::LogsDataBuilder::new(diagnostics_data::BuilderArgs {
                timestamp_nanos: 0.into(),
                component_url: Some(String::default()),
                moniker: String::default(),
                severity: diagnostics_data::Severity::Error,
                size_bytes: 1,
            })
            .set_message("included")
            .add_tag("tag2")
            .build()
            .into()
        )));
        assert!(!criteria.matches(&make_log_entry(
            diagnostics_data::LogsDataBuilder::new(diagnostics_data::BuilderArgs {
                timestamp_nanos: 0.into(),
                component_url: Some(String::default()),
                moniker: String::default(),
                severity: diagnostics_data::Severity::Error,
                size_bytes: 1,
            })
            .set_message("included")
            .add_tag("tag1")
            .add_tag("tag3")
            .build()
            .into()
        )));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_criteria_matches_component_url() {
        let cmd = LogCommand {
            contains: vec!["fuchsia.com".to_string()],
            exclude: vec!["not-this-component.cmx".to_string()],
            ..empty_dump_command()
        };
        let criteria = LogFilterCriteria::from(&cmd);

        assert!(criteria.matches(&make_log_entry(
            diagnostics_data::LogsDataBuilder::new(diagnostics_data::BuilderArgs {
                timestamp_nanos: 0.into(),
                component_url: Some("fuchsia.com/this-component.cmx".to_string()),
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
                component_url: Some("fuchsia.com/not-this-component.cmx".to_string()),
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
                component_url: Some("some-other.com/component.cmx".to_string()),
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
            since: Some(Local.timestamp(FAKE_START_TIMESTAMP, 0)),
            since_monotonic: None,
            until: None,
            until_monotonic: None,
            ..empty_dump_command()
        };
        let params = DaemonDiagnosticsStreamParameters {
            stream_mode: Some(StreamMode::SnapshotAll),
            min_target_timestamp_nanos: Some(START_TIMESTAMP_FOR_DAEMON),
            ..DaemonDiagnosticsStreamParameters::EMPTY
        };

        let mut writer = Vec::new();
        log_cmd(
            setup_fake_daemon_server(params, Arc::new(vec![])),
            setup_fake_rcs(),
            &mut formatter,
            cmd,
            &mut writer,
        )
        .await
        .unwrap();

        let output = String::from_utf8(writer).unwrap();
        assert!(output.is_empty());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_since_monotonic_passed_to_daemon() {
        let mut formatter = FakeLogFormatter::new();
        let cmd = LogCommand {
            since: None,
            since_monotonic: Some(default_ts()),
            until: None,
            until_monotonic: None,
            ..empty_dump_command()
        };
        let params = DaemonDiagnosticsStreamParameters {
            stream_mode: Some(StreamMode::SnapshotAll),
            min_target_timestamp_nanos: Some(default_ts().as_nanos() as u64),
            ..DaemonDiagnosticsStreamParameters::EMPTY
        };

        let mut writer = Vec::new();
        log_cmd(
            setup_fake_daemon_server(params, Arc::new(vec![])),
            setup_fake_rcs(),
            &mut formatter,
            cmd,
            &mut writer,
        )
        .await
        .unwrap();

        let output = String::from_utf8(writer).unwrap();
        assert!(output.is_empty());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_multiple_from_time_args_fails() {
        let mut formatter = FakeLogFormatter::new();
        let cmd = LogCommand {
            since: Some(Local.timestamp(FAKE_START_TIMESTAMP, 0)),
            since_monotonic: Some(default_ts()),
            until: None,
            until_monotonic: None,
            ..empty_dump_command()
        };

        let mut writer = Vec::new();
        assert!(log_cmd(
            setup_fake_daemon_server(DaemonDiagnosticsStreamParameters::EMPTY, Arc::new(vec![])),
            setup_fake_rcs(),
            &mut formatter,
            cmd,
            &mut writer,
        )
        .await
        .unwrap_err()
        .ffx_error()
        .is_some());

        let output = String::from_utf8(writer).unwrap();
        assert!(output.is_empty());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_multiple_to_time_args_fails() {
        let mut formatter = FakeLogFormatter::new();
        let cmd = LogCommand {
            until: Some(Local.timestamp(FAKE_START_TIMESTAMP, 0)),
            until_monotonic: Some(default_ts()),
            since: None,
            since_monotonic: None,
            ..empty_dump_command()
        };

        let mut writer = Vec::new();
        assert!(log_cmd(
            setup_fake_daemon_server(DaemonDiagnosticsStreamParameters::EMPTY, Arc::new(vec![])),
            setup_fake_rcs(),
            &mut formatter,
            cmd,
            &mut writer,
        )
        .await
        .unwrap_err()
        .ffx_error()
        .is_some());

        let output = String::from_utf8(writer).unwrap();
        assert!(output.is_empty());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_default_formatter() {
        let mut stdout = Unblock::new(std::io::stdout());
        let formatter = DefaultLogFormatter::new(
            LogFilterCriteria::default(),
            &mut stdout,
            default_log_formatter_options(),
        );

        assert_eq!(
            formatter.format_target_log_data(logs_data(), None),
            "[1615535969.000][some/moniker][W\u{1b}[m] message\u{1b}[m"
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_default_formatter_local_time() {
        let mut stdout = Unblock::new(std::io::stdout());
        let mut formatter = DefaultLogFormatter::new(
            LogFilterCriteria::default(),
            &mut stdout,
            LogFormatterOptions {
                time_format: TimeFormat::Local,
                ..default_log_formatter_options()
            },
        );

        // Before setting the boot timestamp, it should use monotonic time.
        assert_eq!(
            formatter.format_target_log_data(logs_data(), None),
            "[1615535969.000][some/moniker][W\u{1b}[m] message\u{1b}[m"
        );

        formatter.set_boot_timestamp(1);

        // In order to avoid flakey tests due to timezone differences, we just verify that
        // the output *did* change.
        assert_ne!(
            formatter.format_target_log_data(logs_data(), None),
            "[1615535969.000][some/moniker][W\u{1b}[m] message\u{1b}[m"
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_default_formatter_utc_time() {
        let mut stdout = Unblock::new(std::io::stdout());
        let mut formatter = DefaultLogFormatter::new(
            LogFilterCriteria::default(),
            &mut stdout,
            LogFormatterOptions { time_format: TimeFormat::Utc, ..default_log_formatter_options() },
        );

        // Before setting the boot timestamp, it should use monotonic time.
        assert_eq!(
            formatter.format_target_log_data(logs_data(), None),
            "[1615535969.000][some/moniker][W\u{1b}[m] message\u{1b}[m"
        );

        formatter.set_boot_timestamp(1);
        assert_eq!(
            formatter.format_target_log_data(logs_data(), None),
            "[2021-03-12 07:59:29.000][some/moniker][W\u{1b}[m] message\u{1b}[m"
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_default_formatter_colored_output() {
        let mut stdout = Unblock::new(std::io::stdout());
        let formatter = DefaultLogFormatter::new(
            LogFilterCriteria::default(),
            &mut stdout,
            LogFormatterOptions { color: true, ..default_log_formatter_options() },
        );

        assert_eq!(
            formatter.format_target_log_data(logs_data(), None),
            "[1615535969.000][some/moniker][\u{1b}[38;5;3mW\u{1b}[m] \u{1b}[38;5;3mmessage\u{1b}[m"
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_default_formatter_show_metadata() {
        let mut stdout = Unblock::new(std::io::stdout());
        let formatter = DefaultLogFormatter::new(
            LogFilterCriteria::default(),
            &mut stdout,
            LogFormatterOptions { show_metadata: true, ..default_log_formatter_options() },
        );

        assert_eq!(
            formatter.format_target_log_data(logs_data(), None),
            "[1615535969.000][1][2][some/moniker][W\u{1b}[m] message\u{1b}[m"
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_default_formatter_symbolized_log_message() {
        let mut stdout = Unblock::new(std::io::stdout());
        let formatter = DefaultLogFormatter::new(
            LogFilterCriteria::default(),
            &mut stdout,
            default_log_formatter_options(),
        );

        assert_eq!(
            formatter.format_target_log_data(logs_data(), Some("symbolized".to_string())),
            "[1615535969.000][some/moniker][W\u{1b}[m] symbolized\u{1b}[m"
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_default_formatter_no_symbols() {
        let mut stdout = Unblock::new(std::io::stdout());
        let formatter = DefaultLogFormatter::new(
            LogFilterCriteria::default(),
            &mut stdout,
            LogFormatterOptions { no_symbols: true, ..default_log_formatter_options() },
        );

        assert_eq!(
            formatter.format_target_log_data(logs_data(), Some("symbolized".to_string())),
            "[1615535969.000][some/moniker][W\u{1b}[m] message\u{1b}[m"
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_default_formatter_show_tags() {
        let mut stdout = Unblock::new(std::io::stdout());
        let formatter = DefaultLogFormatter::new(
            LogFilterCriteria::default(),
            &mut stdout,
            LogFormatterOptions { show_tags: true, ..default_log_formatter_options() },
        );

        assert_eq!(
            formatter.format_target_log_data(logs_data(), None),
            "[1615535969.000][some/moniker][tag1,tag2][W\u{1b}[m] message\u{1b}[m"
        );
    }
}
