// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    diagnostics_data::Severity,
    fidl::Peered,
    fidl_fuchsia_test_manager::{
        self as ftest_manager, CaseArtifact, CaseFinished, CaseFound, CaseStarted, CaseStopped,
        RunBuilderProxy, SuiteArtifact, SuiteStopped,
    },
    fuchsia_async as fasync,
    futures::{
        channel::mpsc,
        future::{BoxFuture, Either},
        prelude::*,
        stream::LocalBoxStream,
        StreamExt,
    },
    log::{error, warn},
    std::collections::{HashMap, HashSet, VecDeque},
    std::convert::TryInto,
    std::fmt,
    std::io::{self, ErrorKind, Write},
    std::path::PathBuf,
    std::sync::Arc,
    std::time::Duration,
};

mod artifact;
pub mod diagnostics;
mod error;
pub mod output;
mod stream_util;

pub use error::{RunTestSuiteError, UnexpectedEventError};
use {
    artifact::{Artifact, ArtifactSender},
    output::{
        AnsiFilterWriter, ArtifactType, CaseId, DirectoryArtifactType, DirectoryReporter,
        NoopReporter, RunReporter, SuiteId, SuiteReporter, Timestamp,
    },
    stream_util::StreamUtil,
};

/// Duration after which to emit an excessive duration log.
const EXCESSIVE_DURATION: Duration = Duration::from_secs(60);

#[derive(Debug, Clone)]
pub enum Outcome {
    Passed,
    Failed,
    Inconclusive,
    Timedout,
    /// Suite was stopped prematurely due to cancellation by the user.
    Cancelled,
    /// Suite did not report completion.
    // TODO(fxbug.dev/90037) - this outcome indicates an internal error as test manager isn't
    // sending expected events. We should return an error instead.
    DidNotFinish,
    Error {
        origin: Arc<RunTestSuiteError>,
    },
}

impl Outcome {
    fn error<E: Into<RunTestSuiteError>>(e: E) -> Self {
        Self::Error { origin: Arc::new(e.into()) }
    }
}

impl PartialEq for Outcome {
    fn eq(&self, other: &Self) -> bool {
        match (self, other) {
            (Self::Passed, Self::Passed)
            | (Self::Failed, Self::Failed)
            | (Self::Inconclusive, Self::Inconclusive)
            | (Self::Timedout, Self::Timedout)
            | (Self::Cancelled, Self::Cancelled)
            | (Self::DidNotFinish, Self::DidNotFinish) => true,
            (Self::Error { origin }, Self::Error { origin: other_origin }) => {
                format!("{}", origin.as_ref()) == format!("{}", other_origin.as_ref())
            }
            (_, _) => false,
        }
    }
}

impl fmt::Display for Outcome {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Outcome::Passed => write!(f, "PASSED"),
            Outcome::Failed => write!(f, "FAILED"),
            Outcome::Inconclusive => write!(f, "INCONCLUSIVE"),
            Outcome::Timedout => write!(f, "TIMED OUT"),
            Outcome::Cancelled => write!(f, "CANCELLED"),
            Outcome::DidNotFinish => write!(f, "DID_NOT_FINISH"),
            Outcome::Error { .. } => write!(f, "ERROR"),
        }
    }
}

#[derive(PartialEq, Debug)]
pub struct SuiteRunResult {
    /// URL of the executed test suite.
    pub url: String,

    /// Test outcome.
    pub outcome: Outcome,

    /// All tests which were executed.
    pub executed: Vec<String>,

    /// All tests which passed.
    pub passed: Vec<String>,

    /// All tests which failed.
    pub failed: Vec<String>,

    /// restricted logs produced by this suite run which exceed expected log level.
    pub restricted_logs: Vec<String>,
}

/// Parameters that specify how a single test suite should be executed.
#[derive(Clone, Debug, PartialEq)]
pub struct TestParams {
    /// Test URL.
    pub test_url: String,

    /// |timeout|: Test timeout.should be more than zero.
    pub timeout: Option<std::num::NonZeroU32>,

    /// Filter tests based on glob pattern(s).
    pub test_filters: Option<Vec<String>>,

    // Run disabled tests.
    pub also_run_disabled_tests: bool,

    /// Test concurrency count.
    pub parallel: Option<u16>,

    /// Arguments to pass to test using command line.
    pub test_args: Vec<String>,

    /// Maximum allowable log severity for the test.
    pub max_severity_logs: Option<Severity>,
}

/// Parameters that specify how the overall test run should be executed.
pub struct RunParams {
    /// The behavior of the test run if a suite times out.
    pub timeout_behavior: TimeoutBehavior,

    /// If set, stop executing tests after this number of normal test failures occur.
    pub stop_after_failures: Option<std::num::NonZeroU16>,
}

/// Sets the behavior of the overall run if a suite terminates with a timeout.
pub enum TimeoutBehavior {
    /// Immediately terminate any suites that haven't started.
    TerminateRemaining,
    /// Continue executing any suites that haven't finished.
    Continue,
}

