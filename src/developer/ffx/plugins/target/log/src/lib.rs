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
    diagnostics_data::{LogsData, Severity, Timestamp},
    ffx_config::get,
    ffx_core::{ffx_error, ffx_plugin},
    ffx_log_args::{DumpCommand, LogCommand, LogSubCommand, WatchCommand},
    ffx_log_data::{EventType, LogData, LogEntry},
    ffx_log_utils::{run_logging_pipeline, OrderedBatchPipeline},
    fidl::endpoints::create_proxy,
    fidl_fuchsia_developer_bridge::{
        self as bridge, DaemonDiagnosticsStreamParameters, DiagnosticsStreamError, StreamMode,
    },
    fidl_fuchsia_developer_remotecontrol::{ArchiveIteratorError, ArchiveIteratorMarker},
    fidl_fuchsia_diagnostics::ComponentSelector,
    selectors::{match_moniker_against_component_selectors, parse_path_to_moniker},
    std::iter::Iterator,
    termion::{color, style},
};

type ArchiveIteratorResult = Result<LogEntry, ArchiveIteratorError>;
const PIPELINE_SIZE: usize = 20;
const COLOR_CONFIG_NAME: &str = "target_log.color";
const NO_STREAM_ERROR: &str = "\
The proactive logger isn't connected to this target.

Verify that the target is up with `ffx target list` and retry \
in a few seconds.";

