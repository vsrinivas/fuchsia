// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    fidl_fuchsia_test_manager::HarnessMarker,
    fuchsia_async as fasync,
    fuchsia_async::TimeoutExt,
    fuchsia_zircon as zx,
    futures::{channel::mpsc, future::abortable, prelude::*},
    std::collections::HashSet,
    std::fmt,
    std::io::Write,
    std::sync::atomic::{AtomicBool, Ordering},
    std::sync::Arc,
    test_executor::TestEvent,
};

#[derive(PartialEq, Debug)]
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

    /// Suite protocol completed without error.
    pub successful_completion: bool,
}

/// Runs test defined by `url`, and writes logs to writer.
/// |timeout|: Test timeout.should be more than zero.
pub async fn run_test<W: Write>(
    url: String,
    writer: &mut W,
    timeout: Option<u32>,
) -> Result<RunResult, Error> {
    let harness = fuchsia_component::client::connect_to_service::<HarnessMarker>()?;

    let (sender, mut recv) = mpsc::channel(1);

    let (remote, test_fut) =
        test_executor::run_v2_test_component(harness, url, sender).remote_handle();

    let timed_out = Arc::new(AtomicBool::new(false));
    let timed_out_clone = timed_out.clone();
    match timeout {
        Some(timeout) => {
            if timeout > 0 {
                let (remote, abort_handle) = abortable(remote);
                let timeout = fasync::Time::after(zx::Duration::from_seconds(timeout as _));
                let timeout_fut = remote
                    .on_timeout(timeout, move || {
                        abort_handle.abort();
                        timed_out_clone.store(true, Ordering::SeqCst);
                        Ok(())
                    })
                    .map(drop);
                fasync::spawn(timeout_fut);
            } else {
                return Err(format_err!("timeout should be more than zero"));
            }
        }
        None => fasync::spawn(remote),
    }

    let mut outcome = Outcome::Passed;

    let mut test_cases_in_progress = HashSet::new();
    let mut test_cases_executed = HashSet::new();
    let mut test_cases_passed = HashSet::new();

    let mut successful_completion = false;

    while let Some(test_event) = recv.next().await {
        match test_event {
            TestEvent::TestCaseStarted { test_case_name } => {
                if test_cases_executed.contains(&test_case_name) {
                    return Err(format_err!("test case: '{}' started twice", test_case_name));
                }
                writeln!(writer, "[RUNNING]\t{}", test_case_name).expect("Cannot write logs");
                test_cases_in_progress.insert(test_case_name.clone());
                test_cases_executed.insert(test_case_name);
            }
            TestEvent::TestCaseFinished { test_case_name, result } => {
                if !test_cases_in_progress.contains(&test_case_name) {
                    return Err(format_err!(
                        "test case: '{}' was never started, still got a finish event",
                        test_case_name
                    ));
                }
                test_cases_in_progress.remove(&test_case_name);
                let result_str = match result {
                    test_executor::TestResult::Passed => {
                        test_cases_passed.insert(test_case_name.clone());
                        "PASSED".to_string()
                    }
                    test_executor::TestResult::Failed => {
                        if outcome == Outcome::Passed {
                            outcome = Outcome::Failed;
                        }
                        "FAILED".to_string()
                    }
                    test_executor::TestResult::Skipped => "SKIPPED".to_string(),
                    test_executor::TestResult::Error => {
                        outcome = Outcome::Error;
                        "ERROR".to_string()
                    }
                };
                writeln!(writer, "[{}]\t{}", result_str, test_case_name)
                    .expect("Cannot write logs");
            }
            TestEvent::LogMessage { test_case_name, msg } => {
                if !test_cases_executed.contains(&test_case_name) {
                    return Err(format_err!(
                        "test case: '{}' was never started, still got a log",
                        test_case_name
                    ));
                }
                let msgs = msg.trim().split("\n");
                for msg in msgs {
                    writeln!(writer, "[{}]\t{}", test_case_name, msg).expect("Cannot write logs");
                }
            }
            TestEvent::Finish => {
                successful_completion = true;
                break;
            }
        }
    }

    let timed_out = timed_out.load(Ordering::SeqCst);
    if timed_out {
        outcome = Outcome::Timedout;
    } else {
        test_fut.await.map_err(|e| format_err!("Error running test: {}", e))?;
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
        writeln!(writer, "\nThe following test(s) never completed:").expect("Cannot write logs");
        for t in test_cases_in_progress {
            writeln!(writer, "{}", t).expect("Cannot write logs");
        }
    }

    let mut test_cases_executed: Vec<String> = test_cases_executed.into_iter().collect();
    let mut test_cases_passed: Vec<String> = test_cases_passed.into_iter().collect();

    test_cases_executed.sort();
    test_cases_passed.sort();

    Ok(RunResult {
        outcome: outcome,
        executed: test_cases_executed,
        passed: test_cases_passed,
        successful_completion: successful_completion,
    })
}
