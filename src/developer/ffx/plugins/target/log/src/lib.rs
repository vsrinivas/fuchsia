// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Result},
    async_std::{
        io::{stdout, Write},
        prelude::*,
        sync::Arc,
    },
    async_trait::async_trait,
    chrono::{DateTime, Local, TimeZone},
    diagnostics_data::{LogsData, Severity, Timestamp},
    ffx_config::get,
    ffx_core::{ffx_bail, ffx_error, ffx_plugin},
    ffx_log_args::{DumpCommand, LogCommand, LogSubCommand, WatchCommand},
    ffx_log_data::{EventType, LogData, LogEntry},
    ffx_log_utils::{run_logging_pipeline, OrderedBatchPipeline},
    fidl::endpoints::create_proxy,
    fidl_fuchsia_developer_bridge::{
        DaemonDiagnosticsStreamParameters, DaemonProxy, DiagnosticsStreamError, StreamMode,
    },
    fidl_fuchsia_developer_remotecontrol::{
        ArchiveIteratorError, ArchiveIteratorMarker, RemoteControlProxy,
    },
    fidl_fuchsia_diagnostics::ComponentSelector,
    selectors::{match_moniker_against_component_selectors, parse_path_to_moniker},
    std::{iter::Iterator, time::Duration},
    termion::{color, style},
};

type ArchiveIteratorResult = Result<LogEntry, ArchiveIteratorError>;
const PIPELINE_SIZE: usize = 20;
const COLOR_CONFIG_NAME: &str = "target_log.color";
const NO_STREAM_ERROR: &str = "\
The proactive logger isn't connected to this target.

Verify that the target is up with `ffx target list` and retry \
in a few seconds.";
const NANOS_IN_SECOND: i64 = 1_000_000_000;

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
}

struct DefaultLogFormatter<'a> {
    writer: Box<dyn Write + Unpin + 'a>,
    has_previous_log: bool,
    filters: LogFilterCriteria,
    color: bool,
}

