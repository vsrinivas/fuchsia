// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::trace::duration,
    anyhow::Error,
    diagnostics_data::{LogTextDisplayOptions, LogTextPresenter, LogsData, Severity},
    fidl_fuchsia_test_manager::LogsIteratorOption,
    futures::{Stream, TryStreamExt},
    std::io::Write,
};

/// Configuration for display of text-based (unstructured)
/// logs.
#[derive(Clone, Default)]
pub(crate) struct LogDisplayConfiguration {
    /// Whether or not to show the full moniker
    pub show_full_moniker: bool,
}

// TODO(fxbug.dev/54198, fxbug.dev/70581): deprecate this when implementing metadata selectors for
// logs or when we support OnRegisterInterest that can be sent to *all* test components.
#[derive(Clone, Default)]
pub(crate) struct LogCollectionOptions {
    /// The minimum severity for collecting logs.
    pub min_severity: Option<Severity>,

    /// The maximum severity for collecting logs.
    pub max_severity: Option<Severity>,

    /// Log display options for unstructured logs.
    pub format: LogDisplayConfiguration,
}

impl LogCollectionOptions {
    fn is_restricted_log(&self, log: &LogsData) -> bool {
        let severity = log.metadata.severity;
        matches!(self.max_severity, Some(max) if severity > max)
    }

    fn should_display(&self, log: &LogsData) -> bool {
        let severity = log.metadata.severity;
        matches!(self.min_severity, None)
            || matches!(self.min_severity, Some(min) if severity >= min)
    }
}

#[derive(Debug, PartialEq, Eq)]
pub enum LogCollectionOutcome {
    Error { restricted_logs: Vec<String> },
    Passed,
}

impl From<Vec<String>> for LogCollectionOutcome {
    fn from(restricted_logs: Vec<String>) -> Self {
        if restricted_logs.is_empty() {
            LogCollectionOutcome::Passed
        } else {
            LogCollectionOutcome::Error { restricted_logs }
        }
    }
}

/// Collects logs from |stream|, filters out low severity logs, and stores the results
/// in |log_artifact|. Returns any high severity restricted logs that are encountered.
pub(crate) async fn collect_logs<S, W>(
    mut stream: S,
    mut log_artifact: W,
    options: LogCollectionOptions,
) -> Result<LogCollectionOutcome, Error>
where
    S: Stream<Item = Result<LogsData, Error>> + Unpin,
    W: Write,
{
    duration!("collect_logs");
    let mut restricted_logs = vec![];
    while let Some(log) = stream.try_next().await? {
        duration!("process_single_log");
        let is_restricted = options.is_restricted_log(&log);
        let should_display = options.should_display(&log);
        if !should_display && !is_restricted {
            continue;
        }

        let log_repr = format!(
            "{}",
            LogTextPresenter::new(
                &log,
                LogTextDisplayOptions { show_full_moniker: options.format.show_full_moniker }
            )
        );

        if should_display {
            writeln!(log_artifact, "{}", log_repr)?;
        }

        if is_restricted {
            restricted_logs.push(log_repr);
        }
    }
    Ok(restricted_logs.into())
}

#[cfg(target_os = "fuchsia")]
pub fn get_type() -> LogsIteratorOption {
    LogsIteratorOption::BatchIterator
}

#[cfg(not(target_os = "fuchsia"))]
pub fn get_type() -> LogsIteratorOption {
    LogsIteratorOption::ArchiveIterator
}

#[cfg(test)]
mod test {
    use {
        super::*,
        diagnostics_data::{BuilderArgs, LogsDataBuilder},
    };

    #[fuchsia::test]
    async fn filter_low_severity() {
        let input_logs = vec![
            LogsDataBuilder::new(BuilderArgs {
                moniker: "<root>".into(),
                timestamp_nanos: 0i64.into(),
                component_url: "test-root-url".to_string().into(),
                severity: Severity::Info,
            })
            .set_message("my info log")
            .build(),
            LogsDataBuilder::new(BuilderArgs {
                moniker: "child".into(),
                timestamp_nanos: 1000i64.into(),
                component_url: "test-child-url".to_string().into(),
                severity: Severity::Warn,
            })
            .set_message("my info log")
            .build(),
        ];
        let displayed_logs = vec![LogsDataBuilder::new(BuilderArgs {
            moniker: "child".into(),
            timestamp_nanos: 1000i64.into(),
            component_url: "test-child-url".to_string().into(),
            severity: Severity::Warn,
        })
        .set_message("my info log")
        .build()];

        let mut log_artifact = vec![];
        assert_eq!(
            collect_logs(
                futures::stream::iter(input_logs.into_iter().map(Ok)),
                &mut log_artifact,
                LogCollectionOptions {
                    min_severity: Severity::Warn.into(),
                    max_severity: None,
                    format: LogDisplayConfiguration { show_full_moniker: true }
                },
            )
            .await
            .unwrap(),
            LogCollectionOutcome::Passed
        );
        assert_eq!(
            String::from_utf8(log_artifact).unwrap(),
            displayed_logs.iter().map(|log| format!("{}\n", log)).collect::<Vec<_>>().concat()
        );
    }

