// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{format_err, Error},
    fuchsia_async as fasync,
    futures::{channel::mpsc, prelude::*},
    std::collections::HashSet,
    std::fmt,
    std::io::Write,
    test_executor::TestEvent,
};

#[derive(PartialEq, Debug)]
pub enum TestOutcome {
    Passed,
    Failed,
    Inconclusive,
    Error,
}

impl fmt::Display for TestOutcome {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            TestOutcome::Passed => write!(f, "PASSED"),
            TestOutcome::Failed => write!(f, "FAILED"),
            TestOutcome::Inconclusive => write!(f, "INCONCLUSIVE"),
            TestOutcome::Error => write!(f, "ERROR"),
        }
    }
}

// Runs test defined by |url|, and writes logs to writer.
// Returns (Outcome, Names of tests executed, Names of tests passed)
pub async fn run_test<W: Write>(
    url: String,
    writer: &mut W,
) -> Result<(TestOutcome, Vec<String>, Vec<String>), Error> {
    let launcher =
        match fuchsia_component::client::connect_to_service::<fidl_fuchsia_sys::LauncherMarker>() {
            Ok(l) => l,
            Err(e) => return Err(e),
        };

    let (sender, mut recv) = mpsc::channel(1);

    let (remote, test_fut) =
        test_executor::run_test_component(launcher, url, sender).remote_handle();

    fasync::spawn(remote);

    let mut test_outcome = TestOutcome::Passed;

    let mut test_cases_in_progress = HashSet::new();
    let mut test_cases_executed = HashSet::new();
    let mut test_cases_passed = HashSet::new();

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
            TestEvent::TestCaseFinished { test_case_name, outcome } => {
                if !test_cases_in_progress.contains(&test_case_name) {
                    return Err(format_err!(
                        "test case: '{}' was never started, still got a finish event",
                        test_case_name
                    ));
                }
                test_cases_in_progress.remove(&test_case_name);
                let outcome_str = match outcome {
                    test_executor::Outcome::Passed => {
                        test_cases_passed.insert(test_case_name.clone());
                        "PASSED".to_string()
                    }
                    test_executor::Outcome::Failed => {
                        if test_outcome == TestOutcome::Passed {
                            test_outcome = TestOutcome::Failed;
                        }
                        "FAILED".to_string()
                    }
                    test_executor::Outcome::Error => {
                        test_outcome = TestOutcome::Error;
                        "ERROR".to_string()
                    }
                };
                writeln!(writer, "[{}]\t{}", outcome_str, test_case_name)
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
        }
    }

    test_fut.await.map_err(|e| format_err!("Error running test: {}", e))?;

    let mut test_cases_in_progress: Vec<String> = test_cases_in_progress.into_iter().collect();
    test_cases_in_progress.sort();

    if test_cases_in_progress.len() != 0 {
        match test_outcome {
            TestOutcome::Passed | TestOutcome::Failed => {
                test_outcome = TestOutcome::Inconclusive;
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

    Ok((test_outcome, test_cases_executed, test_cases_passed))
}