async fn collect_results_for_suite<F: Future<Output = ()>>(
    mut running_suite: RunningSuite,
    mut artifact_sender: ArtifactSender,
    suite_reporter: &SuiteReporter<'_>,
    log_opts: diagnostics::LogCollectionOptions,
    cancel_fut: F,
) -> Result<SuiteRunResult, RunTestSuiteError> {
    let mut test_cases = HashMap::new();
    let mut test_case_reporters = HashMap::new();
    let mut test_cases_in_progress = HashMap::new();
    let mut test_cases_executed = HashSet::new();
    let mut test_cases_output = HashMap::new();
    let mut suite_log_tasks = vec![];
    let mut suite_finish_timestamp = Timestamp::Unknown;
    let mut outcome = Outcome::Passed;
    let mut test_cases_passed = HashSet::new();
    let mut test_cases_failed = HashSet::new();
    let mut restricted_logs = vec![];
    let mut successful_completion = false;
    let mut cancelled = false;
    let mut tasks = vec![];

    futures::pin_mut!(cancel_fut);

    loop {
        let cancellation_or_event_result =
            futures::future::select(&mut cancel_fut, running_suite.next_event().boxed()).await;
        match cancellation_or_event_result {
            // cancel invoked.
            Either::Left(((), _)) => {
                cancelled = true;
                break;
            }
            // stream completes normally.
            Either::Right((None, _)) => break,
            Either::Right((Some(Err(e)), _)) => {
                suite_reporter.stopped(&output::ReportedOutcome::Error, Timestamp::Unknown)?;
                return Err(e);
            }
            Either::Right((Some(Ok(event)), _)) => {
                let timestamp = Timestamp::from_nanos(event.timestamp);
                match event.payload.expect("event cannot be None") {
                    ftest_manager::SuiteEventPayload::CaseFound(CaseFound {
                        test_case_name,
                        identifier,
                    }) => {
                        let case_reporter =
                            suite_reporter.new_case(&test_case_name, &CaseId(identifier))?;
                        test_cases.insert(identifier, test_case_name);
                        test_case_reporters.insert(identifier, case_reporter);
                    }
                    ftest_manager::SuiteEventPayload::CaseStarted(CaseStarted { identifier }) => {
                        let test_case_name = test_cases
                            .get(&identifier)
                            .ok_or(UnexpectedEventError::CaseStartedButNotFound { identifier })?
                            .clone();
                        let reporter = test_case_reporters.get(&identifier).unwrap();
                        // TODO(fxbug.dev/79712): Record per-case runtime once we have an
                        // accurate way to measure it.
                        reporter.started(Timestamp::Unknown)?;
                        if test_cases_executed.contains(&identifier) {
                            return Err(UnexpectedEventError::CaseStartedTwice {
                                test_case_name,
                                identifier,
                            }
                            .into());
                        };
                        artifact_sender
                            .send_test_stdout_msg(format!("[RUNNING]\t{}", test_case_name))
                            .await
                            .unwrap_or_else(|e| error!("Cannot write logs: {:?}", e));

                        let mut sender_clone = artifact_sender.clone();
                        test_cases_executed.insert(identifier);
                        let excessive_time_task = fasync::Task::spawn(async move {
                            fasync::Timer::new(EXCESSIVE_DURATION).await;
                            sender_clone
                                .send_test_stdout_msg(format!(
                                    "[duration - {}]:\tStill running after {:?} seconds",
                                    test_case_name,
                                    EXCESSIVE_DURATION.as_secs()
                                ))
                                .await
                                .unwrap_or_else(|e| {
                                    error!("Failed to send excessive duration event: {}", e)
                                });
                        });
                        test_cases_in_progress.insert(identifier, excessive_time_task);
                    }
                    ftest_manager::SuiteEventPayload::CaseArtifact(CaseArtifact {
                        identifier,
                        artifact,
                    }) => match artifact {
                        ftest_manager::Artifact::Stdout(socket) => {
                            let test_case_name = test_cases
                                .get(&identifier)
                                .ok_or(UnexpectedEventError::CaseArtifactButNotFound {
                                    identifier,
                                })?
                                .clone();
                            let (sender, mut recv) = mpsc::channel(1024);
                            let t = fuchsia_async::Task::spawn(
                                test_diagnostics::collect_and_send_string_output(socket, sender),
                            );
                            let mut writer_clone = artifact_sender.clone();
                            let reporter = test_case_reporters.get(&identifier).unwrap();
                            let mut stdout = reporter.new_artifact(&ArtifactType::Stdout)?;
                            let stdout_task = fuchsia_async::Task::spawn(async move {
                                while let Some(mut msg) = recv.next().await {
                                    if msg.ends_with("\n") {
                                        msg.truncate(msg.len() - 1)
                                    }
                                    writer_clone
                                        .send_test_stdout_msg(format!(
                                            "[stdout - {}]:\n{}",
                                            test_case_name, msg
                                        ))
                                        .await
                                        .unwrap_or_else(|e| {
                                            error!(
                                                "Cannot write stdout for {}: {:?}",
                                                test_case_name, e
                                            )
                                        });
                                    writeln!(stdout, "{}", msg)?;
                                }
                                t.await?;
                                Result::Ok::<(), anyhow::Error>(())
                            });
                            test_cases_output.entry(identifier).or_insert(vec![]).push(stdout_task);
                        }
                        ftest_manager::Artifact::Stderr(socket) => {
                            let test_case_name = test_cases
                                .get(&identifier)
                                .ok_or(UnexpectedEventError::CaseArtifactButNotFound {
                                    identifier,
                                })?
                                .clone();
                            let (sender, mut recv) = mpsc::channel(1024);
                            let t = fuchsia_async::Task::spawn(
                                test_diagnostics::collect_and_send_string_output(socket, sender),
                            );
                            let mut writer_clone = artifact_sender.clone();
                            let reporter = test_case_reporters.get_mut(&identifier).unwrap();
                            let mut stderr = reporter.new_artifact(&ArtifactType::Stderr)?;
                            let stderr_task = fuchsia_async::Task::spawn(async move {
                                while let Some(mut msg) = recv.next().await {
                                    if msg.ends_with("\n") {
                                        msg.truncate(msg.len() - 1)
                                    }
                                    writer_clone
                                        .send_test_stderr_msg(format!(
                                            "[stderr - {}]:\n{}",
                                            test_case_name, msg
                                        ))
                                        .await
                                        .unwrap_or_else(|e| {
                                            error!(
                                                "Cannot write stderr for {}: {:?}",
                                                test_case_name, e
                                            )
                                        });
                                    writeln!(stderr, "{}", msg)?;
                                }
                                t.await?;
                                Result::Ok::<(), anyhow::Error>(())
                            });
                            test_cases_output.entry(identifier).or_insert(vec![]).push(stderr_task);
                        }
                        ftest_manager::Artifact::Log(_) => {
                            artifact_sender
                                .send_test_stdout_msg("WARN: per test case logs not supported yet")
                                .await
                                .unwrap_or_else(|e| error!("Cannot write logs: {:?}", e));
                        }
                        ftest_manager::Artifact::Custom(artifact) => {
                            warn!("Got a case custom artifact. Ignoring it.");
                            if let Some(ftest_manager::DirectoryAndToken { token, .. }) =
                                artifact.directory_and_token
                            {
                                // TODO(fxbug.dev/84882): Remove this signal once Overnet
                                // supports automatically signalling EVENTPAIR_CLOSED when the
                                // handle is closed.
                                let _ = token
                                    .signal_peer(fidl::Signals::empty(), fidl::Signals::USER_0);
                            }
                        }
                        ftest_manager::ArtifactUnknown!() => {
                            panic!("unknown artifact")
                        }
                    },
                    ftest_manager::SuiteEventPayload::CaseStopped(CaseStopped {
                        identifier,
                        status,
                    }) => {
                        let test_case_name = test_cases
                            .get(&identifier)
                            .ok_or(UnexpectedEventError::CaseStoppedButNotFound { identifier })?;

                        if let Some(tasks) = test_cases_output.remove(&identifier) {
                            if status == ftest_manager::CaseStatus::TimedOut {
                                for t in tasks {
                                    if let Some(Err(e)) = t.now_or_never() {
                                        error!(
                                            "Cannot write output for {}: {:?}",
                                            test_case_name, e
                                        );
                                    }
                                }
                            } else {
                                for t in tasks {
                                    if let Err(e) = t.await {
                                        error!(
                                            "Cannot write output for {}: {:?}",
                                            test_case_name, e
                                        )
                                    }
                                }
                            }
                        }
                        // the test did not finish.
                        if status == ftest_manager::CaseStatus::Error {
                            continue;
                        }
                        if let Some(excessive_timer_task) =
                            test_cases_in_progress.remove(&identifier)
                        {
                            excessive_timer_task.cancel().await;
                        } else {
                            return Err(UnexpectedEventError::CaseStoppedButNotStarted {
                                test_case_name: test_case_name.clone(),
                                identifier,
                            }
                            .into());
                        }

                        let result_str = match status {
                            ftest_manager::CaseStatus::Passed => {
                                test_cases_passed.insert(test_case_name.clone());
                                "PASSED"
                            }
                            ftest_manager::CaseStatus::Failed => {
                                test_cases_failed.insert(test_case_name.clone());
                                "FAILED"
                            }
                            ftest_manager::CaseStatus::TimedOut => {
                                test_cases_failed.insert(test_case_name.clone());
                                "TIMED_OUT"
                            }
                            ftest_manager::CaseStatus::Skipped => "SKIPPED",
                            status => {
                                return Err(UnexpectedEventError::UnrecognizedCaseStatus {
                                    status,
                                    identifier,
                                }
                                .into());
                            }
                        };
                        let reporter = test_case_reporters.get(&identifier).unwrap();
                        artifact_sender
                            .send_test_stdout_msg(format!("[{}]\t{}", result_str, test_case_name))
                            .await
                            .unwrap_or_else(|e| error!("Cannot write logs: {:?}", e));
                        // TODO(fxbug.dev/79712): Record per-case runtime once we have an
                        // accurate way to measure it.
                        reporter.stopped(&status.into(), Timestamp::Unknown)?;
                    }
                    ftest_manager::SuiteEventPayload::CaseFinished(CaseFinished { identifier }) => {
                        let reporter = test_case_reporters.remove(&identifier).unwrap();
                        reporter.finished()?;
                    }
                    ftest_manager::SuiteEventPayload::SuiteArtifact(SuiteArtifact { artifact }) => {
                        match artifact {
                            ftest_manager::Artifact::Stdout(_) => {
                                artifact_sender
                                    .send_test_stdout_msg(
                                        "WARN: suite level stdout not supported yet",
                                    )
                                    .await
                                    .unwrap_or_else(|e| error!("Cannot write logs: {:?}", e));
                            }
                            ftest_manager::Artifact::Stderr(_) => {
                                artifact_sender
                                    .send_test_stdout_msg(
                                        "WARN: suite level stderr not supported yet",
                                    )
                                    .await
                                    .unwrap_or_else(|e| error!("Cannot write logs: {:?}", e));
                            }
                            ftest_manager::Artifact::Log(syslog) => {
                                match test_diagnostics::LogStream::from_syslog(syslog) {
                                    Ok(log_stream) => {
                                        suite_log_tasks.push(fuchsia_async::Task::spawn(
                                            diagnostics::collect_logs(
                                                log_stream,
                                                artifact_sender.clone(),
                                                log_opts.clone(),
                                            ),
                                        ));
                                    }
                                    Err(e) => {
                                        artifact_sender
                                            .send_test_stdout_msg(format!("WARN: Got invalid log iterator. Cannot collect logs: {:?}", e))
                                            .await
                                            .unwrap_or_else(|e| {
                                                error!("Cannot write logs: {:?}", e)
                                            });
                                    }
                                }
                            }
                            ftest_manager::Artifact::Custom(ftest_manager::CustomArtifact {
                                directory_and_token,
                                component_moniker,
                                ..
                            }) => {
                                let ftest_manager::DirectoryAndToken { directory, token } =
                                    directory_and_token.unwrap();
                                let directory_artifact = suite_reporter.new_directory_artifact(
                                    &DirectoryArtifactType::Custom,
                                    component_moniker,
                                )?;

                                let directory = directory.into_proxy()?;
                                let directory_copy_task = async move {
                                    let directory_ref = &directory;
                                    let files_stream =
                                        files_async::readdir_recursive(directory_ref, None)
                                            .map_err(|e| {
                                                std::io::Error::new(std::io::ErrorKind::Other, e)
                                            })
                                            .try_filter_map(
                                                move |files_async::DirEntry { name, kind }| {
                                                    let res =
                                                        match kind {
                                                            files_async::DirentKind::File => {
                                                                let filepath: PathBuf = name.into();
                                                                match io_util::open_file(
                                                            directory_ref,
                                                            &filepath,
                                                            io_util::OPEN_RIGHT_READABLE,
                                                        ) {
                                                            Ok(file) => Ok(Some((file, filepath))),
                                                            Err(e) => Err(std::io::Error::new(
                                                                std::io::ErrorKind::Other,
                                                                e,
                                                            )),
                                                        }
                                                            }
                                                            _ => Ok(None),
                                                        };
                                                    futures::future::ready(res)
                                                },
                                            )
                                            .boxed();

                                    /// Max number of bytes read at once from a file.
                                    const READ_SIZE: u64 = fidl_fuchsia_io::MAX_BUF;
                                    let directory_artifact_ref = &directory_artifact;
                                    files_stream
                                        .try_for_each_concurrent(
                                            None,
                                            |(file_proxy, filepath)| async move {
                                                let mut file =
                                                    directory_artifact_ref.new_file(&filepath)?;
                                                let mut vector = VecDeque::new();
                                                // Arbitrary number of reads to pipeline. Works for
                                                // a bandwidth delay product of 800kB.
                                                const PIPELINED_READ_COUNT: u64 = 100;
                                                for _n in 0..PIPELINED_READ_COUNT {
                                                    vector.push_back(file_proxy.read(READ_SIZE));
                                                }
                                                loop {
                                                    let (status, mut buf) =
                                                        vector.pop_front().unwrap().await.map_err(
                                                            |e| io::Error::new(ErrorKind::Other, e),
                                                        )?;
                                                    if status != 0 {
                                                        // hack - use a wrapper here
                                                        return Err(io::Error::new(
                                                            ErrorKind::Other,
                                                            fidl::Error::ClientRead(
                                                                fidl::Status::from_raw(status),
                                                            ),
                                                        ));
                                                    }
                                                    if buf.is_empty() {
                                                        break;
                                                    }
                                                    file.write_all(&mut buf)?;
                                                    vector.push_back(file_proxy.read(READ_SIZE));
                                                }
                                                file.flush()
                                            },
                                        )
                                        .await
                                        .unwrap_or_else(|e| {
                                            log::warn!("Failed to copy directory: {:?}", e)
                                        });
                                    // TODO(fxbug.dev/84882): Remove this signal once Overnet
                                    // supports automatically signalling EVENTPAIR_CLOSED when the
                                    // handle is closed.
                                    token
                                        .signal_peer(fidl::Signals::empty(), fidl::Signals::USER_0)
                                        .unwrap_or_else(|e| {
                                            log::warn!("Failed to copy directory: {:?}", e)
                                        });
                                };

                                tasks.push(fasync::Task::spawn(directory_copy_task));
                            }
                            ftest_manager::ArtifactUnknown!() => {
                                panic!("unknown artifact")
                            }
                        }
                    }
                    ftest_manager::SuiteEventPayload::SuiteStarted(_) => {
                        suite_reporter.started(timestamp)?;
                    }
                    ftest_manager::SuiteEventPayload::SuiteStopped(SuiteStopped { status }) => {
                        successful_completion = true;
                        suite_finish_timestamp = timestamp;
                        outcome = match status {
                            ftest_manager::SuiteStatus::Passed => Outcome::Passed,
                            ftest_manager::SuiteStatus::Failed => Outcome::Failed,
                            ftest_manager::SuiteStatus::DidNotFinish => Outcome::Inconclusive,
                            ftest_manager::SuiteStatus::TimedOut => Outcome::Timedout,
                            ftest_manager::SuiteStatus::Stopped => Outcome::Failed,
                            ftest_manager::SuiteStatus::InternalError => {
                                Outcome::error(UnexpectedEventError::InternalErrorSuiteStatus)
                            }
                            s => {
                                return Err(UnexpectedEventError::UnrecognizedSuiteStatus {
                                    status: s,
                                }
                                .into());
                            }
                        };
                    }
                    ftest_manager::SuiteEventPayloadUnknown!() => {
                        warn!("Encountered unrecognized suite event");
                    }
                }
            }
        }
    }

    // collect all logs
    for t in suite_log_tasks {
        match t.await {
            Ok(r) => match r {
                diagnostics::LogCollectionOutcome::Error { restricted_logs: mut logs } => {
                    let mut log_artifact =
                        suite_reporter.new_artifact(&ArtifactType::RestrictedLog)?;
                    for log in logs.iter() {
                        writeln!(log_artifact, "{}", log)?;
                    }
                    restricted_logs.append(&mut logs);
                }
                diagnostics::LogCollectionOutcome::Passed => {}
            },
            Err(e) => {
                println!("Failed to collect logs: {:?}", e);
            }
        }
    }

    for t in tasks.into_iter() {
        t.await;
    }

    let mut test_cases_in_progress = test_cases_in_progress
        .into_iter()
        .map(|(i, _t)| test_cases.get(&i).unwrap())
        .collect::<Vec<_>>();
    test_cases_in_progress.sort();

    // TODO(fxbug.dev/90037) - unless the suite was cancelled, this indicates an internal error.
    // Test manager should always report CaseFinished before terminating the event stream.
    if test_cases_in_progress.len() != 0 {
        match outcome {
            Outcome::Passed | Outcome::Failed if !cancelled => {
                warn!(
                    "Some test cases in {} did not complete. This will soon return an internal error",
                    running_suite.url()
                );
                outcome = Outcome::DidNotFinish;
            }
            _ => {}
        }
        artifact_sender
            .send_test_stdout_msg("\nThe following test(s) never completed:")
            .await
            .unwrap_or_else(|e| error!("Cannot write logs: {:?}", e));
        for t in test_cases_in_progress {
            artifact_sender
                .send_test_stdout_msg(format!("{}", t))
                .await
                .unwrap_or_else(|e| error!("Cannot write logs: {:?}", e));
        }
    }

    if cancelled {
        match outcome {
            Outcome::Passed | Outcome::Failed => {
                outcome = Outcome::Cancelled;
            }
            _ => {}
        }
        artifact_sender
            .send_test_stdout_msg(format!(
                "\nExecution of test {} was cancelled.",
                running_suite.url()
            ))
            .await
            .unwrap_or_else(|e| error!("Cannot write logs: {:?}", e));
    }

    // TODO(fxbug.dev/90037) - this actually indicates an internal error. In the case that
    // run-test-suite drains all the events for the suite, test manager should've reported an
    // outcome for the suite. We should remove this outcome and return a new error enum instead.
    if !successful_completion && !cancelled {
        warn!(
            "No result was returned for {}. This will soon return an internal error",
            running_suite.url()
        );
        if matches!(&outcome, Outcome::Passed | Outcome::Failed) {
            outcome = Outcome::DidNotFinish;
        }
    }

    let mut test_cases_executed = test_cases_executed
        .into_iter()
        .map(|i| test_cases.get(&i).unwrap().clone())
        .collect::<Vec<_>>();
    let mut test_cases_passed: Vec<String> = test_cases_passed.into_iter().collect();
    let mut test_cases_failed: Vec<String> = test_cases_failed.into_iter().collect();

    test_cases_executed.sort();
    test_cases_passed.sort();
    test_cases_failed.sort();

    suite_reporter.stopped(&outcome.clone().into(), suite_finish_timestamp)?;

    Ok(SuiteRunResult {
        url: running_suite.url().to_string(),
        outcome,
        executed: test_cases_executed,
        passed: test_cases_passed,
        failed: test_cases_failed,
        restricted_logs,
    })
}