    #[fuchsia::test]
    async fn filter_log_moniker() {
        let unaltered_logs = vec![
            LogsDataBuilder::new(BuilderArgs {
                moniker: "<root>".into(),
                timestamp_nanos: 0i64.into(),
                component_url: "test-root-url".to_string().into(),
                severity: Severity::Info,
            })
            .set_message("my info log")
            .build(),
            LogsDataBuilder::new(BuilderArgs {
                moniker: "<root>/child/a".into(),
                timestamp_nanos: 1000i64.into(),
                component_url: "test-child-url".to_string().into(),
                severity: Severity::Warn,
            })
            .set_message("my warn log")
            .build(),
        ];
        let altered_moniker_logs = vec![
            LogsDataBuilder::new(BuilderArgs {
                moniker: "<root>".into(),
                timestamp_nanos: 0i64.into(),
                component_url: "test-root-url".to_string().into(),
                severity: Severity::Info,
            })
            .set_message("my info log")
            .build(),
            LogsDataBuilder::new(BuilderArgs {
                moniker: "a".into(),
                timestamp_nanos: 1000i64.into(),
                component_url: "test-child-url".to_string().into(),
                severity: Severity::Warn,
            })
            .set_message("my warn log")
            .build(),
        ];

        let mut log_artifact = vec![];
        assert_eq!(
            collect_logs(
                futures::stream::iter(unaltered_logs.into_iter().map(Ok)),
                &mut log_artifact,
                LogCollectionOptions {
                    min_severity: None,
                    max_severity: None,
                    format: LogDisplayConfiguration { show_full_moniker: false }
                }
            )
            .await
            .unwrap(),
            LogCollectionOutcome::Passed
        );
        assert_eq!(
            String::from_utf8(log_artifact).unwrap(),
            altered_moniker_logs
                .iter()
                .map(|log| format!(
                    "{}\n",
                    LogTextPresenter::new(log, LogTextDisplayOptions { show_full_moniker: false })
                ))
                .collect::<Vec<_>>()
                .concat()
        );
    }

    #[fuchsia::test]
    async fn no_filter_log_moniker() {
        let unaltered_logs = vec![
            LogsDataBuilder::new(BuilderArgs {
                moniker: "<root>".into(),
                timestamp_nanos: 0i64.into(),
                component_url: "test-root-url".to_string().into(),
                severity: Severity::Info,
            })
            .set_message("my info log")
            .build(),
            LogsDataBuilder::new(BuilderArgs {
                moniker: "child/a".into(),
                timestamp_nanos: 1000i64.into(),
                component_url: "test-child-url".to_string().into(),
                severity: Severity::Warn,
            })
            .set_message("my warn log")
            .build(),
        ];
        let altered_moniker_logs = vec![
            LogsDataBuilder::new(BuilderArgs {
                moniker: "<root>".into(),
                timestamp_nanos: 0i64.into(),
                component_url: "test-root-url".to_string().into(),
                severity: Severity::Info,
            })
            .set_message("my info log")
            .build(),
            LogsDataBuilder::new(BuilderArgs {
                moniker: "child/a".into(),
                timestamp_nanos: 1000i64.into(),
                component_url: "test-child-url".to_string().into(),
                severity: Severity::Warn,
            })
            .set_message("my warn log")
            .build(),
        ];

        let mut log_artifact = vec![];
        assert_eq!(
            collect_logs(
                futures::stream::iter(unaltered_logs.into_iter().map(Ok)),
                &mut log_artifact,
                LogCollectionOptions {
                    min_severity: None,
                    max_severity: None,
                    format: LogDisplayConfiguration { show_full_moniker: true }
                }
            )
            .await
            .unwrap(),
            LogCollectionOutcome::Passed
        );
        assert_eq!(
            String::from_utf8(log_artifact).unwrap(),
            altered_moniker_logs
                .iter()
                .map(|log| format!("{}\n", log))
                .collect::<Vec<_>>()
                .concat()
        );
    }

    #[fuchsia::test]
    async fn display_restricted_logs() {
        let input_logs = vec![
            LogsDataBuilder::new(BuilderArgs {
                moniker: "<root>".into(),
                timestamp_nanos: 0i64.into(),
                component_url: "test-root-url".to_string().into(),
                severity: Severity::Info,
            })
            .set_message("my info log")
            .build(),
            LogsDataBuilder::new(BuilderArgs {
                moniker: "child".into(),
                timestamp_nanos: 1000i64.into(),
                component_url: "test-child-url".to_string().into(),
                severity: Severity::Error,
            })
            .set_message("my error log")
            .build(),
        ];
        let displayed_logs = vec![
            LogsDataBuilder::new(BuilderArgs {
                moniker: "<root>".into(),
                timestamp_nanos: 0i64.into(),
                component_url: "test-root-url".to_string().into(),
                severity: Severity::Info,
            })
            .set_message("my info log")
            .build(),
            LogsDataBuilder::new(BuilderArgs {
                moniker: "child".into(),
                timestamp_nanos: 1000i64.into(),
                component_url: "test-child-url".to_string().into(),
                severity: Severity::Error,
            })
            .set_message("my error log")
            .build(),
        ];

        let mut log_artifact = vec![];
        assert_eq!(
            collect_logs(
                futures::stream::iter(input_logs.into_iter().map(Ok)),
                &mut log_artifact,
                LogCollectionOptions {
                    min_severity: None,
                    max_severity: Severity::Warn.into(),
                    format: LogDisplayConfiguration { show_full_moniker: true }
                }
            )
            .await
            .unwrap(),
            LogCollectionOutcome::Error { restricted_logs: vec![format!("{}", displayed_logs[1])] }
        );
        assert_eq!(
            String::from_utf8(log_artifact).unwrap(),
            displayed_logs.iter().map(|log| format!("{}\n", log)).collect::<Vec<_>>().concat()
        );
    }
}
