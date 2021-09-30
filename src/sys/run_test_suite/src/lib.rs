// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Used because we use `futures::select!`.
//
// From https://docs.rs/futures/0.3.1/futures/macro.select.html:
//   Note that select! relies on proc-macro-hack, and may require to set the compiler's
//   recursion limit very high, e.g. #![recursion_limit="1024"].
#![recursion_limit = "512"]

use {
    async_trait::async_trait,
    fidl::Peered,
    fidl_fuchsia_test_manager::{
        self as ftest_manager, CaseArtifact, CaseFinished, CaseFound, CaseStarted, CaseStopped,
        RunBuilderProxy, SuiteArtifact, SuiteStopped,
    },
    fuchsia_async as fasync,
    futures::{channel::mpsc, join, prelude::*, stream::LocalBoxStream},
    log::{error, warn},
    std::collections::{HashMap, HashSet},
    std::convert::TryInto,
    std::fmt,
    std::io::{self, ErrorKind, Write},
    std::path::PathBuf,
    std::time::Duration,
};

mod artifact;
pub mod diagnostics;
pub mod output;

use {
    artifact::{Artifact, ArtifactSender},
    output::{
        AnsiFilterWriter, ArtifactType, CaseId, DirectoryArtifactType, RunReporter, SuiteId,
        SuiteReporter, Timestamp,
    },
};

/// Duration after which to emit an excessive duration log.
const EXCESSIVE_DURATION: Duration = Duration::from_secs(60);

#[derive(PartialEq, Debug, Clone, Copy)]
pub enum Outcome {
    Passed,
    Failed,
    Inconclusive,
    Timedout,
    Error,
}

impl fmt::Display for Outcome {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Outcome::Passed => write!(f, "PASSED"),
            Outcome::Failed => write!(f, "FAILED"),
            Outcome::Inconclusive => write!(f, "INCONCLUSIVE"),
            Outcome::Timedout => write!(f, "TIMED OUT"),
            Outcome::Error => write!(f, "ERROR"),
        }
    }
}

#[derive(PartialEq, Debug)]
pub struct SuiteRunResult {
    /// Test outcome.
    pub outcome: Outcome,

    /// All tests which were executed.
    pub executed: Vec<String>,

    /// All tests which passed.
    pub passed: Vec<String>,

    /// All tests which failed.
    pub failed: Vec<String>,

    /// Suite protocol completed without error.
    pub successful_completion: bool,

    /// restricted logs produced by this suite run which exceed expected log level.
    pub restricted_logs: Vec<String>,
}

#[async_trait]
pub trait BuilderConnector {
    async fn connect(&self) -> RunBuilderProxy;
}

// Parameters for test.
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

    /// RunBuilderProxy connector that manages running the tests.
    pub builder_connector: Box<dyn BuilderConnector>,
}

pub fn convert_launch_error_to_str(e: ftest_manager::LaunchError) -> &'static str {
    match e {
        ftest_manager::LaunchError::CaseEnumeration => "Cannot enumerate test. This may mean `fuchsia.test.Suite` was not \
        configured correctly. Refer to: \
        https://fuchsia.dev/fuchsia-src/development/components/v2/troubleshooting#troubleshoot-test",
        ftest_manager::LaunchError::ResourceUnavailable => "Resource unavailable",
        ftest_manager::LaunchError::InstanceCannotResolve => "Cannot resolve test.",
        ftest_manager::LaunchError::InvalidArgs => {
            "Invalid args passed to builder while adding suite. Please file bug"
        }
        ftest_manager::LaunchError::FailedToConnectToTestSuite => {
            "Cannot communicate with the tests. This may mean `fuchsia.test.Suite` was not \
            configured correctly. Refer to: \
            https://fuchsia.dev/fuchsia-src/development/components/v2/troubleshooting#troubleshoot-test"
        }
        ftest_manager::LaunchError::InternalError => "Internal error, please file bug",
    }
}