async fn run_suite_and_collect_logs<Out: Write, F: Future<Output = ()>>(
    suite: RunningSuite,
    suite_reporter: &SuiteReporter<'_>,
    log_opts: &diagnostics::LogCollectionOptions,
    stdout_writer: &mut Out,
    cancel_fut: F,
) -> Result<SuiteRunResult, RunTestSuiteError> {
    let (artifact_sender, mut artifact_recv) = mpsc::channel(1024);

    let fut1 = collect_results_for_suite(
        suite,
        artifact_sender.into(),
        &suite_reporter,
        log_opts.clone(),
        cancel_fut,
    );
    let fut2 = async {
        let mut syslog_writer = match suite_reporter.new_artifact(&ArtifactType::Syslog) {
            Ok(writer) => writer,
            Err(e) => {
                error!("Cannot create new artifact: {:?}", e);
                return;
            }
        };
        while let Some(artifact) = artifact_recv.next().await {
            match artifact {
                Artifact::SuiteStdoutMessage(msg) | Artifact::SuiteStderrMessage(msg) => {
                    writeln!(stdout_writer, "{}", msg)
                        .unwrap_or_else(|e| error!("Cannot write stdout: {:?}", e));
                }

                Artifact::SuiteLogMessage(log) => {
                    writeln!(stdout_writer, "{}", log)
                        .unwrap_or_else(|e| error!("Cannot write logs to stdout: {:?}", e));
                    writeln!(syslog_writer, "{}", log)
                        .unwrap_or_else(|e| error!("Cannot write logs to reporter: {:?}", e));
                }
            }
        }
    };

    let (result, ()) = futures::future::join(fut1, fut2).await;
    result
}

