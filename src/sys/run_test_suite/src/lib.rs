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
    fidl_fuchsia_test::Invocation,
    fidl_fuchsia_test_manager::HarnessProxy,
    fuchsia_async as fasync,
    futures::{channel::mpsc, join, prelude::*, stream::LocalBoxStream},
    log::error,
    std::collections::{HashMap, HashSet},
    std::fmt,
    std::io,
    std::path::PathBuf,
    test_executor::{LogStream, TestEvent, TestRunOptions},
};

pub mod diagnostics;
pub mod output;

pub use test_executor::DisabledTestHandling;

use output::{
    AnsiFilterWriter, ArtifactType, CaseReporter, MultiplexedWriter, RunReporter, SuiteReporter,
    WriteLine,
};

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
pub struct RunResult {
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
}

// Parameters for test.
pub struct TestParams {
    /// Test URL.
    pub test_url: String,

    /// |timeout|: Test timeout.should be more than zero.
    pub timeout: Option<std::num::NonZeroU32>,

    /// Filter tests based on this glob pattern.
    pub test_filter: Option<String>,

    // Run disabled tests.
    pub also_run_disabled_tests: bool,

    /// Test concurrency count.
    pub parallel: Option<u16>,

    /// Arguments to pass to test using command line.
    pub test_args: Vec<String>,

    /// HarnessProxy that manages running the tests.
    pub harness: HarnessProxy,
}

impl TestParams {
    fn disabled_tests(&self) -> DisabledTestHandling {
        return match self.also_run_disabled_tests {
            true => DisabledTestHandling::Include,
            false => DisabledTestHandling::Exclude,
        };
    }
}

