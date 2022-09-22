// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{cancel::OrCancel, trace::duration},
    anyhow::Error,
    diagnostics_data::{LogTextDisplayOptions, LogTextPresenter, LogsData, Severity},
    fidl_fuchsia_test_manager::LogsIteratorOption,
    fuchsia_async as fasync,
    futures::{Future, FutureExt, Stream, TryStreamExt},
    std::{io::Write, time::Duration},
    tracing::warn,
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

/// Options for timing out log collection.
/// Log collection is timed out when both |timeout_fut| is resolved and
/// |time_between_logs| has passed since either |timeout_fut| was signalled or
/// the last log was processed.
/// TODO(fxbug.dev/98223): remove timeouts once logs no longer hang.
pub(crate) struct LogTimeoutOptions<F: Future> {
    pub timeout_fut: F,
    pub time_between_logs: Duration,
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
pub(crate) async fn collect_logs<S, W, F>(
    mut stream: S,
    mut log_artifact: W,
    options: LogCollectionOptions,
    timeout_options: LogTimeoutOptions<F>,
) -> Result<LogCollectionOutcome, Error>
where
    S: Stream<Item = Result<LogsData, Error>> + Unpin,
    W: Write,
    F: Future,
{
    duration!("collect_logs");
    let timeout_options = LogTimeoutOptions {
        timeout_fut: timeout_options.timeout_fut.map(|_| ()).shared(),
        time_between_logs: timeout_options.time_between_logs,
    };
    let mut restricted_logs = vec![];
    while let Some(log) = next_log_or_timeout(&mut stream, &timeout_options).await? {
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

async fn next_log_or_timeout<S, F>(
    stream: &mut S,
    timeout_options: &LogTimeoutOptions<futures::future::Shared<F>>,
) -> Result<Option<LogsData>, Error>
where
    S: Stream<Item = Result<LogsData, Error>> + Unpin,
    F: Future,
    <F as Future>::Output: std::clone::Clone,
{
    let timeout = async {
        timeout_options.timeout_fut.clone().await;
        fasync::Timer::new(timeout_options.time_between_logs).await;
        warn!("Log timeout invoked")
    };
    stream.try_next().or_cancelled(timeout).await.unwrap_or(Ok(None))
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

    fn infinite_log_timeout() -> LogTimeoutOptions<futures::future::Pending<()>> {
        LogTimeoutOptions {
            timeout_fut: futures::future::pending::<()>(),
            time_between_logs: Duration::ZERO,
        }
    }

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
                infinite_log_timeout(),
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
                },
                infinite_log_timeout(),
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
                },
                infinite_log_timeout(),
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
                },
                infinite_log_timeout(),
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

    // fuchsia only as TestExecutor is not available on host.
    #[cfg(target_os = "fuchsia")]
    mod fuchsia_tests {
        use super::*;
        use fuchsia_zircon as zx;
        use futures::channel::mpsc;

        #[fuchsia::test]
        fn terminate_on_timeout() {
            const TIMEOUT_BETWEEN_LOGS: zx::Duration = zx::Duration::from_seconds(5);
            let mut executor = fasync::TestExecutor::new_with_fake_time().expect("create executor");

            let (mut log_sender, log_recv) = mpsc::channel(5);
            let timeout_signal = async_utils::event::Event::new();

            let mut log_artifact = vec![];
            let mut collect_logs_fut = collect_logs(
                log_recv,
                &mut log_artifact,
                LogCollectionOptions {
                    min_severity: None,
                    max_severity: Severity::Warn.into(),
                    format: LogDisplayConfiguration { show_full_moniker: true },
                },
                LogTimeoutOptions {
                    timeout_fut: timeout_signal.wait(),
                    time_between_logs: Duration::from_secs(
                        TIMEOUT_BETWEEN_LOGS.into_seconds() as u64
                    ),
                },
            )
            .boxed();

            // send first log
            log_sender
                .try_send(Ok(LogsDataBuilder::new(BuilderArgs {
                    moniker: "<root>".into(),
                    timestamp_nanos: 0i64.into(),
                    component_url: "test-root-url".to_string().into(),
                    severity: Severity::Info,
                })
                .set_message("first log")
                .build()))
                .expect("send log");
            assert!(executor.run_until_stalled(&mut collect_logs_fut).is_pending());

            // advancing time past the time between logs shouldn't timeout logs if the
            // timeout signal isn't triggered yet
            executor.set_fake_time(
                executor.now() + TIMEOUT_BETWEEN_LOGS + zx::Duration::from_seconds(1),
            );
            assert!(!executor.wake_expired_timers());
            assert!(executor.run_until_stalled(&mut collect_logs_fut).is_pending());

            // second log should be sent
            log_sender
                .try_send(Ok(LogsDataBuilder::new(BuilderArgs {
                    moniker: "<root>".into(),
                    timestamp_nanos: 0i64.into(),
                    component_url: "test-root-url".to_string().into(),
                    severity: Severity::Info,
                })
                .set_message("second log")
                .build()))
                .expect("send log");
            assert!(executor.run_until_stalled(&mut collect_logs_fut).is_pending());

            // after triggering timeout_signal, logs still received so long as time_between_logs
            // doesn't pass
            timeout_signal.signal();
            log_sender
                .try_send(Ok(LogsDataBuilder::new(BuilderArgs {
                    moniker: "<root>".into(),
                    timestamp_nanos: 0i64.into(),
                    component_url: "test-root-url".to_string().into(),
                    severity: Severity::Info,
                })
                .set_message("third log")
                .build()))
                .expect("send log");
            assert!(executor.run_until_stalled(&mut collect_logs_fut).is_pending());

            executor.set_fake_time(
                executor.now() + TIMEOUT_BETWEEN_LOGS - zx::Duration::from_seconds(1),
            );
            assert!(!executor.wake_expired_timers());
            assert!(executor.run_until_stalled(&mut collect_logs_fut).is_pending());

            // timeout period hasn't elapsed, so logs still accepted
            log_sender
                .try_send(Ok(LogsDataBuilder::new(BuilderArgs {
                    moniker: "<root>".into(),
                    timestamp_nanos: 0i64.into(),
                    component_url: "test-root-url".to_string().into(),
                    severity: Severity::Info,
                })
                .set_message("fourth log")
                .build()))
                .expect("send log");
            assert!(executor.run_until_stalled(&mut collect_logs_fut).is_pending());

            // after timeout period elapses, stop collecting logs even if stream is still open
            executor.set_fake_time(
                executor.now() + TIMEOUT_BETWEEN_LOGS + zx::Duration::from_seconds(1),
            );
            assert!(executor.wake_expired_timers());
            match executor.run_until_stalled(&mut collect_logs_fut) {
                std::task::Poll::Ready(Ok(LogCollectionOutcome::Passed)) => (),
                _ => panic!("Expected future to complete successfully"),
            }
            drop(collect_logs_fut);

            let displayed_logs = vec![
                LogsDataBuilder::new(BuilderArgs {
                    moniker: "<root>".into(),
                    timestamp_nanos: 0i64.into(),
                    component_url: "test-root-url".to_string().into(),
                    severity: Severity::Info,
                })
                .set_message("first log")
                .build(),
                LogsDataBuilder::new(BuilderArgs {
                    moniker: "<root>".into(),
                    timestamp_nanos: 0i64.into(),
                    component_url: "test-root-url".to_string().into(),
                    severity: Severity::Info,
                })
                .set_message("second log")
                .build(),
                LogsDataBuilder::new(BuilderArgs {
                    moniker: "<root>".into(),
                    timestamp_nanos: 0i64.into(),
                    component_url: "test-root-url".to_string().into(),
                    severity: Severity::Info,
                })
                .set_message("third log")
                .build(),
                LogsDataBuilder::new(BuilderArgs {
                    moniker: "<root>".into(),
                    timestamp_nanos: 0i64.into(),
                    component_url: "test-root-url".to_string().into(),
                    severity: Severity::Info,
                })
                .set_message("fourth log")
                .build(),
            ];

            assert_eq!(
                String::from_utf8(log_artifact).unwrap(),
                displayed_logs.iter().map(|log| format!("{}\n", log)).collect::<Vec<_>>().concat()
            );
        }