type SuiteResults<'a> = LocalBoxStream<'a, Result<SuiteRunResult, RunTestSuiteError>>;
type SuiteEventStream = std::pin::Pin<
    Box<dyn Stream<Item = Result<ftest_manager::SuiteEvent, RunTestSuiteError>> + Send>,
>;

/// A test suite that is known to have started execution. A suite is considered started once
/// any event is produced for the suite.
struct RunningSuite {
    event_stream: Option<SuiteEventStream>,
    url: String,
    max_severity_logs: Option<Severity>,
}

impl RunningSuite {
    /// Number of concurrently active GetEvents requests. Chosen by testing powers of 2 when
    /// running a set of tests using ffx test against an emulator, and taking the value at
    /// which improvement stops.
    const PIPELINED_REQUESTS: usize = 8;
    async fn wait_for_start(
        proxy: ftest_manager::SuiteControllerProxy,
        url: String,
        max_severity_logs: Option<Severity>,
    ) -> Self {
        // Stream of fidl responses, with multiple concurrently active requests.
        let unprocessed_event_stream = futures::stream::repeat_with(move || proxy.get_events())
            .buffered(Self::PIPELINED_REQUESTS);
        // Terminate the stream after we get an error or empty list of events.
        let terminated_event_stream =
            unprocessed_event_stream.take_until_stop_after(|result| match &result {
                Ok(Ok(events)) => events.is_empty(),
                Err(_) | Ok(Err(_)) => true,
            });
        // Flatten the stream of vecs into a stream of single events.
        let mut event_stream = terminated_event_stream
            .map(Self::convert_to_result_vec)
            .map(futures::stream::iter)
            .flatten()
            .peekable();
        // Wait for the first event to be ready, which signals the suite has started.
        std::pin::Pin::new(&mut event_stream).peek().await;

        Self { event_stream: Some(event_stream.boxed()), url, max_severity_logs }
    }