async fn run_test_for_invocations<W: WriteLine>(
    suite_instance: &test_executor::SuiteInstance,
    invocations: Vec<Invocation>,
    run_options: TestRunOptions,
    timeout: Option<std::num::NonZeroU32>,
    writer: &mut W,
    suite_reporter: SuiteReporter<'_>,
) -> Result<RunResult, anyhow::Error> {
    let mut timeout = match timeout {
        Some(timeout) => futures::future::Either::Left(
            fasync::Timer::new(std::time::Duration::from_secs(timeout.get().into()))
                .map(|()| Err(())),
        ),
        None => futures::future::Either::Right(futures::future::ready(Ok(()))),
    }
    .fuse();

    let (sender, mut recv) = mpsc::channel(1);
    let mut outcome = Outcome::Passed;

    struct TestCaseAndStdout<'a> {
        case_reporter: CaseReporter<'a>,
        stdout: Option<Box<dyn 'static + WriteLine + Send + Sync>>,
    }

    let mut test_cases_in_progress = HashSet::new();
    let mut test_cases_executed = HashMap::new();
    let mut test_cases_passed = HashSet::new();
    let mut test_cases_failed = HashSet::new();
    let mut successful_completion = false;

    let test_fut = suite_instance
        .run_and_collect_results_for_invocations(sender, invocations, run_options)
        .fuse();
    futures::pin_mut!(test_fut);

    loop {
        futures::select! {
            timeout_res = timeout => {
                match timeout_res {
                    Ok(()) => {}, // No timeout specified.
                    Err(()) => {
                        outcome = Outcome::Timedout;
                        break
                    },
                }
            },
            test_res = test_fut => {
                let () = test_res?;
            },
            test_event = recv.next() => {
                if let Some(test_event) = test_event {
                    match test_event {
                        TestEvent::TestCaseStarted { test_case_name } => {
                            if test_cases_executed.contains_key(&test_case_name) {
                                return Err(anyhow::anyhow!("test case: '{}' started twice", test_case_name));
                            }
                            writer.write_line(&format!("[RUNNING]\t{}", test_case_name))
                                .unwrap_or_else(|e| error!("Cannot write logs: {:?}", e));
                            test_cases_in_progress.insert(
                                test_case_name.clone()
                            );
                            let case_reporter = suite_reporter.new_case(&test_case_name)?;
                            test_cases_executed.insert(
                                test_case_name,
                                TestCaseAndStdout {
                                    case_reporter,
                                    stdout: None
                                }
                            );
                        }
                        TestEvent::TestCaseFinished { test_case_name, result } => {
                            if !test_cases_in_progress.remove(&test_case_name) {
                                return Err(anyhow::anyhow!(
                                    "test case: '{}' was never started, still got a finish event",
                                    test_case_name
                                ));
                            }
                            let result_str = match result {
                                test_executor::TestResult::Passed => {
                                    test_cases_passed.insert(test_case_name.clone());
                                    "PASSED"
                                }
                                test_executor::TestResult::Failed => {
                                    if outcome == Outcome::Passed {
                                        outcome = Outcome::Failed;
                                    }
                                    test_cases_failed.insert(test_case_name.clone());
                                    "FAILED"
                                }
                                test_executor::TestResult::Skipped => "SKIPPED",
                                test_executor::TestResult::Error => {
                                    outcome = Outcome::Error;
                                    test_cases_failed.insert(test_case_name.clone());
                                    "ERROR"
                                }
                            };
                            let reporter = match test_cases_executed.get_mut(&test_case_name) {
                                Some(reporter) => reporter,
                                None => return Err(anyhow::anyhow!(
                                    "test case: '{}' was never started, still got a finish event"))
                            };
                            writer.write_line(&format!("[{}]\t{}", result_str, test_case_name))
                                .unwrap_or_else(|e| error!("Cannot write logs: {:?}", e));
                            reporter.case_reporter.outcome(&result.into())?;
                        }
                        TestEvent::ExcessiveDuration { test_case_name, duration } => {
                            writer.write_line(&format!("[duration - {}]:\tStill running after {:?} seconds",
                                test_case_name, duration.as_secs()))
                                .unwrap_or_else(|e| error!("Cannot write logs: {:?}", e));
                        }
                        TestEvent::StdoutMessage { test_case_name, mut msg } => {
                            let mut test_case = match test_cases_executed.get_mut(&test_case_name) {
                                Some(case) => case,
                                None => return Err(anyhow::anyhow!(
                                    "test case: '{}' was never started, still got a log",
                                    test_case_name
                                )),
                            };
                            // check if last byte is newline and remove it as we are already
                            // printing a newline.
                            if msg.ends_with("\n") {
                                msg.truncate(msg.len()-1)
                            }
                            writer.write_line(&format!("[output - {}]:\n{}", test_case_name, msg))
                                .unwrap_or_else(|e| error!("Cannot write logs: {:?}", e));
                            match test_case.stdout.as_mut() {
                                None => {
                                    let mut stdout = test_case.case_reporter.new_artifact(&ArtifactType::Stdout)?;
                                    stdout.write_line(&msg)?;
                                    test_case.stdout = Some(stdout);
                                }
                                Some(stdout) => {
                                    stdout.write_line(&msg)?;
                                }
                            }
                        }
                        TestEvent::Finish => {
                            successful_completion = true;
                            break;
                        }
                    }
                }
            },
            complete => break,
        }
    }

    let mut test_cases_in_progress: Vec<String> = test_cases_in_progress.into_iter().collect();
    test_cases_in_progress.sort();

    if test_cases_in_progress.len() != 0 {
        match outcome {
            Outcome::Passed | Outcome::Failed => {
                outcome = Outcome::Inconclusive;
            }
            _ => {}
        }
        writer
            .write_line("\nThe following test(s) never completed:")
            .unwrap_or_else(|e| error!("Cannot write logs: {:?}", e));
        for t in test_cases_in_progress {
            writer
                .write_line(&format!("{}", t))
                .unwrap_or_else(|e| error!("Cannot write logs: {:?}", e));
        }
    }

    let mut test_cases_executed: Vec<String> =
        test_cases_executed.into_iter().map(|(case_name, _)| case_name).collect();
    let mut test_cases_passed: Vec<String> = test_cases_passed.into_iter().collect();
    let mut test_cases_failed: Vec<String> = test_cases_failed.into_iter().collect();

    test_cases_executed.sort();
    test_cases_passed.sort();
    test_cases_failed.sort();

    suite_reporter.outcome(&outcome.into())?;
    suite_reporter.record()?;

    Ok(RunResult {
        outcome,
        executed: test_cases_executed,
        passed: test_cases_passed,
        failed: test_cases_failed,
        successful_completion,
    })
}

pub struct TestStreams<'a> {
    pub results: LocalBoxStream<'a, Result<RunResult, anyhow::Error>>,
    pub logs: LogStream,
}

impl<'a> TestStreams<'a> {
    pub fn into_log_and_result(
        self,
    ) -> (LogStream, LocalBoxStream<'a, Result<RunResult, anyhow::Error>>) {
        let Self { results, logs } = self;
        (logs, results)
    }
}

