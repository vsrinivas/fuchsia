// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context},
    diagnostics_data::Severity,
    fidl::Peered,
    fidl_fuchsia_test_manager::{
        self as ftest_manager, CaseArtifact, CaseFinished, CaseFound, CaseStarted, CaseStopped,
        RunBuilderProxy, SuiteArtifact, SuiteStopped,
    },
    fuchsia_async as fasync,
    futures::{channel::mpsc, future::join_all, prelude::*, stream::FuturesUnordered, StreamExt},
    log::{debug, error, warn},
    std::collections::{HashMap, HashSet, VecDeque},
    std::convert::TryInto,
    std::fmt,
    std::io::Write,
    std::path::PathBuf,
    std::sync::Arc,
};

mod artifact;
mod cancel;
pub mod diagnostics;
mod error;
pub mod output;
mod stream_util;

pub use error::{RunTestSuiteError, UnexpectedEventError};
use {
    artifact::Artifact,
    cancel::{Cancelled, OrCancel},
    output::{
        ArtifactType, CaseId, DirectoryArtifactType, RunReporter, SuiteId, SuiteReporter, Timestamp,
    },
    stream_util::StreamUtil,
};

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

/// Parameters that specify how a single test suite should be executed.
#[derive(Clone, Debug, PartialEq, Default)]
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