    fn convert_to_result_vec(
        vec: Result<
            Result<Vec<ftest_manager::SuiteEvent>, ftest_manager::LaunchError>,
            fidl::Error,
        >,
    ) -> Vec<Result<ftest_manager::SuiteEvent, RunTestSuiteError>> {
        match vec {
            Ok(Ok(events)) => events.into_iter().map(Ok).collect(),
            Ok(Err(e)) => vec![Err(e.into())],
            Err(e) => vec![Err(e.into())],
        }
    }

    async fn next_event(&mut self) -> Option<Result<ftest_manager::SuiteEvent, RunTestSuiteError>> {
        match self.event_stream.take() {
            Some(mut stream) => {
                let next = stream.next().await;
                if next.is_some() {
                    self.event_stream = Some(stream);
                } else {
                    // Once we've exhausted all the events, drop the stream, which owns the proxy.
                    // TODO(fxbug.dev/87976) - once fxbug.dev/87890 is fixed this is not needed.
                    // The explicit drop isn't strictly necessary, but it's left here to
                    // communicate that we NEED to close the proxy.
                    drop(stream);
                }
                next
            }
            None => None,
        }
    }

    fn url(&self) -> &str {
        &self.url
    }

    fn max_severity_logs(&self) -> Option<Severity> {
        self.max_severity_logs
    }
}