#[async_trait(?Send)]
impl<'a> LogFormatter for DefaultLogFormatter<'_> {
    async fn push_log(&mut self, log_entry_result: ArchiveIteratorResult) -> Result<()> {
        let mut s = match log_entry_result {
            Ok(log_entry) => {
                if !self.filters.matches(&log_entry) {
                    return Ok(());
                }

                match log_entry {
                    LogEntry { data: LogData::TargetLog(data), .. } => {
                        let ts = timestamp_to_partial_secs(data.metadata.timestamp);
                        let color_str = if self.color {
                            severity_to_color_str(data.metadata.severity)
                        } else {
                            String::default()
                        };

                        let severity_str = &format!("{}", data.metadata.severity)[..1];
                        format!(
                            "[{:05.3}][{}][{}{}{}] {}{}{}",
                            ts,
                            data.moniker,
                            color_str,
                            severity_str,
                            style::Reset,
                            color_str,
                            data.msg().unwrap(),
                            style::Reset
                        )
                    }
                    LogEntry { data: LogData::MalformedTargetLog(raw), .. } => {
                        format!("malformed target log: {}", raw)
                    }
                    LogEntry { data: LogData::FfxEvent(etype), timestamp, .. } => match etype {
                        EventType::LoggingStarted => {
                            let ts: i64 = timestamp.into();
                            let dt = Local
                                .timestamp(ts / NANOS_IN_SECOND, (ts % NANOS_IN_SECOND) as u32)
                                .to_rfc2822();
                            let mut s = format!("----[<ffx daemon>] {}: logger started.", dt);
                            if self.has_previous_log {
                                s.push_str(" Logs before this may have been dropped if they were not cached on the target.");
                            }
                            s
                        }
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
    fn new(filters: LogFilterCriteria, color: bool, writer: impl Write + Unpin + 'a) -> Self {
        Self { filters, color, writer: Box::new(writer), has_previous_log: false }
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
        (Some(dt), None) => Some(Duration::from_secs(dt.timestamp() as u64) - boot_ts),
        (None, Some(m)) => Some(m),
        (None, None) => None,
        _ => panic!("can only accept either a datetime bound or a monotonic one."),
    }
}

#[ffx_plugin("proactive_log.enabled")]
pub async fn log(
    daemon_proxy: DaemonProxy,
    rcs_proxy: RemoteControlProxy,
    cmd: LogCommand,
) -> Result<()> {
    let config_color: bool = get(COLOR_CONFIG_NAME).await?;
    let mut formatter = DefaultLogFormatter::new(
        LogFilterCriteria::from(&cmd),
        should_color(config_color, cmd.color),
        stdout(),
    );
    let ffx: ffx_lib_args::Ffx = argh::from_env();
    let target_str = ffx.target.unwrap_or(String::default());
    log_cmd(daemon_proxy, rcs_proxy, &mut formatter, target_str, cmd).await
}

pub async fn log_cmd(
    daemon_proxy: DaemonProxy,
    rcs: RemoteControlProxy,
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
        LogSubCommand::Dump(DumpCommand { .. }) => StreamMode::SnapshotAll,
    };

    let (from_bound, to_bound) = match cmd.cmd {
        LogSubCommand::Dump(DumpCommand { from, from_monotonic, to, to_monotonic }) => {
            if !(from.is_none() || from_monotonic.is_none()) {
                ffx_bail!("only one of --from or --from-monotonic may be provided at once.");
            }
            if !(to.is_none() || to_monotonic.is_none()) {
                ffx_bail!("only one of --to or --to-monotonic may be provided at once.");
            }

            let target_info = rcs.identify_host().await.unwrap().unwrap();
            let target_boot_time_nanos = match target_info.boot_timestamp_nanos {
                Some(t) => Duration::from_nanos(t),
                None => {
                    if from.is_some() || to.is_some() {
                        return Err(anyhow!("target is missing a boot timestamp."));
                    } else {
                        Duration::new(0, 0)
                    }
                }
            };

            (
                calculate_monotonic_time(target_boot_time_nanos, from, from_monotonic),
                calculate_monotonic_time(target_boot_time_nanos, to, to_monotonic),
            )
        }
        _ => (None, None),
    };
    let params = DaemonDiagnosticsStreamParameters {
        stream_mode: Some(stream_mode),
        min_target_timestamp_nanos: from_bound.map(|f| f.as_nanos() as u64),
        ..DaemonDiagnosticsStreamParameters::EMPTY
    };
    let _ =
        daemon_proxy.stream_diagnostics(Some(&target_str), params, server).await?.map_err(|e| {
            match e {
                DiagnosticsStreamError::NoStreamForTarget => {
                    anyhow!(ffx_error!("{}", NO_STREAM_ERROR))
                }
                _ => anyhow!("failure setting up diagnostics stream: {:?}", e),
            }
        })?;

    let mut requests = OrderedBatchPipeline::new(PIPELINE_SIZE);
    loop {
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
                    _ => {}
                }
                log_formatter.push_log(Ok(parsed)).await?
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
        async_std::sync::Arc,
        diagnostics_data::Timestamp,
        ffx_log_test_utils::{
            setup_fake_archive_iterator, FakeArchiveIteratorResponse, LogsDataBuilder,
        },
        fidl_fuchsia_developer_bridge::DaemonRequest,
        fidl_fuchsia_developer_remotecontrol::{
            ArchiveIteratorError, IdentifyHostResponse, RemoteControlRequest,
        },
        selectors::parse_component_selector,
    };

    const DEFAULT_TS_NANOS: u64 = 1615535969000000000;
    const DEFAULT_TARGET_STR: &str = "target-target";
    const BOOT_TS: u64 = 98765432000000000;
    const FAKE_START_TIMESTAMP: i64 = 1614669138;
    // FAKE_START_TIMESTAMP - BOOT_TS
    const START_TIMESTAMP_FOR_DAEMON: u64 = 1515903706000000000;

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
            DaemonRequest::StreamDiagnostics { target, parameters, iterator, responder } => {
                assert_eq!(parameters, expected_parameters);
                assert_eq!(target, Some(DEFAULT_TARGET_STR.to_string()));
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
            filter: vec![],
            exclude: vec![],
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
            LogsDataBuilder::new()
                .timestamp(Timestamp::from(
                    (default_ts() - Duration::from_nanos(1)).as_nanos() as i64
                ))
                .message("log1")
                .build()
                .into(),
        );
        let log2 = make_log_entry(
            LogsDataBuilder::new()
                .timestamp(Timestamp::from(default_ts().as_nanos() as i64))
                .message("log2")
                .build()
                .into(),
        );
        let log3 = make_log_entry(
            LogsDataBuilder::new()
                .timestamp(Timestamp::from(
                    (default_ts() + Duration::from_nanos(1)).as_nanos() as i64
                ))
                .message("log3")
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
            String::from(DEFAULT_TARGET_STR),
            cmd,
        )
        .await
        .is_ok());

        formatter.assert_same_logs(vec![Ok(log1), Ok(log2)])
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_criteria_moniker_message_and_severity_matches() {
        let cmd = LogCommand {
            cmd: empty_dump_subcommand(),
            color: None,
            moniker: vec![],
            exclude_moniker: vec![],
            filter: vec!["included".to_string()],
            exclude: vec!["not this".to_string()],
            min_severity: Severity::Error,
        };
        let criteria = LogFilterCriteria::from(&cmd);

        assert!(criteria.matches(&make_log_entry(
            LogsDataBuilder::new()
                .moniker("included/moniker")
                .message("included message")
                .severity(Severity::Error)
                .build()
                .into()
        )));
        assert!(criteria.matches(&make_log_entry(
            LogsDataBuilder::new()
                .moniker("included/moniker")
                .message("included message")
                .severity(Severity::Fatal)
                .build()
                .into()
        )));
        assert!(!criteria.matches(&make_log_entry(
            LogsDataBuilder::new()
                .moniker("not/this/moniker")
                .message("different message")
                .severity(Severity::Error)
                .build()
                .into()
        )));
        assert!(!criteria.matches(&make_log_entry(
            LogsDataBuilder::new()
                .moniker("included/moniker")
                .message("included message")
                .severity(Severity::Warn)
                .build()
                .into()
        )));
        assert!(!criteria.matches(&make_log_entry(
            LogsDataBuilder::new()
                .moniker("other/moniker")
                .message("not this message")
                .severity(Severity::Error)
                .build()
                .into()
        )));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_empty_criteria() {
        let cmd = LogCommand {
            cmd: empty_dump_subcommand(),
            color: None,
            moniker: vec![],
            exclude_moniker: vec![],
            filter: vec![],
            exclude: vec![],
            min_severity: Severity::Info,
        };
        let criteria = LogFilterCriteria::from(&cmd);

        assert!(criteria.matches(&make_log_entry(
            LogsDataBuilder::new()
                .moniker("included/moniker")
                .message("included message")
                .severity(Severity::Error)
                .build()
                .into()
        )));
        assert!(criteria.matches(&make_log_entry(
            LogsDataBuilder::new()
                .moniker("included/moniker")
                .message("different message")
                .severity(Severity::Info)
                .build()
                .into()
        )));
        assert!(!criteria.matches(&make_log_entry(
            LogsDataBuilder::new()
                .moniker("other/moniker")
                .message("included message")
                .severity(Severity::Debug)
                .build()
                .into()
        )));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_criteria_multiple_moniker_matches() {
        let cmd = LogCommand {
            cmd: empty_dump_subcommand(),
            color: None,
            moniker: vec![
                parse_component_selector(&"included/*".to_string()).unwrap(),
                parse_component_selector(&"als*/moniker".to_string()).unwrap(),
            ],
            exclude_moniker: vec![],
            filter: vec![],
            exclude: vec![],
            min_severity: Severity::Info,
        };
        let criteria = LogFilterCriteria::from(&cmd);

        assert!(criteria.matches(&make_log_entry(
            LogsDataBuilder::new()
                .moniker("included/moniker")
                .message("included message")
                .severity(Severity::Error)
                .build()
                .into()
        )));
        assert!(criteria.matches(&make_log_entry(
            LogsDataBuilder::new()
                .moniker("also/moniker")
                .message("different message")
                .severity(Severity::Error)
                .build()
                .into()
        )));
        assert!(!criteria.matches(&make_log_entry(
            LogsDataBuilder::new()
                .moniker("other/moniker")
                .message("included message")
                .severity(Severity::Error)
                .build()
                .into()
        )));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_criteria_multiple_matches() {
        let cmd = LogCommand {
            cmd: empty_dump_subcommand(),
            color: None,
            moniker: vec![],
            exclude_moniker: vec![],
            filter: vec!["included".to_string(), "also".to_string()],
            exclude: vec![],
            min_severity: Severity::Info,
        };
        let criteria = LogFilterCriteria::from(&cmd);

        assert!(criteria.matches(&make_log_entry(
            LogsDataBuilder::new()
                .moniker("moniker")
                .message("included message")
                .severity(Severity::Error)
                .build()
                .into()
        )));
        assert!(criteria.matches(&make_log_entry(
            LogsDataBuilder::new()
                .moniker("moniker")
                .message("also message")
                .severity(Severity::Info)
                .build()
                .into()
        )));
        assert!(criteria.matches(&make_log_entry(
            LogsDataBuilder::new()
                .moniker("included/moniker")
                .message("not in there message")
                .severity(Severity::Info)
                .build()
                .into()
        )));
        assert!(!criteria.matches(&make_log_entry(
            LogsDataBuilder::new()
                .moniker("moniker")
                .message("different message")
                .severity(Severity::Error)
                .build()
                .into()
        )));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_criteria_multiple_monikers_excluded() {
        let cmd = LogCommand {
            cmd: empty_dump_subcommand(),
            color: None,
            moniker: vec![],
            exclude_moniker: vec![
                parse_component_selector(&"included/*".to_string()).unwrap(),
                parse_component_selector(&"als*/moniker".to_string()).unwrap(),
            ],
            filter: vec![],
            exclude: vec![],
            min_severity: Severity::Info,
        };
        let criteria = LogFilterCriteria::from(&cmd);

        assert!(!criteria.matches(&make_log_entry(
            LogsDataBuilder::new()
                .moniker("included/moniker.cmx:12345")
                .message("included message")
                .severity(Severity::Error)
                .build()
                .into()
        )));
        assert!(!criteria.matches(&make_log_entry(
            LogsDataBuilder::new()
                .moniker("also/moniker")
                .message("different message")
                .severity(Severity::Error)
                .build()
                .into()
        )));
        assert!(criteria.matches(&make_log_entry(
            LogsDataBuilder::new()
                .moniker("other/moniker")
                .message("included message")
                .severity(Severity::Error)
                .build()
                .into()
        )));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_criteria_multiple_excludes() {
        let cmd = LogCommand {
            cmd: empty_dump_subcommand(),
            color: None,
            moniker: vec![],
            exclude_moniker: vec![],
            filter: vec![],
            exclude: vec![".cmx".to_string(), "also".to_string()],
            min_severity: Severity::Info,
        };
        let criteria = LogFilterCriteria::from(&cmd);

        assert!(!criteria.matches(&make_log_entry(
            LogsDataBuilder::new()
                .moniker("included/moniker.cmx:12345")
                .message("included message")
                .severity(Severity::Error)
                .build()
                .into()
        )));
        assert!(!criteria.matches(&make_log_entry(
            LogsDataBuilder::new()
                .moniker("also/moniker")
                .message("different message")
                .severity(Severity::Error)
                .build()
                .into()
        )));
        assert!(criteria.matches(&make_log_entry(
            LogsDataBuilder::new()
                .moniker("other/moniker")
                .message("included message")
                .severity(Severity::Error)
                .build()
                .into()
        )));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_criteria_matches_component_url() {
        let cmd = LogCommand {
            cmd: empty_dump_subcommand(),
            color: None,
            moniker: vec![],
            exclude_moniker: vec![],
            filter: vec!["fuchsia.com".to_string()],
            exclude: vec!["not-this-component.cmx".to_string()],
            min_severity: Severity::Info,
        };
        let criteria = LogFilterCriteria::from(&cmd);

        assert!(criteria.matches(&make_log_entry(
            LogsDataBuilder::new()
                .moniker("any/moniker")
                .message("message")
                .severity(Severity::Error)
                .component_url("fuchsia.com/this-component.cmx")
                .build()
                .into()
        )));
        assert!(!criteria.matches(&make_log_entry(
            LogsDataBuilder::new()
                .moniker("any/moniker")
                .message("message")
                .severity(Severity::Error)
                .component_url("fuchsia.com/not-this-component.cmx")
                .build()
                .into()
        )));
        assert!(!criteria.matches(&make_log_entry(
            LogsDataBuilder::new()
                .moniker("any/moniker")
                .message("message")
                .severity(Severity::Error)
                .component_url("some-other.com/component.cmx")
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
            color: None,
            moniker: vec![],
            exclude_moniker: vec![],
            filter: vec![],
            exclude: vec![],
            min_severity: Severity::Info,
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
            String::from(DEFAULT_TARGET_STR),
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
            color: None,
            moniker: vec![],
            exclude_moniker: vec![],
            filter: vec![],
            exclude: vec![],
            min_severity: Severity::Info,
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
            String::from(DEFAULT_TARGET_STR),
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
            color: None,
            moniker: vec![],
            exclude_moniker: vec![],
            filter: vec![],
            exclude: vec![],
            min_severity: Severity::Info,
        };

        assert!(log_cmd(
            setup_fake_daemon_server(DaemonDiagnosticsStreamParameters::EMPTY, Arc::new(vec![])),
            setup_fake_rcs(),
            &mut formatter,
            String::from(DEFAULT_TARGET_STR),
            cmd,
        )
        .await
        .unwrap_err()
        .downcast_ref::<ffx_core::FfxError>()
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
            color: None,
            moniker: vec![],
            exclude_moniker: vec![],
            filter: vec![],
            exclude: vec![],
            min_severity: Severity::Info,
        };

        assert!(log_cmd(
            setup_fake_daemon_server(DaemonDiagnosticsStreamParameters::EMPTY, Arc::new(vec![])),
            setup_fake_rcs(),
            &mut formatter,
            String::from(DEFAULT_TARGET_STR),
            cmd,
        )
        .await
        .unwrap_err()
        .downcast_ref::<ffx_core::FfxError>()
        .is_some());
    }
}