fn timestamp_to_partial_secs(ts: Timestamp) -> f64 {
    let u_ts: u64 = ts.into();
    u_ts as f64 / 1_000_000_000.0
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

#[ffx_plugin("proactive_log.enabled")]
pub async fn log(daemon_proxy: bridge::DaemonProxy, cmd: LogCommand) -> Result<()> {
    let config_color: bool = get(COLOR_CONFIG_NAME).await?;
    let mut formatter = DefaultLogFormatter::new(
        LogFilterCriteria::from(&cmd),
        should_color(config_color, cmd.color),
        stdout(),
    );
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
                log_formatter.push_log(Ok(parsed)).await?
            }
        }

        if let Some(err) = terminal_err {
            return Err(anyhow!(err));
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
        diagnostics_data::{LogsData, LogsField, Timestamp},
        diagnostics_hierarchy::{DiagnosticsHierarchy, Property},
        ffx_log_test_utils::{setup_fake_archive_iterator, FakeArchiveIteratorResponse},
        fidl::Error as FidlError,
        fidl_fuchsia_developer_remotecontrol::ArchiveIteratorError,
        selectors::parse_component_selector,
    };

    const DEFAULT_TS: u64 = 1234567;
    const DEFAULT_TARGET_STR: &str = "target-target";
    const DEFAULT_COMPONENT_URL: &str = "fake-url";

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

    fn assert_correct_result(r: Result<()>) {
        assert!(r.is_err(), "expected a ClientChannelClosed error, got Ok");
        let err = r.unwrap_err();
        let e = err.downcast_ref::<FidlError>().unwrap();
        match e {
            FidlError::ClientChannelClosed { .. } => {}
            actual => panic!("expected a ClientChannelClosed error, got {}", actual),
        }
    }

    fn make_log_with_default_url(moniker: &str, msg: &str, severity: Severity) -> LogData {
        make_log(moniker, msg, DEFAULT_COMPONENT_URL, severity)
    }

    fn make_log(moniker: &str, msg: &str, component_url: &str, severity: Severity) -> LogData {
        let hierarchy = DiagnosticsHierarchy::new(
            "root",
            vec![Property::String(LogsField::Msg, msg.to_string())],
            vec![],
        );
        LogData::TargetLog(LogsData::for_logs(
            String::from(moniker),
            Some(hierarchy),
            Timestamp::from(1u64),
            String::from(component_url),
            severity,
            1,
            vec![],
        ))
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

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dump_empty() {
        let mut formatter = FakeLogFormatter::new();
        let cmd = empty_log_command(LogSubCommand::Dump(DumpCommand {}));
        let params = DaemonDiagnosticsStreamParameters {
            stream_mode: Some(StreamMode::SnapshotAll),
            ..DaemonDiagnosticsStreamParameters::EMPTY
        };
        let expected_responses = vec![];

        assert_correct_result(
            log_cmd(
                setup_fake_daemon_server(params, Arc::new(expected_responses)),
                &mut formatter,
                String::from(DEFAULT_TARGET_STR),
                cmd,
            )
            .await,
        );

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

        assert_correct_result(
            log_cmd(
                setup_fake_daemon_server(params, Arc::new(expected_responses)),
                &mut formatter,
                String::from(DEFAULT_TARGET_STR),
                cmd,
            )
            .await,
        );

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

        assert_correct_result(
            log_cmd(
                setup_fake_daemon_server(params, Arc::new(expected_responses)),
                &mut formatter,
                String::from(DEFAULT_TARGET_STR),
                cmd,
            )
            .await,
        );

        formatter.assert_same_logs(vec![
            Ok(log1),
            Ok(log2),
            Err(ArchiveIteratorError::GenericError),
            Ok(log3),
        ])
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_criteria_moniker_message_and_severity_matches() {
        let cmd = LogCommand {
            cmd: LogSubCommand::Dump(DumpCommand {}),
            color: None,
            moniker: vec![],
            exclude_moniker: vec![],
            filter: vec!["included".to_string()],
            exclude: vec!["not this".to_string()],
            min_severity: Severity::Error,
        };
        let criteria = LogFilterCriteria::from(&cmd);

        assert!(criteria.matches(&make_log_entry(make_log_with_default_url(
            "included/moniker",
            "included message",
            Severity::Error
        ))));
        assert!(criteria.matches(&make_log_entry(make_log_with_default_url(
            "included/moniker",
            "included message",
            Severity::Fatal
        ))));
        assert!(!criteria.matches(&make_log_entry(make_log_with_default_url(
            "not/this/moniker",
            "different message",
            Severity::Error
        ))));
        assert!(!criteria.matches(&make_log_entry(make_log_with_default_url(
            "included/moniker",
            "included message",
            Severity::Warn
        ))));
        assert!(!criteria.matches(&make_log_entry(make_log_with_default_url(
            "other/moniker",
            "not this message",
            Severity::Error
        ))));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_empty_criteria() {
        let cmd = LogCommand {
            cmd: LogSubCommand::Dump(DumpCommand {}),
            color: None,
            moniker: vec![],
            exclude_moniker: vec![],
            filter: vec![],
            exclude: vec![],
            min_severity: Severity::Info,
        };
        let criteria = LogFilterCriteria::from(&cmd);

        assert!(criteria.matches(&make_log_entry(make_log_with_default_url(
            "included/moniker",
            "included message",
            Severity::Error
        ))));
        assert!(criteria.matches(&make_log_entry(make_log_with_default_url(
            "included/moniker",
            "different message",
            Severity::Info
        ))));
        assert!(!criteria.matches(&make_log_entry(make_log_with_default_url(
            "other/moniker",
            "included message",
            Severity::Debug
        ))));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_criteria_multiple_moniker_matches() {
        let cmd = LogCommand {
            cmd: LogSubCommand::Dump(DumpCommand {}),
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

        assert!(criteria.matches(&make_log_entry(make_log_with_default_url(
            "included/moniker",
            "included message",
            Severity::Error
        ))));
        assert!(criteria.matches(&make_log_entry(make_log_with_default_url(
            "also/moniker",
            "different message",
            Severity::Error
        ))));
        assert!(!criteria.matches(&make_log_entry(make_log_with_default_url(
            "other/moniker",
            "included message",
            Severity::Error
        ))));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_criteria_multiple_matches() {
        let cmd = LogCommand {
            cmd: LogSubCommand::Dump(DumpCommand {}),
            color: None,
            moniker: vec![],
            exclude_moniker: vec![],
            filter: vec!["included".to_string(), "also".to_string()],
            exclude: vec![],
            min_severity: Severity::Info,
        };
        let criteria = LogFilterCriteria::from(&cmd);

        assert!(criteria.matches(&make_log_entry(make_log_with_default_url(
            "moniker",
            "included message",
            Severity::Error
        ))));
        assert!(criteria.matches(&make_log_entry(make_log_with_default_url(
            "moniker",
            "also message",
            Severity::Info
        ))));
        assert!(criteria.matches(&make_log_entry(make_log_with_default_url(
            "included/moniker",
            "not in there message",
            Severity::Info
        ))));
        assert!(!criteria.matches(&make_log_entry(make_log_with_default_url(
            "moniker",
            "different message",
            Severity::Error
        ))));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_criteria_multiple_monikers_excluded() {
        let cmd = LogCommand {
            cmd: LogSubCommand::Dump(DumpCommand {}),
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

        assert!(!criteria.matches(&make_log_entry(make_log_with_default_url(
            "included/moniker.cmx:12345",
            "included message",
            Severity::Error
        ))));
        assert!(!criteria.matches(&make_log_entry(make_log_with_default_url(
            "also/moniker",
            "different message",
            Severity::Error
        ))));
        assert!(criteria.matches(&make_log_entry(make_log_with_default_url(
            "other/moniker",
            "included message",
            Severity::Error
        ))));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_criteria_multiple_excludes() {
        let cmd = LogCommand {
            cmd: LogSubCommand::Dump(DumpCommand {}),
            color: None,
            moniker: vec![],
            exclude_moniker: vec![],
            filter: vec![],
            exclude: vec![".cmx".to_string(), "also".to_string()],
            min_severity: Severity::Info,
        };
        let criteria = LogFilterCriteria::from(&cmd);

        assert!(!criteria.matches(&make_log_entry(make_log_with_default_url(
            "included/moniker.cmx:12345",
            "included message",
            Severity::Error
        ))));
        assert!(!criteria.matches(&make_log_entry(make_log_with_default_url(
            "also/moniker",
            "different message",
            Severity::Error
        ))));
        assert!(criteria.matches(&make_log_entry(make_log_with_default_url(
            "other/moniker",
            "included message",
            Severity::Error
        ))));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_criteria_matches_component_url() {
        let cmd = LogCommand {
            cmd: LogSubCommand::Dump(DumpCommand {}),
            color: None,
            moniker: vec![],
            exclude_moniker: vec![],
            filter: vec!["fuchsia.com".to_string()],
            exclude: vec!["not-this-component.cmx".to_string()],
            min_severity: Severity::Info,
        };
        let criteria = LogFilterCriteria::from(&cmd);

        assert!(criteria.matches(&make_log_entry(make_log(
            "any/moniker",
            "message",
            "fuchsia.com/this-component.cmx",
            Severity::Error
        ))));
        assert!(!criteria.matches(&make_log_entry(make_log(
            "any/moniker",
            "message",
            "fuchsia.com/not-this-component.cmx",
            Severity::Error
        ))));
        assert!(!criteria.matches(&make_log_entry(make_log(
            "any/moniker",
            "message",
            "some-other.com/component.cmx",
            Severity::Error
        ))));
    }
}