/// Runs the tests in `test_params`, and writes logs to writer.
pub async fn run_test<'a, Out: Write, F: 'a + Future<Output = ()>>(
    builder_proxy: RunBuilderProxy,
    test_params: Vec<TestParams>,
    run_params: RunParams,
    min_severity_logs: Option<Severity>,
    stdout_writer: &'a mut Out,
    run_reporter: &'a mut RunReporter,
    cancel_fut: F,
) -> Result<SuiteResults<'a>, RunTestSuiteError> {
    let mut suite_start_futs = vec![];
    let mut suite_reporters = HashMap::new();
    for (suite_id_raw, params) in test_params.into_iter().enumerate() {
        let timeout: Option<i64> = match params.timeout {
            Some(t) => {
                const NANOS_IN_SEC: u64 = 1_000_000_000;
                let secs: u32 = t.get();
                let nanos: u64 = (secs as u64) * NANOS_IN_SEC;
                // Unwrap okay here as max value (u32::MAX * 1_000_000_000) is 62 bits
                Some(nanos.try_into().unwrap())
            }
            None => None,
        };
        let run_options = SuiteRunOptions {
            parallel: params.parallel,
            arguments: Some(params.test_args),
            run_disabled_tests: Some(params.also_run_disabled_tests),
            timeout,
            test_filters: params.test_filters,
            log_iterator: Some(diagnostics::get_type()),
        };

        let suite_id = SuiteId(suite_id_raw as u32);
        suite_reporters.insert(suite_id, run_reporter.new_suite(&params.test_url, &suite_id)?);
        let (suite_controller, suite_server_end) = fidl::endpoints::create_proxy()?;
        let suite_and_id_fut = RunningSuite::wait_for_start(
            suite_controller,
            params.test_url.clone(),
            params.max_severity_logs,
        )
        .map(move |running_suite| (running_suite, suite_id))
        .boxed();
        suite_start_futs.push(suite_and_id_fut);
        builder_proxy.add_suite(&params.test_url, run_options.into(), suite_server_end)?;
    }
    let (run_controller, run_server_end) = fidl::endpoints::create_proxy()?;
    builder_proxy.build(run_server_end)?;

    struct FoldArgs<'a, Out: Write, F: 'a + Future<Output = ()>> {
        suite_start_futs: Vec<BoxFuture<'a, (RunningSuite, SuiteId)>>,
        run_controller: ftest_manager::RunControllerProxy,
        suite_reporters: HashMap<SuiteId, SuiteReporter<'a>>,
        stdout_writer: &'a mut Out,
        min_severity_logs: Option<Severity>,
        run_params: RunParams,
        num_failed: u16,
        cancel_fut: futures::future::Shared<F>,
    }

    let args = FoldArgs {
        suite_start_futs,
        run_controller,
        suite_reporters,
        stdout_writer,
        min_severity_logs,
        run_params,
        num_failed: 0,
        cancel_fut: cancel_fut.shared(),
    };

    // Handle suite events. Note this assumes that suites are run in serial - it waits to
    // find a suite that is started and drains the events before polling the next one.
    let stream = futures::stream::try_unfold(args, |mut args| async move {
        match args.suite_start_futs.is_empty() {
            false => {
                let ((running_suite, suite_id), _idx, mut unstarted_suites) =
                    futures::future::select_all(args.suite_start_futs).await;
                let suite_reporter = args.suite_reporters.remove(&suite_id).unwrap();

                let log_options = diagnostics::LogCollectionOptions {
                    min_severity: args.min_severity_logs,
                    max_severity: running_suite.max_severity_logs(),
                };

                let result = run_suite_and_collect_logs(
                    running_suite,
                    &suite_reporter,
                    &log_options,
                    args.stdout_writer,
                    args.cancel_fut.clone(),
                )
                .await;
                // We should always persist results, even if something failed.
                suite_reporter.finished()?;
                let result = result?;

                let accumulated_failures = match result.outcome {
                    Outcome::Passed => args.num_failed,
                    _ => args.num_failed + 1,
                };
                let stop_due_to_timeout = match args.run_params.timeout_behavior {
                    TimeoutBehavior::TerminateRemaining => result.outcome == Outcome::Timedout,
                    TimeoutBehavior::Continue => false,
                };
                let stop_due_to_failures = match args.run_params.stop_after_failures.as_ref() {
                    Some(threshold) => accumulated_failures >= threshold.get(),
                    None => false,
                };
                let stop_due_to_cancellation = matches!(&result.outcome, Outcome::Cancelled);

                if stop_due_to_timeout || stop_due_to_failures || stop_due_to_cancellation {
                    args.run_controller.stop()?;
                    // Drop remaining controllers, which is the same as calling kill on
                    // each controller.
                    unstarted_suites = vec![];
                    args.suite_reporters
                        .drain()
                        .try_for_each(|(_id, reporter)| reporter.finished())?;
                }
                let next_args = FoldArgs {
                    suite_start_futs: unstarted_suites,
                    num_failed: accumulated_failures,
                    ..args
                };
                Ok(Some((result, next_args)))
            }
            true => {
                // No suites left to poll.
                loop {
                    let events = args.run_controller.get_events().await?;
                    if events.len() == 0 {
                        break;
                    }
                    println!("WARN: Discarding run events: {:?}", events);
                }
                Ok(None)
            }
        }
    });

    Ok(stream.boxed_local())
}

