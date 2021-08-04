// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod lib;

use {
    crate::lib::{assert_events_eq, run_test},
    fidl_fuchsia_test_manager as ftest_manager,
    ftest_manager::{CaseStatus, SuiteStatus},
    maplit::hashset,
    pretty_assertions::assert_eq,
    std::{collections::HashSet, iter::FromIterator},
    test_manager_test_lib::{GroupRunEventByTestCase, RunEvent},
};

fn default_options() -> ftest_manager::RunOptions {
    ftest_manager::RunOptions {
        run_disabled_tests: Some(false),
        parallel: Some(10),
        arguments: None,
        timeout: None,
        case_filters_to_run: None,
        log_iterator: None,
        ..ftest_manager::RunOptions::EMPTY
    }
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_run_sample_test() {
    let test_url = "fuchsia-pkg://fuchsia.com/gtest-runner-example-tests#meta/sample_tests.cm";

    let (events, _logs) = run_test(test_url, default_options()).await.unwrap();
    let events = events.into_iter().group_by_test_case_unordered();

    let expected_events = include!("../test_data/sample_tests_golden_events.rsf")
        .into_iter()
        .group_by_test_case_unordered();

    assert_events_eq(&events, &expected_events);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_run_test_with_custom_args() {
    let test_url =
        "fuchsia-pkg://fuchsia.com/gtest-runner-example-tests#meta/test_with_custom_args.cm";
    let mut run_options = default_options();
    run_options.arguments = Some(vec!["--my_custom_arg2".to_owned()]);
    let (events, _logs) = run_test(test_url, run_options).await.unwrap();

    let expected_events = vec![
        RunEvent::suite_started(),
        RunEvent::case_found("TestArg.TestArg"),
        RunEvent::case_started("TestArg.TestArg"),
        RunEvent::case_stopped("TestArg.TestArg", CaseStatus::Passed),
        RunEvent::case_finished("TestArg.TestArg"),
        RunEvent::suite_stopped(SuiteStatus::Passed),
    ];
    assert_eq!(expected_events, events);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_run_test_with_environ() {
    let test_url = "fuchsia-pkg://fuchsia.com/gtest-runner-example-tests#meta/test_with_environ.cm";
    let (events, _logs) = run_test(test_url, default_options()).await.unwrap();

    let expected_events = vec![
        RunEvent::suite_started(),
        RunEvent::case_found("TestEnviron.TestEnviron"),
        RunEvent::case_started("TestEnviron.TestEnviron"),
        RunEvent::case_stopped("TestEnviron.TestEnviron", CaseStatus::Passed),
        RunEvent::case_finished("TestEnviron.TestEnviron"),
        RunEvent::suite_stopped(SuiteStatus::Passed),
    ];
    assert_eq!(expected_events, events);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_run_sample_test_no_concurrent() {
    let test_url = "fuchsia-pkg://fuchsia.com/gtest-runner-example-tests#meta/sample_tests.cm";
    let mut run_options = default_options();
    run_options.parallel = None;
    let (events, _logs) = run_test(test_url, run_options).await.unwrap();
    let events = events.into_iter().group_by_test_case_unordered();

    let expected_events = include!("../test_data/sample_tests_golden_events.rsf")
        .into_iter()
        .group_by_test_case_unordered();

    assert_events_eq(&events, &expected_events);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_run_sample_test_include_disabled() {
    const TEST_URL: &str =
        "fuchsia-pkg://fuchsia.com/gtest-runner-example-tests#meta/sample_tests.cm";
    let mut run_options = default_options();
    run_options.run_disabled_tests = Some(true);
    let (events, _logs) = run_test(TEST_URL, run_options).await.unwrap();
    let events = events.into_iter().group_by_test_case_unordered();

    let expected_pass_events = vec![
        RunEvent::case_found("SampleDisabled.DISABLED_TestPass"),
        RunEvent::case_started("SampleDisabled.DISABLED_TestPass"),
        RunEvent::case_stopped("SampleDisabled.DISABLED_TestPass", CaseStatus::Passed),
        RunEvent::case_finished("SampleDisabled.DISABLED_TestPass"),
    ]
    .into_iter()
    .group();
    let expected_fail_events = hashset![
        RunEvent::case_found("SampleDisabled.DISABLED_TestFail"),
        RunEvent::case_started("SampleDisabled.DISABLED_TestFail"),
        RunEvent::case_stopped("SampleDisabled.DISABLED_TestFail", CaseStatus::Failed),
        RunEvent::case_finished("SampleDisabled.DISABLED_TestFail"),
    ];
    let expected_skip_events = vec![
        RunEvent::case_found("SampleDisabled.DynamicSkip"),
        RunEvent::case_started("SampleDisabled.DynamicSkip"),
        RunEvent::case_stdout(
            "SampleDisabled.DynamicSkip",
            "../../src/sys/test_runners/gtest/test_data/sample_tests.cc:25: Skipped",
        ),
        RunEvent::case_stdout("SampleDisabled.DynamicSkip", ""),
        // gtest treats tests that call `GTEST_SKIP()` as `Passed`.
        RunEvent::case_stopped("SampleDisabled.DynamicSkip", CaseStatus::Passed),
        RunEvent::case_finished("SampleDisabled.DynamicSkip"),
    ]
    .into_iter()
    .group();

    let actual_pass_events =
        events.get(&Some("SampleDisabled.DISABLED_TestPass".to_string())).unwrap();
    assert_eq!(actual_pass_events, &expected_pass_events);

    // Not going to check all of the exact log events.
    let actual_fail_events = HashSet::from_iter(
        events
            .get(&Some("SampleDisabled.DISABLED_TestFail".to_string()))
            .unwrap()
            .non_artifact_events
            .clone(),
    );
    assert!(
        actual_fail_events.is_superset(&expected_fail_events),
        "actual_fail_events: {:?}",
        &actual_fail_events
    );

    let actual_skip_events = events.get(&Some("SampleDisabled.DynamicSkip".to_string())).unwrap();
    assert_eq!(actual_skip_events, &expected_skip_events);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_run_empty_test() {
    let test_url = "fuchsia-pkg://fuchsia.com/gtest-runner-example-tests#meta/empty_test.cm";
    let (events, _logs) = run_test(test_url, default_options()).await.unwrap();

    let expected_events =
        vec![RunEvent::suite_started(), RunEvent::suite_stopped(SuiteStatus::Passed)];

    assert_eq!(expected_events, events);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_echo_test() {
    let test_url = "fuchsia-pkg://fuchsia.com/gtest-runner-example-tests#meta/echo_test_realm.cm";
    let (events, _logs) = run_test(test_url, default_options()).await.unwrap();

    let expected_events = vec![
        RunEvent::suite_started(),
        RunEvent::case_found("EchoTest.TestEcho"),
        RunEvent::case_started("EchoTest.TestEcho"),
        RunEvent::case_stopped("EchoTest.TestEcho", CaseStatus::Passed),
        RunEvent::case_finished("EchoTest.TestEcho"),
        RunEvent::suite_stopped(SuiteStatus::Passed),
    ];
    assert_eq!(expected_events, events);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_parallel_execution() {
    let test_url = "fuchsia-pkg://fuchsia.com/gtest-runner-example-tests#meta/concurrency-test.cm";
    let mut run_options = default_options();
    run_options.parallel = Some(5);
    let (events, _logs) = run_test(test_url, run_options).await.unwrap();
    let events = events.into_iter().group_by_test_case_unordered();

    let mut expected_events = vec![RunEvent::suite_started()];

    for i in 1..=5 {
        let s = format!("EchoTest.TestEcho{}", i);
        expected_events.extend(vec![
            RunEvent::case_found(&s),
            RunEvent::case_started(&s),
            RunEvent::case_stopped(&s, CaseStatus::Passed),
            RunEvent::case_finished(&s),
        ])
    }
    expected_events.push(RunEvent::suite_stopped(SuiteStatus::Passed));
    let expected_events = expected_events.into_iter().group_by_test_case_unordered();
    assert_events_eq(&expected_events, &events);
}