/// Runs the test `count` number of times, and writes logs to writer.
pub async fn run_test<'a, W: output::WriteLine>(
    test_params: TestParams,
    count: u16,
    writer: &'a mut W,
    run_reporter: &'a mut RunReporter,
) -> Result<TestStreams<'a>, anyhow::Error> {
    let run_options = TestRunOptions {
        disabled_tests: test_params.disabled_tests(),
        parallel: test_params.parallel,
        arguments: test_params.test_args.clone(),
    };

    struct FoldArgs<'a, W: output::WriteLine> {
        current_count: u16,
        count: u16,
        suite_instance: test_executor::SuiteInstance,
        invocations: Option<Vec<fidl_fuchsia_test::Invocation>>,
        test_params: TestParams,
        run_options: TestRunOptions,
        writer: &'a mut W,
        run_reporter: &'a mut RunReporter,
    }

    let mut suite_instance = test_executor::SuiteInstance::new(test_executor::SuiteInstanceOpts {
        harness: &test_params.harness,
        test_url: &test_params.test_url,
        force_log_protocol: None,
    })
    .await?;
    let log_stream = suite_instance.take_log_stream().unwrap();

    let args = FoldArgs {
        current_count: 0,
        count,
        suite_instance,
        invocations: None,
        test_params,
        run_options,
        writer,
        run_reporter,
    };

    let results = stream::try_unfold(args, move |mut args| async move {
        if args.current_count >= args.count {
            args.suite_instance.kill()?;
            return Ok(None);
        }

        let invocations = match args.invocations {
            Some(ref i) => i.clone(),
            None => args
                .suite_instance
                .enumerate_tests(&args.test_params.test_filter.as_ref().map(String::as_str))
                .await
                .or_else(|err| {
                    args.suite_instance.kill()?;
                    Err(err)
                })?,
        };

        let mut next_count = args.current_count + 1;
        let result = run_test_for_invocations(
            &args.suite_instance,
            invocations.clone(),
            args.run_options.clone(),
            args.test_params.timeout,
            args.writer,
            args.run_reporter.new_suite(&args.test_params.test_url)?,
        )
        .await
        .or_else(|err| {
            args.suite_instance.kill()?;
            Err(err)
        })?;
        if result.outcome == Outcome::Timedout || result.outcome == Outcome::Error {
            // don't run test again
            next_count = args.count;
        }

        args.invocations = Some(invocations);
        args.current_count = next_count;
        Ok(Some((result, args)))
    })
    .boxed_local();

    Ok(TestStreams { logs: log_stream, results })
}

async fn collect_results(
    test_url: &str,
    count: std::num::NonZeroU16,
    mut stream: LocalBoxStream<'_, Result<RunResult, anyhow::Error>>,
) -> Outcome {
    let mut i: u16 = 1;
    let mut final_outcome = Outcome::Passed;

    loop {
        match stream.try_next().await {
            Err(e) => {
                println!("Test suite encountered error trying to run tests: {:?}", e);
                return Outcome::Error;
            }
            Ok(Some(RunResult { outcome, executed, passed, failed, successful_completion })) => {
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

    let mut stdout_for_results: Box<dyn WriteLine + Send + Sync> = match filter_ansi {
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
    let mut syslog_writer = match reporter.new_artifact(&ArtifactType::Syslog) {
        Ok(reporter_syslog) => MultiplexedWriter::new(reporter_syslog, io::stdout()),
        Err(e) => {
            println!("Test suite encountered error trying to run tests: {:?}", e);
            return Outcome::Error;
        }
    };

    let streams =
        match run_test(test_params, count.get(), &mut stdout_for_results, &mut reporter).await {
            Ok(s) => s,
            Err(e) => {
                println!("Test suite encountered error trying to run tests: {:?}", e);
                return Outcome::Error;
            }
        };

    let (log_stream, result_stream) = streams.into_log_and_result();
    let log_collection_fut = diagnostics::collect_logs(log_stream, &mut syslog_writer, log_opts);
    let results_collection_fut = collect_results(&test_url, count, result_stream);

    let (log_collection_result, mut test_outcome) =
        join!(log_collection_fut, results_collection_fut);

    if count.get() > 1 && test_outcome != Outcome::Passed {
        println!("One or more test runs failed.");
    }

    match log_collection_result {
        Err(e) => {
            println!("Failed to collect logs: {:?}", e);
        }
        Ok(outcome) => match outcome {
            diagnostics::LogCollectionOutcome::Passed => {}
            diagnostics::LogCollectionOutcome::Error { restricted_logs } => {
                test_outcome = Outcome::Failed;
                println!("Test {} produced unexpected high-severity logs:", test_url);
                println!("----------------xxxxx----------------");
                for log in restricted_logs {
                    println!("{}", log);
                }
                println!("----------------xxxxx----------------");
                println!("Failing this test. See: https://fuchsia.dev/fuchsia-src/concepts/testing/logs#restricting_log_severity");
            }
        },
    }

    let report_result = match reporter.outcome(&test_outcome.into()) {
        Ok(()) => reporter.record(),
        Err(e) => Err(e),
    };
    if let Err(e) = report_result {
        println!("Failed to record results: {:?}", e);
    }

    test_outcome
}