async fn collect_results(mut stream: SuiteResults<'_>) -> Outcome {
    let mut i: u16 = 1;
    let mut final_outcome = Outcome::Passed;

    loop {
        match stream.try_next().await {
            Err(e) => {
                println!("Test suite encountered error trying to run tests: {}", e);
                return Outcome::error(e);
            }
            Ok(Some(SuiteRunResult {
                url,
                mut outcome,
                executed,
                passed,
                failed,
                restricted_logs,
            })) => {
                println!("\n");
                if !failed.is_empty() {
                    println!("Failed tests: {}", failed.join(", "))
                }
                println!("{} out of {} tests passed...", passed.len(), executed.len());
                match &outcome {
                    Outcome::Cancelled => println!("{} was cancelled before completion.", &url),
                    Outcome::DidNotFinish => {
                        println!("{} did not complete successfully.", &url)
                    }
                    outcome => println!("{} completed with result: {}", &url, outcome),
                }
                if executed.is_empty() {
                    println!("WARN: No test cases were executed!");
                }
                if restricted_logs.len() > 0 {
                    if outcome == Outcome::Passed {
                        outcome = Outcome::Failed;
                    }
                    println!("\nTest {} produced unexpected high-severity logs:", &url);
                    println!("----------------xxxxx----------------");
                    for log in restricted_logs {
                        println!("{}", log);
                    }
                    println!("----------------xxxxx----------------");
                    println!("Failing this test. See: https://fuchsia.dev/fuchsia-src/concepts/testing/logs#restricting_log_severity\n");
                }

                i = i + 1;
                if i > 2 {
                    if outcome != Outcome::Passed {
                        final_outcome = Outcome::Failed;
                    }
                } else {
                    final_outcome = outcome;
                }
            }
            Ok(None) => {
                return final_outcome;
            }
        }
    }
}

/// Runs the test and writes logs to stdout.
/// |count|: Number of times to run this test.
/// |filter_ansi|: Whether or not to filter out ANSI escape sequences from stdout.
pub async fn run_tests_and_get_outcome<F: Future<Output = ()>>(
    builder_proxy: RunBuilderProxy,
    test_params: Vec<TestParams>,
    run_params: RunParams,
    min_severity_logs: Option<Severity>,
    filter_ansi: bool,
    record_directory: Option<PathBuf>,
    cancel_fut: F,
) -> Outcome {
    let mut stdout_for_results: Box<dyn Write + Send + Sync> = match filter_ansi {
        true => Box::new(AnsiFilterWriter::new(io::stdout())),
        false => Box::new(io::stdout()),
    };

    let reporter_res = match record_directory {
        Some(dir) => DirectoryReporter::new(dir).map(|reporter| match filter_ansi {
            true => RunReporter::new_ansi_filtered(reporter),
            false => RunReporter::new(reporter),
        }),
        None => Ok(RunReporter::new(NoopReporter)),
    };
    let mut reporter = match reporter_res {
        Ok(r) => r,
        Err(e) => {
            println!("Test suite encountered error trying to run tests: {:?}", e);
            return Outcome::error(e);
        }
    };

    let result_stream = match run_test(
        builder_proxy,
        test_params,
        run_params,
        min_severity_logs,
        &mut stdout_for_results,
        &mut reporter,
        cancel_fut,
    )
    .await
    {
        Ok(s) => s,
        Err(e) => {
            println!("Encountered error trying to run tests: {}", e);
            return Outcome::error(e);
        }
    };

    let test_outcome = collect_results(result_stream).await;

    if test_outcome != Outcome::Passed {
        println!("One or more test runs failed.");
    }

    let report_result = match reporter.stopped(&test_outcome.clone().into(), Timestamp::Unknown) {
        Ok(()) => reporter.finished(),
        Err(e) => Err(e),
    };
    if let Err(e) = report_result {
        println!("Failed to record results: {:?}", e);
    }

    test_outcome
}