async fn collect_results_for_suite(
    suite_controller: ftest_manager::SuiteControllerProxy,
    mut artifact_sender: ArtifactSender,
    suite_reporter: &SuiteReporter<'_>,
    log_opts: diagnostics::LogCollectionOptions,
) -> Result<SuiteRunResult, anyhow::Error> {
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
    let mut tasks = vec![];

    loop {
        match suite_controller.get_events().await? {
            Err(e) => {
                suite_reporter.stopped(&Outcome::Error.into(), Timestamp::Unknown)?;
                return Err(anyhow::anyhow!(convert_launch_error_to_str(e)));
            }
            Ok(events) => {
                if events.len() == 0 {
                    break;
                }
                for event in events {
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
                        ftest_manager::SuiteEventPayload::CaseStarted(CaseStarted {
                            identifier,
                        }) => {
                            let test_case_name = test_cases
                                .get(&identifier)
                                .ok_or(anyhow::anyhow!(
                                    "test case with identifier {} not found",
                                    identifier
                                ))?
                                .clone();
                            let reporter = test_case_reporters.get(&identifier).unwrap();
                            // TODO(fxbug.dev/79712): Record per-case runtime once we have an
                            // accurate way to measure it.
                            reporter.started(Timestamp::Unknown)?;
                            if test_cases_executed.contains(&identifier) {
                                return Err(anyhow::anyhow!(
                                    "test case: '{}' started twice",
                                    test_case_name
                                ));
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
                                    .ok_or(anyhow::anyhow!(
                                        "test case with identifier {} not found",
                                        identifier
                                    ))?
                                    .clone();
                                let (sender, mut recv) = mpsc::channel(1024);
                                let t = fuchsia_async::Task::spawn(
                                    test_diagnostics::collect_and_send_string_output(
                                        socket, sender,
                                    ),
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
                                test_cases_output
                                    .entry(identifier)
                                    .or_insert(vec![])
                                    .push(stdout_task);
                            }
                            ftest_manager::Artifact::Stderr(socket) => {
                                let test_case_name = test_cases
                                    .get(&identifier)
                                    .ok_or(anyhow::anyhow!(
                                        "test case with identifier {} not found",
                                        identifier
                                    ))?
                                    .clone();
                                let (sender, mut recv) = mpsc::channel(1024);
                                let t = fuchsia_async::Task::spawn(
                                    test_diagnostics::collect_and_send_string_output(
                                        socket, sender,
                                    ),
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
                                test_cases_output
                                    .entry(identifier)
                                    .or_insert(vec![])
                                    .push(stderr_task);
                            }
                            ftest_manager::Artifact::Log(_) => {
                                artifact_sender
                                    .send_test_stdout_msg(
                                        "WARN: per test case logs not supported yet",
                                    )
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
                                    token.signal_peer(
                                        fidl::Signals::empty(),
                                        fidl::Signals::USER_0,
                                    )?;
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
                            let test_case_name =
                                test_cases.get(&identifier).ok_or(anyhow::anyhow!(
                                    "test case with identifier {} not found",
                                    identifier
                                ))?;

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
                                return Err(anyhow::anyhow!(
                                    "test case: '{}' was never started, still got a stop event",
                                    test_case_name
                                ));
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
                                e => {
                                    return Err(anyhow::anyhow!(
                                        "test status '{:?}' not supported",
                                        e
                                    ));
                                }
                            };
                            let reporter = test_case_reporters.get(&identifier).unwrap();
                            artifact_sender
                                .send_test_stdout_msg(format!(
                                    "[{}]\t{}",
                                    result_str, test_case_name
                                ))
                                .await
                                .unwrap_or_else(|e| error!("Cannot write logs: {:?}", e));
                            // TODO(fxbug.dev/79712): Record per-case runtime once we have an
                            // accurate way to measure it.
                            reporter.stopped(&status.into(), Timestamp::Unknown)?;
                        }
                        ftest_manager::SuiteEventPayload::CaseFinished(CaseFinished {
                            identifier,
                        }) => {
                            let reporter = test_case_reporters.remove(&identifier).unwrap();
                            reporter.finished()?;
                        }
                        ftest_manager::SuiteEventPayload::SuiteArtifact(SuiteArtifact {
                            artifact,
                        }) => match artifact {
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
                                    const READ_SIZE: u64 = 1024;
                                    let directory_artifact_ref = &directory_artifact;
                                    files_stream
                                        .try_for_each_concurrent(
                                            None,
                                            |(file_proxy, filepath)| async move {
                                                let mut file =
                                                    directory_artifact_ref.new_file(&filepath)?;
                                                loop {
                                                    let (status, mut buf) =
                                                        file_proxy.read(READ_SIZE).await.map_err(
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
                        },
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
                                ftest_manager::SuiteStatus::InternalError => Outcome::Error,
                                e => {
                                    return Err(anyhow::anyhow!("outcome '{:?}' not supported", e));
                                }
                            };
                        }
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
                    restricted_logs.append(&mut logs)
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

    if test_cases_in_progress.len() != 0 {
        match outcome {
            Outcome::Passed | Outcome::Failed => {
                outcome = Outcome::Inconclusive;
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

    let mut test_cases_executed = test_cases_executed
        .into_iter()
        .map(|i| test_cases.get(&i).unwrap().clone())
        .collect::<Vec<_>>();
    let mut test_cases_passed: Vec<String> = test_cases_passed.into_iter().collect();
    let mut test_cases_failed: Vec<String> = test_cases_failed.into_iter().collect();

    test_cases_executed.sort();
    test_cases_passed.sort();
    test_cases_failed.sort();

    suite_reporter.stopped(&outcome.into(), suite_finish_timestamp)?;

    Ok(SuiteRunResult {
        outcome,
        executed: test_cases_executed,
        passed: test_cases_passed,
        failed: test_cases_failed,
        successful_completion,
        restricted_logs,
    })
}

type SuiteResults<'a> = LocalBoxStream<'a, Result<SuiteRunResult, anyhow::Error>>;

/// Runs the test `count` number of times, and writes logs to writer.
pub async fn run_test<'a, Out: Write>(
    test_params: TestParams,
    count: u16,
    log_opts: diagnostics::LogCollectionOptions,
    stdout_writer: &'a mut Out,
    run_reporter: &'a mut RunReporter,
) -> Result<SuiteResults<'a>, anyhow::Error> {
    let timeout: Option<i64> = match test_params.timeout {
        Some(t) => Some(std::time::Duration::from_secs(t.get().into()).as_nanos().try_into()?),
        None => None,
    };
    let run_options = SuiteRunOptions {
        parallel: test_params.parallel,
        arguments: Some(test_params.test_args.clone()),
        run_disabled_tests: Some(test_params.also_run_disabled_tests),
        timeout: timeout,
        test_filters: test_params.test_filters,
        log_iterator: Some(diagnostics::get_type()),
    };

    struct FoldArgs<'a, Out: Write> {
        current_count: u16,
        count: u16,
        builder: Box<dyn BuilderConnector>,
        run_options: SuiteRunOptions,
        test_url: String,
        stdout_writer: &'a mut Out,
        run_reporter: &'a mut RunReporter,
        log_opts: diagnostics::LogCollectionOptions,
    }

    let args = FoldArgs {
        current_count: 0,
        count,
        builder: test_params.builder_connector,
        run_options,
        stdout_writer,
        run_reporter,
        test_url: test_params.test_url,
        log_opts,
    };

    let results = stream::try_unfold(args, move |mut args| async move {
        if args.current_count >= args.count {
            return Ok(None);
        }
        let (suite_controller, suite_server_end) = fidl::endpoints::create_proxy()?;
        let (run_controller, run_server_end) = fidl::endpoints::create_proxy()?;
        let builder_proxy = args.builder.connect().await;
        builder_proxy.add_suite(
            &args.test_url,
            args.run_options.clone().into(),
            suite_server_end,
        )?;

        builder_proxy.build(run_server_end)?;
        let mut next_count = args.current_count + 1;
        let (sender, mut recv) = mpsc::channel(1024);
        let reporter =
            args.run_reporter.new_suite(&args.test_url, &SuiteId(args.current_count.into()))?;

        let stdout_writer = args.stdout_writer;
        let fut1 = collect_results_for_suite(
            suite_controller,
            sender.into(),
            &reporter,
            args.log_opts.clone(),
        );
        let fut2 = async {
            let mut syslog_writer = match reporter.new_artifact(&ArtifactType::Syslog) {
                Ok(reporter_syslog) => reporter_syslog,
                Err(e) => {
                    return Err(anyhow::anyhow!("cannot collect logs: {:?}", e));
                }
            };
            while let Some(artifact) = recv.next().await {
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
            Ok(())
        };

        let (result, _) = join!(fut1, fut2);
        reporter.finished()?;
        let result = result?;

        args.stdout_writer = stdout_writer;
        loop {
            let events = run_controller.get_events().await?;
            if events.len() == 0 {
                break;
            }
            println!("WARN: Discarding run events: {:?}", events);
        }

        if result.outcome == Outcome::Timedout || result.outcome == Outcome::Error {
            // don't run test again
            next_count = args.count;
        }

        args.current_count = next_count;
        Ok(Some((result, args)))
    })
    .boxed_local();

    Ok(results)
}

async fn collect_results(
    test_url: &str,
    count: std::num::NonZeroU16,
    mut stream: SuiteResults<'_>,
) -> Outcome {
    let mut i: u16 = 1;
    let mut final_outcome = Outcome::Passed;

    loop {
        match stream.try_next().await {
            Err(e) => {
                println!("Test suite encountered error trying to run tests: {:?}", e);
                return Outcome::Error;
            }
            Ok(Some(SuiteRunResult {
                mut outcome,
                executed,
                passed,
                failed,
                successful_completion,
                restricted_logs,
            })) => {
                if count.get() > 1 {
                    println!("\nTest run count {}/{}", i, count);
                }
                println!("\n");
                if !failed.is_empty() {
                    println!("Failed tests: {}", failed.join(", "))
                }
                println!("{} out of {} tests passed...", passed.len(), executed.len());
                println!("{} completed with result: {}", &test_url, outcome);
                if !successful_completion {
                    println!("{} did not complete successfully.", &test_url);
                }
                if restricted_logs.len() > 0 {
                    if outcome == Outcome::Passed {
                        outcome = Outcome::Failed;
                    }
                    println!("\nTest {} produced unexpected high-severity logs:", &test_url);
                    println!("----------------xxxxx----------------");
                    for log in restricted_logs {
                        println!("{}", log);
                    }
                    println!("----------------xxxxx----------------");
                    println!("Failing this test. See: https://fuchsia.dev/fuchsia-src/concepts/testing/logs#restricting_log_severity\n");
                }

                i = i + 1;
                if count.get() > 1 {
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
pub async fn run_tests_and_get_outcome(
    test_params: TestParams,
    log_opts: diagnostics::LogCollectionOptions,
    count: std::num::NonZeroU16,
    filter_ansi: bool,
    record_directory: Option<PathBuf>,
) -> Outcome {
    let test_url = test_params.test_url.clone();
    println!("\nRunning test '{}'", &test_url);

    let mut stdout_for_results: Box<dyn Write + Send + Sync> = match filter_ansi {
        true => Box::new(AnsiFilterWriter::new(io::stdout())),
        false => Box::new(io::stdout()),
    };

    let reporter_res = match (filter_ansi, record_directory) {
        (true, Some(dir)) => RunReporter::new_ansi_filtered(dir),
        (false, Some(dir)) => RunReporter::new(dir),
        (_, None) => Ok(RunReporter::new_noop()),
    };
    let mut reporter = match reporter_res {
        Ok(r) => r,
        Err(e) => {
            println!("Test suite encountered error trying to run tests: {:?}", e);
            return Outcome::Error;
        }
    };

    let result_stream =
        match run_test(test_params, count.get(), log_opts, &mut stdout_for_results, &mut reporter)
            .await
        {
            Ok(s) => s,
            Err(e) => {
                println!(
                    "Test suite '{}' encountered error trying to run tests: {:?}",
                    test_url, e
                );
                return Outcome::Error;
            }
        };

    let test_outcome = collect_results(&test_url, count, result_stream).await;

    if count.get() > 1 && test_outcome != Outcome::Passed {
        println!("One or more test runs failed.");
    }

    let report_result = match reporter.stopped(&test_outcome.into(), Timestamp::Unknown) {
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