        #[fuchsia::test]
        fn timeout_not_triggered_until_timeout_signal_given() {
            const TIMEOUT_BETWEEN_LOGS: zx::Duration = zx::Duration::from_seconds(5);
            let mut executor = fasync::TestExecutor::new_with_fake_time().expect("create executor");
            let (mut log_sender, log_recv) = mpsc::channel(5);
            let timeout_signal = async_utils::event::Event::new();

            let mut log_artifact = vec![];
            let mut collect_logs_fut = collect_logs(
                log_recv,
                &mut log_artifact,
                LogCollectionOptions {
                    min_severity: None,
                    max_severity: Severity::Warn.into(),
                    format: LogDisplayConfiguration { show_full_moniker: true },
                },
                LogTimeoutOptions {
                    timeout_fut: timeout_signal.wait(),
                    time_between_logs: Duration::from_secs(
                        TIMEOUT_BETWEEN_LOGS.into_seconds() as u64
                    ),
                },
            )
            .boxed();

            // send first log
            log_sender
                .try_send(Ok(LogsDataBuilder::new(BuilderArgs {
                    moniker: "<root>".into(),
                    timestamp_nanos: 0i64.into(),
                    component_url: "test-root-url".to_string().into(),
                    severity: Severity::Info,
                })
                .set_message("first log")
                .build()))
                .expect("send log");
            assert!(executor.run_until_stalled(&mut collect_logs_fut).is_pending());

            // advancing time past the time between logs shouldn't timeout logs if the
            // timeout signal isn't triggered yet
            executor.set_fake_time(
                executor.now() + TIMEOUT_BETWEEN_LOGS + zx::Duration::from_seconds(1),
            );
            assert!(!executor.wake_expired_timers());
            assert!(executor.run_until_stalled(&mut collect_logs_fut).is_pending());

            // when timeout is triggered, logs should be polled for an additional
            // TIMEOUT_BETWEEN_LOGS
            timeout_signal.signal();
            assert!(executor.run_until_stalled(&mut collect_logs_fut).is_pending());
            assert!(!executor.wake_expired_timers());

            // After an additional TIMEOUT_BETWEEN_LOGS elapses logs should stop
            executor.set_fake_time(
                executor.now() + TIMEOUT_BETWEEN_LOGS + zx::Duration::from_seconds(1),
            );
            assert!(executor.wake_expired_timers());
            match executor.run_until_stalled(&mut collect_logs_fut) {
                std::task::Poll::Ready(Ok(LogCollectionOutcome::Passed)) => (),
                _ => panic!("Expected future to complete successfully"),
            }
            drop(collect_logs_fut);

            let displayed_logs = vec![LogsDataBuilder::new(BuilderArgs {
                moniker: "<root>".into(),
                timestamp_nanos: 0i64.into(),
                component_url: "test-root-url".to_string().into(),
                severity: Severity::Info,
            })
            .set_message("first log")
            .build()];

            assert_eq!(
                String::from_utf8(log_artifact).unwrap(),
                displayed_logs.iter().map(|log| format!("{}\n", log)).collect::<Vec<_>>().concat()
            );
        }
    }
}