/// Options that apply when executing a test suite.
///
/// For the FIDL equivalent, see [`fidl_fuchsia_test::RunOptions`].
#[derive(Debug, Clone, Default, Eq, PartialEq)]
pub struct SuiteRunOptions {
    /// How to handle tests that were marked disabled/ignored by the developer.
    pub run_disabled_tests: Option<bool>,

    /// Number of test cases to run in parallel.
    pub parallel: Option<u16>,

    /// Arguments passed to tests.
    pub arguments: Option<Vec<String>>,

    /// suite timeout
    pub timeout: Option<i64>,

    /// Test cases to filter and run.
    pub test_filters: Option<Vec<String>>,

    /// Type of log iterator
    pub log_iterator: Option<ftest_manager::LogsIteratorOption>,
}

impl From<SuiteRunOptions> for fidl_fuchsia_test_manager::RunOptions {
    fn from(test_run_options: SuiteRunOptions) -> Self {
        // Note: This will *not* break if new members are added to the FIDL table.
        fidl_fuchsia_test_manager::RunOptions {
            parallel: test_run_options.parallel,
            arguments: test_run_options.arguments,
            run_disabled_tests: test_run_options.run_disabled_tests,
            timeout: test_run_options.timeout,
            case_filters_to_run: test_run_options.test_filters,
            log_iterator: test_run_options.log_iterator,
            ..fidl_fuchsia_test_manager::RunOptions::EMPTY
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use fidl::endpoints::create_proxy_and_stream;
    use matches::assert_matches;

    const TEST_URL: &str = "test.cm";

    async fn respond_to_get_events(
        request_stream: &mut ftest_manager::SuiteControllerRequestStream,
        events: Vec<ftest_manager::SuiteEvent>,
    ) {
        let request = request_stream
            .next()
            .await
            .expect("did not get next request")
            .expect("error getting next request");
        let responder = match request {
            ftest_manager::SuiteControllerRequest::GetEvents { responder } => responder,
            r => panic!("Expected GetEvents request but got {:?}", r),
        };

        responder.send(&mut Ok(events)).expect("send events");
    }

    /// Creates a SuiteEvent which is unpopulated, except for timestamp.
    /// This isn't representative of an actual event from test framework, but is sufficient
    /// to assert events are routed correctly.
    fn create_empty_event(timestamp: i64) -> ftest_manager::SuiteEvent {
        ftest_manager::SuiteEvent { timestamp: Some(timestamp), ..ftest_manager::SuiteEvent::EMPTY }
    }

    macro_rules! assert_empty_events_eq {
        ($t1:expr, $t2:expr) => {
            assert_eq!($t1.timestamp, $t2.timestamp, "Got incorrect event.")
        };
    }

    #[fuchsia::test]
    async fn running_suite_events_simple() {
        let (suite_proxy, mut suite_request_stream) =
            create_proxy_and_stream::<ftest_manager::SuiteControllerMarker>()
                .expect("create proxy");
        let suite_server_task = fasync::Task::spawn(async move {
            respond_to_get_events(&mut suite_request_stream, vec![create_empty_event(0)]).await;
            respond_to_get_events(&mut suite_request_stream, vec![]).await;
            drop(suite_request_stream);
        });

        let mut running_suite =
            RunningSuite::wait_for_start(suite_proxy, TEST_URL.to_string(), None).await;
        assert_empty_events_eq!(
            running_suite.next_event().await.unwrap().unwrap(),
            create_empty_event(0)
        );
        assert!(running_suite.next_event().await.is_none());
        // polling again should still give none.
        assert!(running_suite.next_event().await.is_none());
        suite_server_task.await;
    }

    #[fuchsia::test]
    async fn running_suite_events_multiple_events() {
        let (suite_proxy, mut suite_request_stream) =
            create_proxy_and_stream::<ftest_manager::SuiteControllerMarker>()
                .expect("create proxy");
        let suite_server_task = fasync::Task::spawn(async move {
            respond_to_get_events(
                &mut suite_request_stream,
                vec![create_empty_event(0), create_empty_event(1)],
            )
            .await;
            respond_to_get_events(
                &mut suite_request_stream,
                vec![create_empty_event(2), create_empty_event(3)],
            )
            .await;
            respond_to_get_events(&mut suite_request_stream, vec![]).await;
            drop(suite_request_stream);
        });

        let mut running_suite =
            RunningSuite::wait_for_start(suite_proxy, TEST_URL.to_string(), None).await;

        for num in 0..4 {
            assert_empty_events_eq!(
                running_suite.next_event().await.unwrap().unwrap(),
                create_empty_event(num)
            );
        }
        assert!(running_suite.next_event().await.is_none());
        suite_server_task.await;
    }

    #[fuchsia::test]
    async fn running_suite_events_peer_closed() {
        let (suite_proxy, mut suite_request_stream) =
            create_proxy_and_stream::<ftest_manager::SuiteControllerMarker>()
                .expect("create proxy");
        let suite_server_task = fasync::Task::spawn(async move {
            respond_to_get_events(&mut suite_request_stream, vec![create_empty_event(1)]).await;
            drop(suite_request_stream);
        });

        let mut running_suite =
            RunningSuite::wait_for_start(suite_proxy, TEST_URL.to_string(), None).await;
        assert_empty_events_eq!(
            running_suite.next_event().await.unwrap().unwrap(),
            create_empty_event(1)
        );
        assert_matches!(
            running_suite.next_event().await,
            Some(Err(RunTestSuiteError::Fidl(fidl::Error::ClientChannelClosed { .. })))
        );
        suite_server_task.await;
    }
}
