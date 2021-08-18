// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    fidl_fuchsia_test_manager as ftest_manager,
    ftest_manager::{CaseStatus, RunOptions, SuiteStatus},
    fuchsia_async as fasync,
    pretty_assertions::assert_eq,
    test_manager_test_lib::RunEvent,
};

const LEGACY_TEST: &str = "legacy_test";

pub async fn run_test(
    test_url: &str,
    run_options: RunOptions,
) -> Result<(Vec<RunEvent>, Vec<String>), Error> {
    let run_builder = test_runners_test_lib::connect_to_test_manager().await?;
    let builder = test_manager_test_lib::TestBuilder::new(run_builder);
    let suite_instance =
        builder.add_suite(test_url, run_options).await.context("Cannot create suite instance")?;
    let builder_run = fasync::Task::spawn(async move { builder.run().await });
    let ret = test_runners_test_lib::process_events(suite_instance, true).await?;
    builder_run.await.context("builder execution failed")?;
    Ok(ret)
}

fn default_options() -> ftest_manager::RunOptions {
    ftest_manager::RunOptions {
        run_disabled_tests: Some(false),
        parallel: None,
        arguments: None,
        timeout: None,
        case_filters_to_run: None,
        log_iterator: None,
        ..ftest_manager::RunOptions::EMPTY
    }
}

fn assert_test_run_and_return_std(events: Vec<RunEvent>, expected_status: CaseStatus) -> String {
    let (std, events): (Vec<RunEvent>, Vec<RunEvent>) =
        events.into_iter().partition(|e| match &e {
            RunEvent::CaseStdout { .. } => true,
            _ => false,
        });
    let mut expected_suite_status = SuiteStatus::Passed;
    if expected_status != CaseStatus::Passed {
        expected_suite_status = SuiteStatus::Failed;
    }
    let expected_events = vec![
        RunEvent::suite_started(),
        RunEvent::case_found(LEGACY_TEST),
        RunEvent::case_started(LEGACY_TEST),
        RunEvent::case_stopped(LEGACY_TEST, expected_status),
        RunEvent::case_finished(LEGACY_TEST),
        RunEvent::suite_stopped(expected_suite_status),
    ];

    assert_eq!(expected_events, events);
    let std = std
        .into_iter()
        .map(|e| match e {
            RunEvent::CaseStdout { name, stdout_message } => {
                assert_eq!(name, LEGACY_TEST);
                stdout_message + "\n"
            }
            e => panic!("invalid event : {:?}", e),
        })
        .collect::<Vec<_>>();
    std.iter().flat_map(|m| m.chars()).collect()
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_echo_test() {
    let test_url = "fuchsia-pkg://fuchsia.com/legacy_test_runner_examples#meta/echo_test.cm";
    let (events, logs) = run_test(test_url, default_options()).await.unwrap();
    let std = assert_test_run_and_return_std(events, CaseStatus::Passed);
    assert!(std.contains("test test_echo ... ok\n"), "std: {}", std);
    assert!(std.contains("test result: ok. 1 passed; 0 failed;"), "std: {}", std);
    assert_eq!(logs.len(), 0, "logs: {:?}", logs);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_simple_test() {
    let test_url = "fuchsia-pkg://fuchsia.com/legacy_test_runner_examples#meta/simple_test.cm";
    let (events, logs) = run_test(test_url, default_options()).await.unwrap();
    let std = assert_test_run_and_return_std(events, CaseStatus::Failed);
    assert!(std.contains("2 FAILED TESTS\n"), "std: {}", std);
    assert!(std.contains("YOU HAVE 2 DISABLED TESTS\n"), "std: {}", std);
    assert!(std.contains("[  PASSED  ] 9 tests.\n"), "std: {}", std);
    assert!(std.contains("12 tests from 6 test suites ran."), "std: {}", std);
    assert_eq!(logs, vec!["info msg", "warn msg"]);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_arguments() {
    // test running disabled test
    let test_url = "fuchsia-pkg://fuchsia.com/legacy_test_runner_examples#meta/simple_test.cm";
    let mut options = default_options();
    options.arguments = vec!["--gtest_also_run_disabled_tests".into()].into();
    let (events, _logs) = run_test(test_url, options).await.unwrap();
    let std = assert_test_run_and_return_std(events, CaseStatus::Failed);
    assert!(std.contains("3 FAILED TESTS\n"), "std: {}", std);
    // no test was skipped
    assert!(!std.contains("DISABLED TESTS\n"), "std: {}", std);
    assert!(std.contains("[  PASSED  ] 10 tests.\n"), "std: {}", std);
    assert!(std.contains("14 tests from 6 test suites ran."), "std: {}", std);

    // test gtest filter
    let test_url = "fuchsia-pkg://fuchsia.com/legacy_test_runner_examples#meta/simple_test.cm";
    let mut options = default_options();
    options.arguments = vec!["--gtest_filter=*SimplePass".into()].into();
    let (events, logs) = run_test(test_url, options).await.unwrap();
    let std = assert_test_run_and_return_std(events, CaseStatus::Passed);
    // no test failed
    assert!(!std.contains("FAILED TESTS\n"), "std: {}", std);
    // no test was skipped
    assert!(!std.contains("DISABLED TESTS\n"), "std: {}", std);
    assert!(std.contains("[  PASSED  ] 1 test.\n"), "std: {}", std);
    assert!(std.contains("1 test from 1 test suite ran."), "std: {}", std);

    assert_eq!(logs.len(), 0, "logs: {:?}", logs);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_with_sys_service() {
    let test_url =
        "fuchsia-pkg://fuchsia.com/legacy_test_runner_examples#meta/test_with_system_service.cm";
    let (events, logs) = run_test(test_url, default_options()).await.unwrap();
    let std = assert_test_run_and_return_std(events, CaseStatus::Passed);
    assert!(std.contains("test system_services ... ok\n"), "std: {}", std);
    assert!(std.contains("test result: ok. 1 passed; 0 failed;"), "std: {}", std);
    assert_eq!(logs.len(), 0, "logs: {:?}", logs);
}