/// Collects results and artifacts for a single suite.
// TODO(satsukiu): There's two ways to return an error here:
// * Err(RunTestSuiteError)
// * Ok(Outcome::Error(RunTestSuiteError))
// We should consider how to consolidate these.
async fn run_suite_and_collect_logs<F: Future<Output = ()> + Unpin>(
    mut running_suite: RunningSuite,
    suite_reporter: &SuiteReporter<'_>,
    log_opts: diagnostics::LogCollectionOptions,
    cancel_fut: F,
) -> Result<Outcome, RunTestSuiteError> {
    let mut test_cases = HashMap::new();
    let mut test_case_reporters = HashMap::new();
    let mut test_cases_in_progress = HashSet::new();
    let mut test_cases_output = HashMap::new();
    let mut suite_log_tasks = vec![];
    let mut suite_finish_timestamp = Timestamp::Unknown;
    let mut outcome = Outcome::Passed;
    let mut successful_completion = false;
    let mut cancelled = false;
    let mut tasks = vec![];

    let colect_results_fut = async {
        while let Some(event_result) = running_suite.next_event().await {
            match event_result {
                Err(e) => {
                    suite_reporter
                        .stopped(&output::ReportedOutcome::Error, Timestamp::Unknown)
                        .await?;
                    return Err(e);
                }
                Ok(event) => {
                    let timestamp = Timestamp::from_nanos(event.timestamp);
                    match event.payload.expect("event cannot be None") {
                        ftest_manager::SuiteEventPayload::CaseFound(CaseFound {
                            test_case_name,
                            identifier,
                        }) => {
                            let case_reporter = suite_reporter
                                .new_case(&test_case_name, &CaseId(identifier))
                                .await?;
                            test_cases.insert(identifier, test_case_name);
                            test_case_reporters.insert(identifier, case_reporter);
                        }
                        ftest_manager::SuiteEventPayload::CaseStarted(CaseStarted {
                            identifier,
                        }) => {
                            let test_case_name = test_cases
                                .get(&identifier)
                                .ok_or(UnexpectedEventError::CaseStartedButNotFound { identifier })?
                                .clone();
                            let reporter = test_case_reporters.get(&identifier).unwrap();
                            // TODO(fxbug.dev/79712): Record per-case runtime once we have an
                            // accurate way to measure it.
                            reporter.started(Timestamp::Unknown).await?;
                            if test_cases_in_progress.contains(&identifier) {
                                return Err(UnexpectedEventError::CaseStartedTwice {
                                    test_case_name,
                                    identifier,
                                }
                                .into());
                            };
                            test_cases_in_progress.insert(identifier);
                        }
                        ftest_manager::SuiteEventPayload::CaseArtifact(CaseArtifact {
                            identifier,
                            artifact,
                        }) => match artifact {
                            ftest_manager::Artifact::Stdout(socket) => {
                                let (sender, mut recv) = mpsc::channel(1024);
                                let t = fuchsia_async::Task::spawn(
                                    // TODO - this does some buffering that doesn't need to apply to
                                    // the directory reporter, probably move that logic to the live reporter
                                    test_diagnostics::collect_and_send_string_output(
                                        socket, sender,
                                    ),
                                );
                                let reporter = test_case_reporters.get(&identifier).unwrap();
                                let mut stdout =
                                    reporter.new_artifact(&ArtifactType::Stdout).await?;
                                let stdout_task = fuchsia_async::Task::spawn(async move {
                                    while let Some(msg) = recv.next().await {
                                        stdout.write_all(msg.as_bytes())?;
                                    }
                                    stdout.flush()?;
                                    t.await?;
                                    Result::Ok::<(), anyhow::Error>(())
                                });
                                test_cases_output
                                    .entry(identifier)
                                    .or_insert(vec![])
                                    .push(stdout_task);
                            }
                            ftest_manager::Artifact::Stderr(socket) => {
                                let (sender, mut recv) = mpsc::channel(1024);
                                let t = fuchsia_async::Task::spawn(
                                    test_diagnostics::collect_and_send_string_output(
                                        socket, sender,
                                    ),
                                );
                                let reporter = test_case_reporters.get_mut(&identifier).unwrap();
                                let mut stderr =
                                    reporter.new_artifact(&ArtifactType::Stderr).await?;
                                let stderr_task = fuchsia_async::Task::spawn(async move {
                                    while let Some(msg) = recv.next().await {
                                        stderr.write_all(msg.as_bytes())?;
                                    }
                                    stderr.flush()?;
                                    t.await?;
                                    Result::Ok::<(), anyhow::Error>(())
                                });
                                test_cases_output
                                    .entry(identifier)
                                    .or_insert(vec![])
                                    .push(stderr_task);
                            }
                            ftest_manager::Artifact::Log(_) => {
                                warn!("WARN: per test case logs not supported yet")
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
                            let test_case_name = test_cases.get(&identifier).ok_or(
                                UnexpectedEventError::CaseStoppedButNotFound { identifier },
                            )?;
                            // when a case errors out we handle it as if it never completed
                            if status != ftest_manager::CaseStatus::Error {
                                test_cases_in_progress.remove(&identifier);
                            }
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
                            let reporter = test_case_reporters.get(&identifier).unwrap();
                            // TODO(fxbug.dev/79712): Record per-case runtime once we have an
                            // accurate way to measure it.
                            reporter.stopped(&status.into(), Timestamp::Unknown).await?;
                        }
                        ftest_manager::SuiteEventPayload::CaseFinished(CaseFinished {
                            identifier,
                        }) => {
                            let reporter = test_case_reporters.remove(&identifier).unwrap();
                            reporter.finished().await?;
                        }
                        ftest_manager::SuiteEventPayload::SuiteArtifact(SuiteArtifact {
                            artifact,
                        }) => {
                            match artifact {
                                ftest_manager::Artifact::Stdout(_) => {
                                    warn!("suite level stdout not supported yet");
                                }
                                ftest_manager::Artifact::Stderr(_) => {
                                    warn!("suite level stderr not supported yet");
                                }
                                ftest_manager::Artifact::Log(syslog) => {
                                    let log_stream =
                                        test_diagnostics::LogStream::from_syslog(syslog)?;
                                    let mut log_artifact =
                                        suite_reporter.new_artifact(&ArtifactType::Syslog).await?;
                                    let log_opts_clone = log_opts.clone();
                                    suite_log_tasks.push(fuchsia_async::Task::spawn(async move {
                                        let (send, mut recv) = mpsc::channel(32);
                                        let fut_1 = diagnostics::collect_logs(
                                            log_stream,
                                            send.into(),
                                            log_opts_clone,
                                        );
                                        let fut_2 = async move {
                                            while let Some(artifact) = recv.next().await {
                                                let artifact_string = match artifact {
                                                    Artifact::SuiteLogMessage(s) => s,
                                                };
                                                writeln!(log_artifact, "{}", artifact_string)?;
                                            }
                                            Result::<_, std::io::Error>::Ok(())
                                        };
                                        let (outcome, _) =
                                            futures::future::join(fut_1, fut_2).await;
                                        outcome
                                    }));
                                }
                                ftest_manager::Artifact::Custom(
                                    ftest_manager::CustomArtifact {
                                        directory_and_token,
                                        component_moniker,
                                        ..
                                    },
                                ) => {
                                    if let Some(ftest_manager::DirectoryAndToken {
                                        directory,
                                        token,
                                        ..
                                    }) = directory_and_token
                                    {
                                        let directory = directory.into_proxy()?;
                                        let directory_artifact = suite_reporter
                                            .new_directory_artifact(
                                                &DirectoryArtifactType::Custom,
                                                component_moniker,
                                            )
                                            .await?;

                                        tasks.push(fasync::Task::spawn(async move {
                                            if let Err(e) = read_custom_artifact_directory(
                                                directory,
                                                directory_artifact,
                                            )
                                            .await
                                            {
                                                warn!(
                                                    "Error reading suite artifact directory: {:?}",
                                                    e
                                                );
                                            }
                                            // TODO(fxbug.dev/84882): Remove this signal once Overnet
                                            // supports automatically signalling EVENTPAIR_CLOSED when the
                                            // handle is closed.
                                            let _ = token.signal_peer(
                                                fidl::Signals::empty(),
                                                fidl::Signals::USER_0,
                                            );
                                        }));
                                    }
                                }
                                ftest_manager::ArtifactUnknown!() => {
                                    panic!("unknown artifact")
                                }
                            }
                        }
                        ftest_manager::SuiteEventPayload::SuiteStarted(_) => {
                            suite_reporter.started(timestamp).await?;
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
                    diagnostics::LogCollectionOutcome::Error { restricted_logs } => {
                        let mut log_artifact =
                            suite_reporter.new_artifact(&ArtifactType::RestrictedLog).await?;
                        for log in restricted_logs.iter() {
                            writeln!(log_artifact, "{}", log)?;
                        }
                        if Outcome::Passed == outcome {
                            outcome = Outcome::Failed;
                        }
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
        Ok(())
    };

    match colect_results_fut.boxed_local().or_cancelled(cancel_fut).await {
        Ok(Ok(())) => (),
        Ok(Err(e)) => return Err(e),
        Err(Cancelled) => {
            cancelled = true;
        }
    }

    // TODO(fxbug.dev/90037) - unless the suite was cancelled, this indicates an internal error.
    // Test manager should always report CaseFinished before terminating the event stream.
    if !test_cases_in_progress.is_empty() {
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
    }
    if cancelled {
        match outcome {
            Outcome::Passed | Outcome::Failed => {
                outcome = Outcome::Cancelled;
            }
            _ => {}
        }
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

    suite_reporter.stopped(&outcome.clone().into(), suite_finish_timestamp).await?;

    Ok(outcome)
}

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

/// Schedule and run the tests specified in |test_params|, and collect the results.
/// Note this currently doesn't record the result or call finished() on run_reporter,
/// the caller should do this instead.
async fn run_tests<'a, F: 'a + Future<Output = ()> + Unpin>(
    builder_proxy: RunBuilderProxy,
    test_params: Vec<TestParams>,
    run_params: RunParams,
    min_severity_logs: Option<Severity>,
    run_reporter: &'a RunReporter,
    cancel_fut: F,
) -> Result<Outcome, RunTestSuiteError> {
    let mut suite_start_futs = FuturesUnordered::new();
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
        suite_reporters
            .insert(suite_id, run_reporter.new_suite(&params.test_url, &suite_id).await?);
        let (suite_controller, suite_server_end) = fidl::endpoints::create_proxy()?;
        let suite_and_id_fut = RunningSuite::wait_for_start(
            suite_controller,
            params.test_url.clone(),
            params.max_severity_logs,
        )
        .map(move |running_suite| (running_suite, suite_id));
        suite_start_futs.push(suite_and_id_fut);
        builder_proxy.add_suite(&params.test_url, run_options.into(), suite_server_end)?;
    }
    let (run_controller, run_server_end) = fidl::endpoints::create_proxy()?;
    let run_controller_ref = &run_controller;
    builder_proxy.build(run_server_end)?;
    run_reporter.started(Timestamp::Unknown).await?;
    let cancel_fut = cancel_fut.shared();
    let cancel_fut_clone = cancel_fut.clone();

    let handle_suite_fut = async move {
        let mut num_failed = 0;
        let mut final_outcome = None;
        let mut stopped_prematurely = false;
        // for now, we assume that suites are run serially.
        loop {
            let (running_suite, suite_id) =
                match suite_start_futs.next().or_cancelled(cancel_fut.clone()).await {
                    Ok(Some((running_suite, suite_id))) => (running_suite, suite_id),
                    // normal completion.
                    Ok(None) => break,
                    Err(Cancelled) => {
                        stopped_prematurely = true;
                        final_outcome = Some(Outcome::Cancelled);
                        break;
                    }
                };

            let suite_reporter = suite_reporters.remove(&suite_id).unwrap();

            let log_options = diagnostics::LogCollectionOptions {
                min_severity: min_severity_logs,
                max_severity: running_suite.max_severity_logs(),
            };

            let result = run_suite_and_collect_logs(
                running_suite,
                &suite_reporter,
                log_options.clone(),
                cancel_fut.clone(),
            )
            .await;
            let suite_outcome = result.unwrap_or_else(|err| Outcome::error(err));
            // We should always persist results, even if something failed.
            suite_reporter.finished().await?;

            num_failed = match suite_outcome {
                Outcome::Passed => num_failed,
                _ => num_failed + 1,
            };
            let stop_due_to_timeout = match run_params.timeout_behavior {
                TimeoutBehavior::TerminateRemaining => suite_outcome == Outcome::Timedout,
                TimeoutBehavior::Continue => false,
            };
            let stop_due_to_failures = match run_params.stop_after_failures.as_ref() {
                Some(threshold) => num_failed >= threshold.get(),
                None => false,
            };
            let stop_due_to_cancellation = matches!(&suite_outcome, Outcome::Cancelled);
            let stop_due_to_internal_error = match &suite_outcome {
                Outcome::Error { origin } => origin.is_internal_error(),
                _ => false,
            };

            final_outcome = match (final_outcome.take(), suite_outcome) {
                (None, first_outcome) => Some(first_outcome),
                (Some(outcome), Outcome::Passed) => Some(outcome),
                (Some(_), failing_outcome) => Some(failing_outcome),
            };
            if stop_due_to_timeout
                || stop_due_to_failures
                || stop_due_to_cancellation
                || stop_due_to_internal_error
            {
                stopped_prematurely = true;
                break;
            }
        }
        if stopped_prematurely {
            // Ignore errors here since we're stopping anyway.
            let _ = run_controller_ref.stop();
            // Drop remaining controllers, which is the same as calling kill on
            // each controller.
            suite_start_futs.clear();
            for (_id, reporter) in suite_reporters.drain() {
                reporter.finished().await?;
            }
        }
        Ok(final_outcome.unwrap_or(Outcome::Passed))
    };

    let handle_run_events_fut = async move {
        loop {
            let events = run_controller_ref.get_events().await?;
            if events.len() == 0 {
                return Ok(());
            }

            for event in events.into_iter() {
                let ftest_manager::RunEvent { payload, .. } = event;
                match payload {
                    // TODO(fxbug.dev/91151): Add support for RunStarted and RunStopped when test_manager sends them.
                    Some(ftest_manager::RunEventPayload::Artifact(artifact)) => {
                        match artifact {
                            ftest_manager::Artifact::DebugData(iterator) => {
                                let iterator = iterator.into_proxy().unwrap();
                                let output_directory = run_reporter
                                    .new_directory_artifact(
                                        &DirectoryArtifactType::Debug,
                                        None, /* moniker */
                                    )
                                    .await?;

                                const PIPELINED_REQUESTS: usize = 16;
                                let unprocessed_data_stream =
                                    futures::stream::repeat_with(move || iterator.get_next())
                                        .buffered(PIPELINED_REQUESTS);
                                let terminated_event_stream = unprocessed_data_stream
                                    .take_until_stop_after(|result| match &result {
                                        Ok(events) => events.is_empty(),
                                        _ => true,
                                    });

                                let data_futs = terminated_event_stream
                                    .map(|result| match result {
                                        Ok(vals) => vals,
                                        Err(e) => {
                                            warn!("Request failure: {:?}", e);
                                            vec![]
                                        }
                                    })
                                    .map(futures::stream::iter)
                                    .flatten()
                                    .map(|debug_data| {
                                        let output = debug_data
                                            .name
                                            .as_ref()
                                            .ok_or_else(|| anyhow!("Missing profile name"))
                                            .and_then(|name| {
                                                output_directory
                                                    .new_file(&PathBuf::from(name))
                                                    .map_err(anyhow::Error::from)
                                            });
                                        async move {
                                            let mut output = output?;
                                            let file = debug_data
                                                .file
                                                .ok_or_else(|| {
                                                    anyhow!("Missing profile file handle")
                                                })?
                                                .into_proxy()?;
                                            debug!(
                                                "Reading run profile \"{}\"",
                                                debug_data.name.unwrap_or_default()
                                            );
                                            read_file_to_writer(&file, &mut output).await
                                        }
                                    })
                                    .collect::<Vec<_>>()
                                    .await;
                                join_all(data_futs).await;
                                debug!("All profiles downloaded");
                            }
                            ftest_manager::Artifact::Custom(val) => {
                                if let Some(ftest_manager::DirectoryAndToken {
                                    directory,
                                    token,
                                    ..
                                }) = val.directory_and_token
                                {
                                    let directory = directory.into_proxy()?;
                                    let out_dir = run_reporter
                                        .new_directory_artifact(
                                            &output::DirectoryArtifactType::Custom,
                                            None, /* moniker */
                                        )
                                        .await?;

                                    if let Err(e) =
                                        read_custom_artifact_directory(directory, out_dir).await
                                    {
                                        warn!("Error reading run artifact directory: {:?}", e);
                                    }

                                    // TODO(fxbug.dev/84882): Remove this signal once Overnet
                                    // supports automatically signalling EVENTPAIR_CLOSED when the
                                    // handle is closed.
                                    let _ = token
                                        .signal_peer(fidl::Signals::empty(), fidl::Signals::USER_0);
                                }
                            }
                            other => {
                                warn!("Discarding run artifact: {:?}", other);
                            }
                        }
                    }
                    e => {
                        warn!("Discarding run event: {:?}", e);
                    }
                }
            }
        }
    };

    // Make sure we stop polling run events on cancel. Since cancellation is expected
    // ignore cancellation errors.
    let cancellable_run_events_fut = handle_run_events_fut
        .boxed_local()
        .or_cancelled(cancel_fut_clone)
        .map(|cancelled_result| match cancelled_result {
            Ok(completed_result) => completed_result,
            Err(Cancelled) => Ok(()),
        });

    // Use join instead of try_join as we want to poll the futures to completion
    // even if one fails.
    match futures::future::join(handle_suite_fut, cancellable_run_events_fut).await {
        (Ok(outcome), Ok(())) => Ok(outcome),
        (Err(e), _) | (_, Err(e)) => Err(e),
    }
}

async fn read_custom_artifact_directory(
    directory: fidl_fuchsia_io::DirectoryProxy,
    out_dir: Box<output::DynDirectoryArtifact>,
) -> Result<(), anyhow::Error> {
    let mut paths = vec![];
    let mut enumerate = files_async::readdir_recursive(&directory, None);
    while let Ok(Some(file)) = enumerate.try_next().await {
        if file.kind == files_async::DirentKind::File {
            paths.push(file.name);
        }
    }

    let futs = FuturesUnordered::new();
    paths.iter().for_each(|path| {
        let path = std::path::PathBuf::from(path);
        let file = io_util::open_file(&directory, &path, io_util::OPEN_RIGHT_READABLE);
        let output_file = out_dir.new_file(&path);
        futs.push(async move {
            let file = file.with_context(|| format!("with path {:?}", path))?;
            let mut output_file = output_file?;
            read_file_to_writer(&file, &mut output_file).await
        });
    });

    futs.for_each(|result| {
        if let Err(e) = result {
            warn!("Custom artifact failure: {}", e);
        }
        async move {}
    })
    .await;

    Ok(())
}

async fn read_file_to_writer<T: Write>(
    file: &fidl_fuchsia_io::FileProxy,
    output: &mut T,
) -> Result<(), anyhow::Error> {
    const READ_SIZE: u64 = fidl_fuchsia_io::MAX_BUF;

    let mut vector = VecDeque::new();
    // Arbitrary number of reads to pipeline. Works for
    // a bandwidth delay product of 800kB.
    const PIPELINED_READ_COUNT: u64 = 100;
    for _n in 0..PIPELINED_READ_COUNT {
        vector.push_back(file.read(READ_SIZE));
    }
    loop {
        let mut buf =
            vector.pop_front().unwrap().await?.map_err(fuchsia_zircon_status::Status::from_raw)?;
        if buf.is_empty() {
            break;
        }
        output.write_all(&mut buf)?;
        vector.push_back(file.read(READ_SIZE));
    }
    Ok(())
}

/// Runs tests specified in |test_params| and reports the results to
/// |run_reporter|.
///
/// Options specifying how the test run is executed are passed in via |run_params|.
/// Options specific to how a single suite is run are passed in via the entry for
/// the suite in |test_params|.
/// |cancel_fut| is used to gracefully stop execution of tests. Tests are
/// terminated and recorded when the future resolves. The caller can control when the
/// future resolves by passing in the receiver end of a `future::channel::oneshot`
/// channel.
/// |min_severity_logs| specifies the minimum log severity to report. As it is an
/// option for output, it will likely soon be moved to a reporter.
pub async fn run_tests_and_get_outcome<F: Future<Output = ()>>(
    builder_proxy: RunBuilderProxy,
    test_params: Vec<TestParams>,
    run_params: RunParams,
    min_severity_logs: Option<Severity>,
    run_reporter: RunReporter,
    cancel_fut: F,
) -> Outcome {
    let test_outcome = match run_tests(
        builder_proxy,
        test_params,
        run_params,
        min_severity_logs,
        &run_reporter,
        cancel_fut.boxed_local(),
    )
    .await
    {
        Ok(s) => s,
        Err(e) => {
            println!("Encountered error trying to run tests: {}", e);
            return Outcome::error(e);
        }
    };

    if test_outcome != Outcome::Passed {
        println!("One or more test runs failed.");
    }

    let report_result =
        match run_reporter.stopped(&test_outcome.clone().into(), Timestamp::Unknown).await {
            Ok(()) => run_reporter.finished().await,
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
    use crate::output::InMemoryReporter;
    use assert_matches::assert_matches;
    use fidl::endpoints::{create_proxy_and_stream, ServerEnd};
    use fuchsia_zircon as zx;
    use futures::future::{join, join3};
    use maplit::hashmap;
    use output::EntityId;
    use vfs::{
        directory::entry::DirectoryEntry, execution_scope::ExecutionScope,
        file::vmo::read_only_static, pseudo_directory,
    };

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

    async fn fake_running_all_suites_and_return_run_events(
        mut stream: ftest_manager::RunBuilderRequestStream,
        mut suite_events: HashMap<&str, Vec<ftest_manager::SuiteEvent>>,
        run_events: Vec<ftest_manager::RunEvent>,
    ) {
        let mut suite_streams = vec![];

        let mut run_controller = None;
        while let Ok(Some(req)) = stream.try_next().await {
            match req {
                ftest_manager::RunBuilderRequest::AddSuite { test_url, controller, .. } => {
                    let events = suite_events
                        .remove(test_url.as_str())
                        .expect("Got a request for an unexpected test URL");
                    suite_streams.push((controller.into_stream().expect("into stream"), events));
                }
                ftest_manager::RunBuilderRequest::Build { controller, .. } => {
                    run_controller = Some(controller);
                    break;
                }
            }
        }
        assert!(
            run_controller.is_some(),
            "Expected a RunController to be present. RunBuilder/Build() may not have been called."
        );
        assert!(suite_events.is_empty(), "Expected AddSuite to be called for all specified suites");
        let mut run_stream =
            run_controller.expect("controller present").into_stream().expect("into stream");

        // Each suite just reports that it started and passed.
        let mut suite_streams = suite_streams
            .into_iter()
            .map(|(mut stream, events)| {
                async move {
                    let mut maybe_events = Some(events);
                    while let Ok(Some(req)) = stream.try_next().await {
                        match req {
                            ftest_manager::SuiteControllerRequest::GetEvents {
                                responder, ..
                            } => {
                                let send_events = maybe_events.take().unwrap_or(vec![]);
                                let _ = responder.send(&mut Ok(send_events));
                            }
                            _ => {
                                // ignore all other requests
                            }
                        }
                    }
                }
            })
            .collect::<FuturesUnordered<_>>();

        let suite_drain_fut = async move { while let Some(_) = suite_streams.next().await {} };

        let run_fut = async move {
            let mut events = Some(run_events);
            while let Ok(Some(req)) = run_stream.try_next().await {
                match req {
                    ftest_manager::RunControllerRequest::GetEvents { responder, .. } => {
                        if events.is_none() {
                            let _ = responder.send(&mut vec![].into_iter());
                            continue;
                        }
                        let events = events.take().unwrap();
                        let _ = responder.send(&mut events.into_iter());
                    }
                    _ => {
                        // ignore all other requests
                    }
                }
            }
        };

        join(suite_drain_fut, run_fut).await;
    }

    struct ParamsForRunTests {
        builder_proxy: ftest_manager::RunBuilderProxy,
        test_params: Vec<TestParams>,
        run_reporter: RunReporter,
    }

    fn create_empty_suite_events() -> Vec<ftest_manager::SuiteEvent> {
        vec![
            ftest_manager::SuiteEvent {
                timestamp: Some(1000),
                payload: Some(ftest_manager::SuiteEventPayload::SuiteStarted(
                    ftest_manager::SuiteStarted,
                )),
                ..ftest_manager::SuiteEvent::EMPTY
            },
            ftest_manager::SuiteEvent {
                timestamp: Some(2000),
                payload: Some(ftest_manager::SuiteEventPayload::SuiteStopped(
                    ftest_manager::SuiteStopped { status: ftest_manager::SuiteStatus::Passed },
                )),
                ..ftest_manager::SuiteEvent::EMPTY
            },
        ]
    }

    async fn call_run_tests(params: ParamsForRunTests) -> Outcome {
        run_tests_and_get_outcome(
            params.builder_proxy,
            params.test_params,
            RunParams { timeout_behavior: TimeoutBehavior::Continue, stop_after_failures: None },
            None,
            params.run_reporter,
            futures::future::pending(),
        )
        .await
    }

    #[fuchsia::test]
    async fn empty_run_no_events() {
        let (builder_proxy, run_builder_stream) =
            create_proxy_and_stream::<ftest_manager::RunBuilderMarker>()
                .expect("create builder proxy");

        let reporter = InMemoryReporter::new();
        let run_reporter = RunReporter::new(reporter.clone());
        let run_fut =
            call_run_tests(ParamsForRunTests { builder_proxy, test_params: vec![], run_reporter });
        let fake_fut =
            fake_running_all_suites_and_return_run_events(run_builder_stream, hashmap! {}, vec![]);

        assert_eq!(join(run_fut, fake_fut).await.0, Outcome::Passed,);

        let reports = reporter.get_reports();
        assert_eq!(1usize, reports.len());
        assert_eq!(reports[0].id, EntityId::TestRun);
    }

    #[fuchsia::test]
    async fn single_run_no_events() {
        let (builder_proxy, run_builder_stream) =
            create_proxy_and_stream::<ftest_manager::RunBuilderMarker>()
                .expect("create builder proxy");

        let reporter = InMemoryReporter::new();
        let run_reporter = RunReporter::new(reporter.clone());
        let run_fut = call_run_tests(ParamsForRunTests {
            builder_proxy,
            test_params: vec![TestParams {
                test_url: "fuchsia-pkg://fuchsia.com/nothing#meta/nothing.cm".to_string(),
                ..TestParams::default()
            }],
            run_reporter,
        });
        let fake_fut = fake_running_all_suites_and_return_run_events(
            run_builder_stream,
            hashmap! {
                "fuchsia-pkg://fuchsia.com/nothing#meta/nothing.cm" => create_empty_suite_events()
            },
            vec![],
        );

        assert_eq!(join(run_fut, fake_fut).await.0, Outcome::Passed,);

        let reports = reporter.get_reports();
        assert_eq!(2usize, reports.len());
        assert!(reports[0].report.artifacts.is_empty());
        assert!(reports[0].report.directories.is_empty());
        assert!(reports[1].report.artifacts.is_empty());
        assert!(reports[1].report.directories.is_empty());
    }

    #[fuchsia::test]
    async fn single_run_custom_directory() {
        let (builder_proxy, run_builder_stream) =
            create_proxy_and_stream::<ftest_manager::RunBuilderMarker>()
                .expect("create builder proxy");

        let reporter = InMemoryReporter::new();
        let run_reporter = RunReporter::new(reporter.clone());
        let run_fut = call_run_tests(ParamsForRunTests {
            builder_proxy,
            test_params: vec![TestParams {
                test_url: "fuchsia-pkg://fuchsia.com/nothing#meta/nothing.cm".to_string(),
                ..TestParams::default()
            }],
            run_reporter,
        });

        let dir = pseudo_directory! {
            "test_file.txt" => read_only_static("Hello, World!"),
        };

        let (directory_client, directory_service) =
            fidl::endpoints::create_endpoints::<fidl_fuchsia_io::DirectoryMarker>().unwrap();
        let scope = ExecutionScope::new();
        dir.open(
            scope,
            fidl_fuchsia_io::OPEN_RIGHT_READABLE | fidl_fuchsia_io::OPEN_RIGHT_WRITABLE,
            fidl_fuchsia_io::MODE_TYPE_DIRECTORY,
            vfs::path::Path::dot(),
            ServerEnd::new(directory_service.into_channel()),
        );

        let (_pair_1, pair_2) = zx::EventPair::create().unwrap();

        let events = vec![ftest_manager::RunEvent {
            payload: Some(ftest_manager::RunEventPayload::Artifact(
                ftest_manager::Artifact::Custom(ftest_manager::CustomArtifact {
                    directory_and_token: Some(ftest_manager::DirectoryAndToken {
                        directory: directory_client,
                        token: pair_2,
                    }),
                    ..ftest_manager::CustomArtifact::EMPTY
                }),
            )),
            ..ftest_manager::RunEvent::EMPTY
        }];

        let fake_fut = fake_running_all_suites_and_return_run_events(
            run_builder_stream,
            hashmap! {
                "fuchsia-pkg://fuchsia.com/nothing#meta/nothing.cm" => create_empty_suite_events()
            },
            events,
        );

        assert_eq!(join(run_fut, fake_fut).await.0, Outcome::Passed,);

        let reports = reporter.get_reports();
        assert_eq!(2usize, reports.len());
        let run = reports.iter().find(|e| e.id == EntityId::TestRun).expect("find run report");
        assert_eq!(1usize, run.report.directories.len());
        let dir = &run.report.directories[0];
        let files = dir.1.files.lock();
        assert_eq!(1usize, files.len());
        let (name, file) = &files[0];
        assert_eq!(name.to_string_lossy(), "test_file.txt".to_string());
        assert_eq!(file.get_contents(), b"Hello, World!");
    }

    #[fuchsia::test]
    async fn record_output_after_internal_error() {
        let (builder_proxy, run_builder_stream) =
            create_proxy_and_stream::<ftest_manager::RunBuilderMarker>()
                .expect("create builder proxy");

        let reporter = InMemoryReporter::new();
        let run_reporter = RunReporter::new(reporter.clone());
        let run_fut = call_run_tests(ParamsForRunTests {
            builder_proxy,
            test_params: vec![
                TestParams {
                    test_url: "fuchsia-pkg://fuchsia.com/invalid#meta/invalid.cm".to_string(),
                    ..TestParams::default()
                },
                TestParams {
                    test_url: "fuchsia-pkg://fuchsia.com/nothing#meta/nothing.cm".to_string(),
                    ..TestParams::default()
                },
            ],
            run_reporter,
        });

        let fake_fut = fake_running_all_suites_and_return_run_events(
            run_builder_stream,
            hashmap! {
                // return an internal error from the first test.
                "fuchsia-pkg://fuchsia.com/invalid#meta/invalid.cm" => vec![
                    ftest_manager::SuiteEvent {
                        timestamp: Some(1000),
                        payload: Some(
                            ftest_manager::SuiteEventPayload::SuiteStarted(
                                ftest_manager::SuiteStarted,
                            ),
                        ),
                        ..ftest_manager::SuiteEvent::EMPTY
                    },
                    ftest_manager::SuiteEvent {
                        timestamp: Some(2000),
                        payload: Some(ftest_manager::SuiteEventPayload::SuiteStopped(
                            ftest_manager::SuiteStopped { status: ftest_manager::SuiteStatus::InternalError },
                        )),
                        ..ftest_manager::SuiteEvent::EMPTY
                    },
                ],
                "fuchsia-pkg://fuchsia.com/nothing#meta/nothing.cm" => create_empty_suite_events()
            },
            vec![],
        );

        assert_matches!(join(run_fut, fake_fut).await.0, Outcome::Error { .. });

        let reports = reporter.get_reports();
        assert_eq!(3usize, reports.len());
        let invalid_suite = reports
            .iter()
            .find(|e| e.report.name == "fuchsia-pkg://fuchsia.com/invalid#meta/invalid.cm")
            .expect("find run report");
        assert_eq!(invalid_suite.report.outcome, Some(output::ReportedOutcome::Error));
        assert!(invalid_suite.report.is_finished);

        // The valid suite should not have been started, but finish should've been called so that
        // results get persisted.
        let not_started = reports
            .iter()
            .find(|e| e.report.name == "fuchsia-pkg://fuchsia.com/nothing#meta/nothing.cm")
            .expect("find run report");
        assert!(not_started.report.outcome.is_none());
        assert!(not_started.report.is_finished);

        // The results for the run should also be saved.
        let run = reports.iter().find(|e| e.id == EntityId::TestRun).expect("find run report");
        assert_eq!(run.report.outcome, Some(output::ReportedOutcome::Error));
        assert!(run.report.is_finished);
        assert!(run.report.started_time.is_some());
    }

    #[fuchsia::test]
    async fn single_run_debug_data() {
        let (builder_proxy, run_builder_stream) =
            create_proxy_and_stream::<ftest_manager::RunBuilderMarker>()
                .expect("create builder proxy");

        let reporter = InMemoryReporter::new();
        let run_reporter = RunReporter::new(reporter.clone());
        let run_fut = call_run_tests(ParamsForRunTests {
            builder_proxy,
            test_params: vec![TestParams {
                test_url: "fuchsia-pkg://fuchsia.com/nothing#meta/nothing.cm".to_string(),
                ..TestParams::default()
            }],
            run_reporter,
        });

        let dir = pseudo_directory! {
            "test_file.profraw" => read_only_static("Not a real profile"),
        };

        let (file_client, file_service) =
            fidl::endpoints::create_endpoints::<fidl_fuchsia_io::FileMarker>().unwrap();
        let scope = ExecutionScope::new();
        dir.open(
            scope,
            fidl_fuchsia_io::OPEN_RIGHT_READABLE,
            fidl_fuchsia_io::MODE_TYPE_FILE,
            vfs::path::Path::validate_and_split("test_file.profraw").unwrap(),
            ServerEnd::new(file_service.into_channel()),
        );

        let (debug_client, debug_service) =
            fidl::endpoints::create_endpoints::<ftest_manager::DebugDataIteratorMarker>().unwrap();
        let debug_data_fut = async move {
            let mut service = debug_service.into_stream().unwrap();
            let mut data = vec![ftest_manager::DebugData {
                name: Some("test_file.profraw".to_string()),
                file: Some(file_client),
                ..ftest_manager::DebugData::EMPTY
            }];
            while let Ok(Some(request)) = service.try_next().await {
                match request {
                    ftest_manager::DebugDataIteratorRequest::GetNext { responder, .. } => {
                        let _ = responder.send(&mut data.drain(0..));
                    }
                }
            }
        };
        let events = vec![ftest_manager::RunEvent {
            payload: Some(ftest_manager::RunEventPayload::Artifact(
                ftest_manager::Artifact::DebugData(debug_client),
            )),
            ..ftest_manager::RunEvent::EMPTY
        }];

        let fake_fut = fake_running_all_suites_and_return_run_events(
            run_builder_stream,
            hashmap! {

                "fuchsia-pkg://fuchsia.com/nothing#meta/nothing.cm" => create_empty_suite_events(),
            },
            events,
        );

        assert_eq!(join3(run_fut, debug_data_fut, fake_fut).await.0, Outcome::Passed);

        let reports = reporter.get_reports();
        assert_eq!(2usize, reports.len());
        let run = reports.iter().find(|e| e.id == EntityId::TestRun).expect("find run report");
        assert_eq!(1usize, run.report.directories.len());
        let dir = &run.report.directories[0];
        let files = dir.1.files.lock();
        assert_eq!(1usize, files.len());
        let (name, file) = &files[0];
        assert_eq!(name.to_string_lossy(), "test_file.profraw".to_string());
        assert_eq!(file.get_contents(), b"Not a real profile");
    }
}
