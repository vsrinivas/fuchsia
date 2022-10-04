// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        artifacts,
        cancel::{Cancelled, NamedFutureExt, OrCancel},
        diagnostics::{self, LogDisplayConfiguration},
        outcome::{Outcome, RunTestSuiteError, UnexpectedEventError},
        output::{
            self, ArtifactType, CaseId, DirectoryArtifactType, RunReporter, SuiteId, SuiteReporter,
            Timestamp,
        },
        params::{RunParams, TestParams, TimeoutBehavior},
        stream_util::StreamUtil,
        trace::duration,
    },
    async_utils::event,
    diagnostics_data::Severity,
    fidl::Peered,
    fidl_fuchsia_test_manager::{
        self as ftest_manager, CaseArtifact, CaseFinished, CaseFound, CaseStarted, CaseStopped,
        RunBuilderProxy, SuiteArtifact, SuiteStopped,
    },
    fuchsia_async as fasync,
    futures::{prelude::*, stream::FuturesUnordered, StreamExt},
    std::collections::{HashMap, HashSet},
    std::convert::TryInto,
    std::io::Write,
    std::path::PathBuf,
    std::time::Duration,
    tracing::{error, info, warn},
};

/// Timeout for draining logs.
const LOG_TIMEOUT_DURATION: Duration = Duration::from_secs(10);

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
    duration!("collect_suite");
    let mut test_cases = HashMap::new();
    let mut test_case_reporters = HashMap::new();
    let mut test_cases_in_progress = HashSet::new();
    let mut test_cases_timed_out = HashSet::new();
    let mut test_cases_output = HashMap::new();
    let mut suite_log_tasks = vec![];
    let mut suite_finish_timestamp = Timestamp::Unknown;
    let mut outcome = Outcome::Passed;
    let mut successful_completion = false;
    let mut cancelled = false;
    let mut tasks = vec![];
    let suite_events_done_event = event::Event::new();

    let collect_results_fut = async {
        while let Some(event_result) = running_suite.next_event().named("next_event").await {
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
                                let reporter = test_case_reporters.get(&identifier).unwrap();
                                let stdout = reporter.new_artifact(&ArtifactType::Stdout).await?;
                                test_cases_output.entry(identifier).or_insert(vec![]).push(
                                    fasync::Task::spawn(
                                        artifacts::copy_socket_artifact(socket, stdout)
                                            .named("stdout"),
                                    ),
                                );
                            }
                            ftest_manager::Artifact::Stderr(socket) => {
                                let reporter = test_case_reporters.get_mut(&identifier).unwrap();
                                let stderr = reporter.new_artifact(&ArtifactType::Stderr).await?;
                                test_cases_output.entry(identifier).or_insert(vec![]).push(
                                    fasync::Task::spawn(
                                        artifacts::copy_socket_artifact(socket, stderr)
                                            .named("stdout"),
                                    ),
                                );
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
                            // when a case errors out we handle it as if it never completed
                            if status != ftest_manager::CaseStatus::Error {
                                test_cases_in_progress.remove(&identifier);
                            }
                            // record timed out cases so we can cancel artifact collection for them
                            if status == ftest_manager::CaseStatus::TimedOut {
                                test_cases_timed_out.insert(identifier);
                            };
                            let reporter = test_case_reporters.get(&identifier).unwrap();
                            // TODO(fxbug.dev/79712): Record per-case runtime once we have an
                            // accurate way to measure it.
                            reporter.stopped(&status.into(), Timestamp::Unknown).await?;
                        }
                        ftest_manager::SuiteEventPayload::CaseFinished(CaseFinished {
                            identifier,
                        }) => {
                            // in case the test timed out, terminate artifact collection since it
                            // may be hung.
                            let test_case_name = test_cases.get(&identifier).ok_or(
                                UnexpectedEventError::CaseFinishedButNotFound { identifier },
                            )?;
                            if test_cases_timed_out.remove(&identifier) {
                                for t in test_cases_output.remove(&identifier).unwrap_or(vec![]) {
                                    if let Some(Err(e)) = t.now_or_never() {
                                        error!(
                                            "Cannot write output for {}: {:?}",
                                            test_case_name, e
                                        );
                                    }
                                }
                            }
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
                                    suite_log_tasks.push(fasync::Task::spawn(
                                        diagnostics::collect_logs(
                                            test_diagnostics::LogStream::from_syslog(syslog)?,
                                            suite_reporter
                                                .new_artifact(&ArtifactType::Syslog)
                                                .await?,
                                            log_opts.clone(),
                                            diagnostics::LogTimeoutOptions {
                                                timeout_fut: suite_events_done_event
                                                    .wait_or_dropped(),
                                                time_between_logs: LOG_TIMEOUT_DURATION,
                                            },
                                        )
                                        .named("syslog"),
                                    ));
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

                                        let custom_fut = async move {
                                            if let Err(e) =
                                                artifacts::copy_custom_artifact_directory(
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
                                        }
                                        .named("custom_artifacts");

                                        tasks.push(fasync::Task::spawn(custom_fut));
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
        suite_events_done_event.signal();

        // complete collecting all case artifacts and signal completion
        info!("Awaiting case artifacts");
        for (identifier, test_case_reporter) in test_case_reporters.into_iter() {
            for t in test_cases_output.remove(&identifier).unwrap_or(vec![]) {
                if let Err(e) = t.await {
                    let test_case_name = test_cases.get(&identifier).unwrap();
                    error!("Cannot write output for {}: {:?}", test_case_name, e);
                }
            }
            test_case_reporter.finished().await?;
        }

        // collect all logs
        info!("Awaiting suite artifacts");
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
                    warn!("Failed to collect logs: {:?}", e);
                }
            }
        }

        for t in tasks.into_iter() {
            t.await;
        }
        Ok(())
    };

    match collect_results_fut.boxed_local().or_cancelled(cancel_fut).await {
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
    const DEFAULT_PIPELINED_REQUESTS: usize = 8;
    async fn wait_for_start(
        proxy: ftest_manager::SuiteControllerProxy,
        url: String,
        max_severity_logs: Option<Severity>,
        max_pipelined: Option<usize>,
    ) -> Self {
        // Stream of fidl responses, with multiple concurrently active requests.
        let unprocessed_event_stream = futures::stream::repeat_with(move || {
            proxy.get_events().inspect(|events_result| match events_result {
                Ok(Ok(ref events)) => info!("Latest suite event: {:?}", events.last()),
                _ => (),
            })
        })
        .buffered(max_pipelined.unwrap_or(Self::DEFAULT_PIPELINED_REQUESTS));
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

// Will invoke the WithSchedulingOptions FIDL method if a client indicates
// that they want to use experimental parallel execution.
async fn request_scheduling_options(
    run_params: &RunParams,
    builder_proxy: &RunBuilderProxy,
) -> Result<(), RunTestSuiteError> {
    let scheduling_options = ftest_manager::SchedulingOptions {
        max_parallel_suites: run_params.experimental_parallel_execution,
        accumulate_debug_data: Some(run_params.accumulate_debug_data),
        ..ftest_manager::SchedulingOptions::EMPTY
    };
    builder_proxy.with_scheduling_options(scheduling_options)?;
    Ok(())
}

/// Schedule and run the tests specified in |test_params|, and collect the results.
/// Note this currently doesn't record the result or call finished() on run_reporter,
/// the caller should do this instead.
async fn run_tests<'a, F: 'a + Future<Output = ()> + Unpin>(
    builder_proxy: RunBuilderProxy,
    test_params: impl IntoIterator<Item = TestParams>,
    run_params: RunParams,
    min_severity_logs: Option<Severity>,
    run_reporter: &'a RunReporter,
    cancel_fut: F,
) -> Result<Outcome, RunTestSuiteError> {
    let mut suite_start_futs = FuturesUnordered::new();
    let mut suite_reporters = HashMap::new();
    let mut show_full_moniker = false;
    for (suite_id_raw, params) in test_params.into_iter().enumerate() {
        show_full_moniker = show_full_moniker || params.show_full_moniker;
        let timeout: Option<i64> = match params.timeout_seconds {
            Some(t) => {
                const NANOS_IN_SEC: u64 = 1_000_000_000;
                let secs: u32 = t.get();
                let nanos: u64 = (secs as u64) * NANOS_IN_SEC;
                // Unwrap okay here as max value (u32::MAX * 1_000_000_000) is 62 bits
                Some(nanos.try_into().unwrap())
            }
            None => None,
        };
        let run_options = fidl_fuchsia_test_manager::RunOptions {
            parallel: params.parallel,
            arguments: Some(params.test_args),
            run_disabled_tests: Some(params.also_run_disabled_tests),
            timeout,
            case_filters_to_run: params.test_filters,
            log_iterator: Some(run_params.log_protocol.unwrap_or_else(diagnostics::get_type)),
            ..fidl_fuchsia_test_manager::RunOptions::EMPTY
        };

        let suite_id = SuiteId(suite_id_raw as u32);
        let suite = run_reporter.new_suite(&params.test_url, &suite_id).await?;
        suite.set_tags(params.tags).await;
        suite_reporters.insert(suite_id, suite);
        let (suite_controller, suite_server_end) = fidl::endpoints::create_proxy()?;
        let suite_and_id_fut = RunningSuite::wait_for_start(
            suite_controller,
            params.test_url.clone(),
            params.max_severity_logs,
            None,
        )
        .map(move |running_suite| (running_suite, suite_id));
        suite_start_futs.push(suite_and_id_fut);
        builder_proxy.add_suite(&params.test_url, run_options, suite_server_end)?;
    }

    request_scheduling_options(&run_params, &builder_proxy).await?;
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
            let (running_suite, suite_id) = match suite_start_futs
                .next()
                .named("suite_start")
                .or_cancelled(cancel_fut.clone())
                .await
            {
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
                format: LogDisplayConfiguration { show_full_moniker },
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
        duration!("run_events");
        loop {
            let events = run_controller_ref.get_events().named("run_event").await?;
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
                                let output_directory = run_reporter
                                    .new_directory_artifact(
                                        &DirectoryArtifactType::Debug,
                                        None, /* moniker */
                                    )
                                    .await?;
                                artifacts::copy_debug_data(
                                    iterator.into_proxy()?,
                                    output_directory,
                                )
                                .named("debug_data")
                                .await;
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

                                    if let Err(e) = artifacts::copy_custom_artifact_directory(
                                        directory, out_dir,
                                    )
                                    .named("run_custom_artifact")
                                    .await
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
    test_params: impl IntoIterator<Item = TestParams>,
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
            return Outcome::error(e);
        }
    };

    let report_result =
        match run_reporter.stopped(&test_outcome.clone().into(), Timestamp::Unknown).await {
            Ok(()) => run_reporter.finished().await,
            Err(e) => Err(e),
        };
    if let Err(e) = report_result {
        warn!("Failed to record results: {:?}", e);
    }

    test_outcome
}

pub struct DirectoryReporterOptions {
    /// Root path of the directory.
    pub root_path: PathBuf,
}

/// Create a reporter for use with |run_tests_and_get_outcome|.
pub fn create_reporter<W: 'static + Write + Send + Sync>(
    filter_ansi: bool,
    dir: Option<DirectoryReporterOptions>,
    writer: W,
) -> Result<output::RunReporter, anyhow::Error> {
    let stdout_reporter = output::ShellReporter::new(writer);
    let dir_reporter = dir
        .map(|dir| {
            output::DirectoryWithStdoutReporter::new(dir.root_path, output::SchemaVersion::V1)
        })
        .transpose()?;
    let reporter = match (dir_reporter, filter_ansi) {
        (Some(dir_reporter), false) => output::RunReporter::new(output::MultiplexedReporter::new(
            stdout_reporter,
            dir_reporter,
        )),
        (Some(dir_reporter), true) => output::RunReporter::new_ansi_filtered(
            output::MultiplexedReporter::new(stdout_reporter, dir_reporter),
        ),
        (None, false) => output::RunReporter::new(stdout_reporter),
        (None, true) => output::RunReporter::new_ansi_filtered(stdout_reporter),
    };
    Ok(reporter)
}

#[cfg(test)]
mod test {
    use {
        super::*, crate::output::InMemoryReporter, assert_matches::assert_matches,
        fidl::endpoints::create_proxy_and_stream, ftest_manager, futures::future::join,
        maplit::hashmap, output::EntityId,
    };
    #[cfg(target_os = "fuchsia")]
    use {
        fidl::endpoints::ServerEnd,
        fidl_fuchsia_io as fio, fuchsia_zircon as zx,
        futures::future::join3,
        vfs::{
            directory::entry::DirectoryEntry, execution_scope::ExecutionScope,
            file::vmo::read_only_static, pseudo_directory,
        },
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

    /// Serves all events to completion.
    async fn serve_all_events(
        mut request_stream: ftest_manager::SuiteControllerRequestStream,
        events: Vec<ftest_manager::SuiteEvent>,
    ) {
        const BATCH_SIZE: usize = 5;
        let mut event_iter = events.into_iter();
        while event_iter.len() > 0 {
            respond_to_get_events(
                &mut request_stream,
                event_iter.by_ref().take(BATCH_SIZE).collect(),
            )
            .await;
        }
        respond_to_get_events(&mut request_stream, vec![]).await;
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
            RunningSuite::wait_for_start(suite_proxy, TEST_URL.to_string(), None, None).await;
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
            RunningSuite::wait_for_start(suite_proxy, TEST_URL.to_string(), None, None).await;

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
            RunningSuite::wait_for_start(suite_proxy, TEST_URL.to_string(), None, None).await;
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

    fn suite_event_from_payload(
        timestamp: i64,
        payload: ftest_manager::SuiteEventPayload,
    ) -> ftest_manager::SuiteEvent {
        let mut event = create_empty_event(timestamp);
        event.payload = Some(payload);
        event
    }

    fn case_found_event(timestamp: i64, identifier: u32, name: &str) -> ftest_manager::SuiteEvent {
        suite_event_from_payload(
            timestamp,
            ftest_manager::SuiteEventPayload::CaseFound(ftest_manager::CaseFound {
                test_case_name: name.into(),
                identifier,
            }),
        )
    }

    fn case_started_event(timestamp: i64, identifier: u32) -> ftest_manager::SuiteEvent {
        suite_event_from_payload(
            timestamp,
            ftest_manager::SuiteEventPayload::CaseStarted(ftest_manager::CaseStarted {
                identifier,
            }),
        )
    }

    fn case_stopped_event(
        timestamp: i64,
        identifier: u32,
        status: ftest_manager::CaseStatus,
    ) -> ftest_manager::SuiteEvent {
        suite_event_from_payload(
            timestamp,
            ftest_manager::SuiteEventPayload::CaseStopped(ftest_manager::CaseStopped {
                identifier,
                status,
            }),
        )
    }

    fn case_finished_event(timestamp: i64, identifier: u32) -> ftest_manager::SuiteEvent {
        suite_event_from_payload(
            timestamp,
            ftest_manager::SuiteEventPayload::CaseFinished(ftest_manager::CaseFinished {
                identifier,
            }),
        )
    }

    fn case_stdout_event(
        timestamp: i64,
        identifier: u32,
        stdout: fidl::Socket,
    ) -> ftest_manager::SuiteEvent {
        suite_event_from_payload(
            timestamp,
            ftest_manager::SuiteEventPayload::CaseArtifact(ftest_manager::CaseArtifact {
                identifier,
                artifact: ftest_manager::Artifact::Stdout(stdout),
            }),
        )
    }

    fn case_stderr_event(
        timestamp: i64,
        identifier: u32,
        stderr: fidl::Socket,
    ) -> ftest_manager::SuiteEvent {
        suite_event_from_payload(
            timestamp,
            ftest_manager::SuiteEventPayload::CaseArtifact(ftest_manager::CaseArtifact {
                identifier,
                artifact: ftest_manager::Artifact::Stderr(stderr),
            }),
        )
    }

    fn suite_started_event(timestamp: i64) -> ftest_manager::SuiteEvent {
        suite_event_from_payload(
            timestamp,
            ftest_manager::SuiteEventPayload::SuiteStarted(ftest_manager::SuiteStarted),
        )
    }

    fn suite_stopped_event(
        timestamp: i64,
        status: ftest_manager::SuiteStatus,
    ) -> ftest_manager::SuiteEvent {
        suite_event_from_payload(
            timestamp,
            ftest_manager::SuiteEventPayload::SuiteStopped(ftest_manager::SuiteStopped { status }),
        )
    }

    #[fuchsia::test]
    async fn collect_suite_events_simple() {
        let all_events = vec![
            suite_started_event(0),
            case_found_event(100, 0, "my_test_case"),
            case_started_event(200, 0),
            case_stopped_event(300, 0, ftest_manager::CaseStatus::Passed),
            case_finished_event(400, 0),
            suite_stopped_event(500, ftest_manager::SuiteStatus::Passed),
        ];

        let (proxy, stream) = create_proxy_and_stream::<ftest_manager::SuiteControllerMarker>()
            .expect("create stream");
        let test_fut = async move {
            let reporter = output::InMemoryReporter::new();
            let run_reporter = output::RunReporter::new(reporter.clone());
            let suite_reporter =
                run_reporter.new_suite("test-url", &SuiteId(0)).await.expect("create new suite");

            let suite =
                RunningSuite::wait_for_start(proxy, "test-url".to_string(), None, None).await;
            assert_eq!(
                run_suite_and_collect_logs(
                    suite,
                    &suite_reporter,
                    diagnostics::LogCollectionOptions::default(),
                    futures::future::pending()
                )
                .await
                .expect("collect results"),
                Outcome::Passed
            );
            suite_reporter.finished().await.expect("Reporter finished");

            let reports = reporter.get_reports();
            let case = reports
                .iter()
                .find(|report| report.id == EntityId::Case { suite: SuiteId(0), case: CaseId(0) })
                .unwrap();
            assert_eq!(case.report.name, "my_test_case");
            assert_eq!(case.report.outcome, Some(output::ReportedOutcome::Passed));
            assert!(case.report.is_finished);
            assert!(case.report.artifacts.is_empty());
            assert!(case.report.directories.is_empty());
            let suite =
                reports.iter().find(|report| report.id == EntityId::Suite(SuiteId(0))).unwrap();
            assert_eq!(suite.report.name, "test-url");
            assert_eq!(suite.report.outcome, Some(output::ReportedOutcome::Passed));
            assert!(suite.report.is_finished);
            assert!(suite.report.artifacts.is_empty());
            assert!(suite.report.directories.is_empty());
        };

        futures::future::join(serve_all_events(stream, all_events), test_fut).await;
    }

    #[fuchsia::test]
    async fn collect_suite_events_with_case_artifacts() {
        const STDOUT_CONTENT: &str = "stdout from my_test_case";
        const STDERR_CONTENT: &str = "stderr from my_test_case";

        let (stdout_write, stdout_read) =
            fidl::Socket::create(fidl::SocketOpts::STREAM).expect("create socket");
        let (stderr_write, stderr_read) =
            fidl::Socket::create(fidl::SocketOpts::STREAM).expect("create socket");
        let all_events = vec![
            suite_started_event(0),
            case_found_event(100, 0, "my_test_case"),
            case_started_event(200, 0),
            case_stdout_event(300, 0, stdout_read),
            case_stderr_event(300, 0, stderr_read),
            case_stopped_event(300, 0, ftest_manager::CaseStatus::Passed),
            case_finished_event(400, 0),
            suite_stopped_event(500, ftest_manager::SuiteStatus::Passed),
        ];

        let (proxy, stream) = create_proxy_and_stream::<ftest_manager::SuiteControllerMarker>()
            .expect("create stream");
        let stdio_write_fut = async move {
            let mut async_stdout =
                fasync::Socket::from_socket(stdout_write).expect("make async socket");
            async_stdout.write_all(STDOUT_CONTENT.as_bytes()).await.expect("write to socket");
            let mut async_stderr =
                fasync::Socket::from_socket(stderr_write).expect("make async socket");
            async_stderr.write_all(STDERR_CONTENT.as_bytes()).await.expect("write to socket");
        };
        let test_fut = async move {
            let reporter = output::InMemoryReporter::new();
            let run_reporter = output::RunReporter::new(reporter.clone());
            let suite_reporter =
                run_reporter.new_suite("test-url", &SuiteId(0)).await.expect("create new suite");

            let suite =
                RunningSuite::wait_for_start(proxy, "test-url".to_string(), None, None).await;
            assert_eq!(
                run_suite_and_collect_logs(
                    suite,
                    &suite_reporter,
                    diagnostics::LogCollectionOptions::default(),
                    futures::future::pending()
                )
                .await
                .expect("collect results"),
                Outcome::Passed
            );
            suite_reporter.finished().await.expect("Reporter finished");

            let reports = reporter.get_reports();
            let case = reports
                .iter()
                .find(|report| report.id == EntityId::Case { suite: SuiteId(0), case: CaseId(0) })
                .unwrap();
            assert_eq!(case.report.name, "my_test_case");
            assert_eq!(case.report.outcome, Some(output::ReportedOutcome::Passed));
            assert!(case.report.is_finished);
            assert_eq!(case.report.artifacts.len(), 2);
            assert_eq!(
                case.report
                    .artifacts
                    .iter()
                    .map(|(artifact_type, artifact)| (*artifact_type, artifact.get_contents()))
                    .collect::<HashMap<_, _>>(),
                hashmap! {
                    output::ArtifactType::Stdout => STDOUT_CONTENT.as_bytes().to_vec(),
                    output::ArtifactType::Stderr => STDERR_CONTENT.as_bytes().to_vec()
                }
            );
            assert!(case.report.directories.is_empty());

            let suite =
                reports.iter().find(|report| report.id == EntityId::Suite(SuiteId(0))).unwrap();
            assert_eq!(suite.report.name, "test-url");
            assert_eq!(suite.report.outcome, Some(output::ReportedOutcome::Passed));
            assert!(suite.report.is_finished);
            assert!(suite.report.artifacts.is_empty());
            assert!(suite.report.directories.is_empty());
        };

        futures::future::join3(serve_all_events(stream, all_events), stdio_write_fut, test_fut)
            .await;
    }

    #[fuchsia::test]
    async fn collect_suite_events_case_artifacts_complete_after_suite() {
        const STDOUT_CONTENT: &str = "stdout from my_test_case";
        const STDERR_CONTENT: &str = "stderr from my_test_case";

        let (stdout_write, stdout_read) =
            fidl::Socket::create(fidl::SocketOpts::STREAM).expect("create socket");
        let (stderr_write, stderr_read) =
            fidl::Socket::create(fidl::SocketOpts::STREAM).expect("create socket");
        let all_events = vec![
            suite_started_event(0),
            case_found_event(100, 0, "my_test_case"),
            case_started_event(200, 0),
            case_stdout_event(300, 0, stdout_read),
            case_stderr_event(300, 0, stderr_read),
            case_stopped_event(300, 0, ftest_manager::CaseStatus::Passed),
            case_finished_event(400, 0),
            suite_stopped_event(500, ftest_manager::SuiteStatus::Passed),
        ];

        let (proxy, stream) = create_proxy_and_stream::<ftest_manager::SuiteControllerMarker>()
            .expect("create stream");
        let serve_fut = async move {
            // server side will send all events, then write to (and close) sockets.
            serve_all_events(stream, all_events).await;
            let mut async_stdout =
                fasync::Socket::from_socket(stdout_write).expect("make async socket");
            async_stdout.write_all(STDOUT_CONTENT.as_bytes()).await.expect("write to socket");
            let mut async_stderr =
                fasync::Socket::from_socket(stderr_write).expect("make async socket");
            async_stderr.write_all(STDERR_CONTENT.as_bytes()).await.expect("write to socket");
        };
        let test_fut = async move {
            let reporter = output::InMemoryReporter::new();
            let run_reporter = output::RunReporter::new(reporter.clone());
            let suite_reporter =
                run_reporter.new_suite("test-url", &SuiteId(0)).await.expect("create new suite");

            let suite =
                RunningSuite::wait_for_start(proxy, "test-url".to_string(), None, Some(1)).await;
            assert_eq!(
                run_suite_and_collect_logs(
                    suite,
                    &suite_reporter,
                    diagnostics::LogCollectionOptions::default(),
                    futures::future::pending()
                )
                .await
                .expect("collect results"),
                Outcome::Passed
            );
            suite_reporter.finished().await.expect("Reporter finished");

            let reports = reporter.get_reports();
            let case = reports
                .iter()
                .find(|report| report.id == EntityId::Case { suite: SuiteId(0), case: CaseId(0) })
                .unwrap();
            assert_eq!(case.report.name, "my_test_case");
            assert_eq!(case.report.outcome, Some(output::ReportedOutcome::Passed));
            assert!(case.report.is_finished);
            assert_eq!(case.report.artifacts.len(), 2);
            assert_eq!(
                case.report
                    .artifacts
                    .iter()
                    .map(|(artifact_type, artifact)| (*artifact_type, artifact.get_contents()))
                    .collect::<HashMap<_, _>>(),
                hashmap! {
                    output::ArtifactType::Stdout => STDOUT_CONTENT.as_bytes().to_vec(),
                    output::ArtifactType::Stderr => STDERR_CONTENT.as_bytes().to_vec()
                }
            );
            assert!(case.report.directories.is_empty());

            let suite =
                reports.iter().find(|report| report.id == EntityId::Suite(SuiteId(0))).unwrap();
            assert_eq!(suite.report.name, "test-url");
            assert_eq!(suite.report.outcome, Some(output::ReportedOutcome::Passed));
            assert!(suite.report.is_finished);
            assert!(suite.report.artifacts.is_empty());
            assert!(suite.report.directories.is_empty());
        };

        futures::future::join(serve_fut, test_fut).await;
    }

    #[fuchsia::test]
    async fn collect_suite_events_with_case_artifacts_sent_after_case_stopped() {
        const STDOUT_CONTENT: &str = "stdout from my_test_case";
        const STDERR_CONTENT: &str = "stderr from my_test_case";

        let (stdout_write, stdout_read) =
            fidl::Socket::create(fidl::SocketOpts::STREAM).expect("create socket");
        let (stderr_write, stderr_read) =
            fidl::Socket::create(fidl::SocketOpts::STREAM).expect("create socket");
        let all_events = vec![
            suite_started_event(0),
            case_found_event(100, 0, "my_test_case"),
            case_started_event(200, 0),
            case_stopped_event(300, 0, ftest_manager::CaseStatus::Passed),
            case_stdout_event(300, 0, stdout_read),
            case_stderr_event(300, 0, stderr_read),
            case_finished_event(400, 0),
            suite_stopped_event(500, ftest_manager::SuiteStatus::Passed),
        ];

        let (proxy, stream) = create_proxy_and_stream::<ftest_manager::SuiteControllerMarker>()
            .expect("create stream");
        let stdio_write_fut = async move {
            let mut async_stdout =
                fasync::Socket::from_socket(stdout_write).expect("make async socket");
            async_stdout.write_all(STDOUT_CONTENT.as_bytes()).await.expect("write to socket");
            let mut async_stderr =
                fasync::Socket::from_socket(stderr_write).expect("make async socket");
            async_stderr.write_all(STDERR_CONTENT.as_bytes()).await.expect("write to socket");
        };
        let test_fut = async move {
            let reporter = output::InMemoryReporter::new();
            let run_reporter = output::RunReporter::new(reporter.clone());
            let suite_reporter =
                run_reporter.new_suite("test-url", &SuiteId(0)).await.expect("create new suite");

            let suite =
                RunningSuite::wait_for_start(proxy, "test-url".to_string(), None, None).await;
            assert_eq!(
                run_suite_and_collect_logs(
                    suite,
                    &suite_reporter,
                    diagnostics::LogCollectionOptions::default(),
                    futures::future::pending()
                )
                .await
                .expect("collect results"),
                Outcome::Passed
            );
            suite_reporter.finished().await.expect("Reporter finished");

            let reports = reporter.get_reports();
            let case = reports
                .iter()
                .find(|report| report.id == EntityId::Case { suite: SuiteId(0), case: CaseId(0) })
                .unwrap();
            assert_eq!(case.report.name, "my_test_case");
            assert_eq!(case.report.outcome, Some(output::ReportedOutcome::Passed));
            assert!(case.report.is_finished);
            assert_eq!(case.report.artifacts.len(), 2);
            assert_eq!(
                case.report
                    .artifacts
                    .iter()
                    .map(|(artifact_type, artifact)| (*artifact_type, artifact.get_contents()))
                    .collect::<HashMap<_, _>>(),
                hashmap! {
                    output::ArtifactType::Stdout => STDOUT_CONTENT.as_bytes().to_vec(),
                    output::ArtifactType::Stderr => STDERR_CONTENT.as_bytes().to_vec()
                }
            );
            assert!(case.report.directories.is_empty());

            let suite =
                reports.iter().find(|report| report.id == EntityId::Suite(SuiteId(0))).unwrap();
            assert_eq!(suite.report.name, "test-url");
            assert_eq!(suite.report.outcome, Some(output::ReportedOutcome::Passed));
            assert!(suite.report.is_finished);
            assert!(suite.report.artifacts.is_empty());
            assert!(suite.report.directories.is_empty());
        };

        futures::future::join3(serve_all_events(stream, all_events), stdio_write_fut, test_fut)
            .await;
    }

    #[fuchsia::test]
    async fn collect_suite_events_timed_out_case_with_hanging_artifacts() {
        // create sockets and leave the server end open to simulate a hang.
        let (_stdout_write, stdout_read) =
            fidl::Socket::create(fidl::SocketOpts::STREAM).expect("create socket");
        let (_stderr_write, stderr_read) =
            fidl::Socket::create(fidl::SocketOpts::STREAM).expect("create socket");
        let all_events = vec![
            suite_started_event(0),
            case_found_event(100, 0, "my_test_case"),
            case_started_event(200, 0),
            case_stdout_event(300, 0, stdout_read),
            case_stopped_event(300, 0, ftest_manager::CaseStatus::TimedOut),
            // artifact sent after stopped should be terminated too.
            case_stderr_event(300, 0, stderr_read),
            case_finished_event(400, 0),
            suite_stopped_event(500, ftest_manager::SuiteStatus::TimedOut),
        ];

        let (proxy, stream) = create_proxy_and_stream::<ftest_manager::SuiteControllerMarker>()
            .expect("create stream");
        let test_fut = async move {
            let reporter = output::InMemoryReporter::new();
            let run_reporter = output::RunReporter::new(reporter.clone());
            let suite_reporter =
                run_reporter.new_suite("test-url", &SuiteId(0)).await.expect("create new suite");

            let suite =
                RunningSuite::wait_for_start(proxy, "test-url".to_string(), None, None).await;
            assert_eq!(
                run_suite_and_collect_logs(
                    suite,
                    &suite_reporter,
                    diagnostics::LogCollectionOptions::default(),
                    futures::future::pending()
                )
                .await
                .expect("collect results"),
                Outcome::Timedout
            );
            suite_reporter.finished().await.expect("Reporter finished");

            let reports = reporter.get_reports();
            let case = reports
                .iter()
                .find(|report| report.id == EntityId::Case { suite: SuiteId(0), case: CaseId(0) })
                .unwrap();
            assert_eq!(case.report.name, "my_test_case");
            assert_eq!(case.report.outcome, Some(output::ReportedOutcome::Timedout));
            assert!(case.report.is_finished);
            assert_eq!(case.report.artifacts.len(), 2);
            assert_eq!(
                case.report
                    .artifacts
                    .iter()
                    .map(|(artifact_type, artifact)| (*artifact_type, artifact.get_contents()))
                    .collect::<HashMap<_, _>>(),
                hashmap! {
                    output::ArtifactType::Stdout => vec![],
                    output::ArtifactType::Stderr => vec![]
                }
            );
            assert!(case.report.directories.is_empty());

            let suite =
                reports.iter().find(|report| report.id == EntityId::Suite(SuiteId(0))).unwrap();
            assert_eq!(suite.report.name, "test-url");
            assert_eq!(suite.report.outcome, Some(output::ReportedOutcome::Timedout));
            assert!(suite.report.is_finished);
            assert!(suite.report.artifacts.is_empty());
            assert!(suite.report.directories.is_empty());
        };

        futures::future::join(serve_all_events(stream, all_events), test_fut).await;
    }

    // TODO(fxbug.dev/98222): add unit tests for suite artifacts too.

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
                ftest_manager::RunBuilderRequest::WithSchedulingOptions { options, .. } => {
                    if let Some(_) = options.max_parallel_suites {
                        panic!("Not expecting calls to WithSchedulingOptions where options.max_parallel_suites is Some()")
                    }
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
            RunParams {
                timeout_behavior: TimeoutBehavior::Continue,
                stop_after_failures: None,
                experimental_parallel_execution: None,
                accumulate_debug_data: false,
                log_protocol: None,
            },
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

    #[cfg(target_os = "fuchsia")]
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
            fidl::endpoints::create_endpoints::<fio::DirectoryMarker>().unwrap();
        let scope = ExecutionScope::new();
        dir.open(
            scope,
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
            fio::MODE_TYPE_DIRECTORY,
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

    #[cfg(target_os = "fuchsia")]
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
            fidl::endpoints::create_endpoints::<fio::FileMarker>().unwrap();
        let scope = ExecutionScope::new();
        dir.open(
            scope,
            fio::OpenFlags::RIGHT_READABLE,
            fio::MODE_TYPE_FILE,
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

    async fn fake_parallel_options_server(
        mut stream: ftest_manager::RunBuilderRequestStream,
    ) -> Option<ftest_manager::SchedulingOptions> {
        let mut scheduling_options = None;
        if let Ok(Some(req)) = stream.try_next().await {
            match req {
                ftest_manager::RunBuilderRequest::AddSuite { .. } => {
                    panic!("Not expecting an AddSuite request")
                }
                ftest_manager::RunBuilderRequest::Build { .. } => {
                    panic!("Not expecting a Build request")
                }
                ftest_manager::RunBuilderRequest::WithSchedulingOptions { options, .. } => {
                    scheduling_options = Some(options);
                }
            }
        }
        scheduling_options
    }

    #[fuchsia::test]
    async fn request_scheduling_options_test_parallel() {
        let max_parallel_suites: u16 = 10;
        let expected_max_parallel_suites = Some(max_parallel_suites);

        let (builder_proxy, run_builder_stream) =
            create_proxy_and_stream::<ftest_manager::RunBuilderMarker>()
                .expect("create builder proxy");

        let run_params = RunParams {
            timeout_behavior: TimeoutBehavior::Continue,
            stop_after_failures: None,
            experimental_parallel_execution: Some(max_parallel_suites),
            accumulate_debug_data: false,
            log_protocol: None,
        };

        let request_parallel_fut = request_scheduling_options(&run_params, &builder_proxy);
        let fake_server_fut = fake_parallel_options_server(run_builder_stream);

        let returned_options = join(request_parallel_fut, fake_server_fut).await.1;
        let max_parallel_suites_received = match returned_options {
            Some(scheduling_options) => scheduling_options.max_parallel_suites,
            None => panic!("Expected scheduling options."),
        };
        assert_eq!(max_parallel_suites_received, expected_max_parallel_suites);
    }

    #[fuchsia::test]
    async fn request_scheduling_options_test_serial() {
        let expected_max_parallel_suites = None;

        let (builder_proxy, run_builder_stream) =
            create_proxy_and_stream::<ftest_manager::RunBuilderMarker>()
                .expect("create builder proxy");

        let run_params = RunParams {
            timeout_behavior: TimeoutBehavior::Continue,
            stop_after_failures: None,
            experimental_parallel_execution: None,
            accumulate_debug_data: false,
            log_protocol: None,
        };

        let request_parallel_fut = request_scheduling_options(&run_params, &builder_proxy);
        let fake_server_fut = fake_parallel_options_server(run_builder_stream);

        let returned_options = join(request_parallel_fut, fake_server_fut)
            .await
            .1
            .expect("Expected scheduling options.");
        let max_parallel_suites_received = returned_options.max_parallel_suites;
        assert_eq!(max_parallel_suites_received, expected_max_parallel_suites);
    }
}
