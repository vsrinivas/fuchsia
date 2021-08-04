// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod lib;

use {
    crate::lib::run_test,
    fidl_fuchsia_test_manager::{CaseStatus, SuiteStatus},
    pretty_assertions::assert_eq,
    test_manager_test_lib::{GroupRunEventByTestCase as _, RunEvent},
};

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_echo_test() {
    let test_url =
        "fuchsia-pkg://fuchsia.com/rust-test-runner-example#meta/echo_integration_test.cm";
    let (events, _logs) = run_test(test_url, false, Some(10), vec![])
        .await
        .expect(&format!("failed to run test {}", test_url));

    let expected_events = vec![
        RunEvent::suite_started(),
        RunEvent::case_found("test_echo"),
        RunEvent::case_started("test_echo"),
        RunEvent::case_stopped("test_echo", CaseStatus::Passed),
        RunEvent::case_finished("test_echo"),
        RunEvent::suite_stopped(SuiteStatus::Passed),
    ];
    assert_eq!(expected_events, events);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_file_with_no_test() {
    let test_url = "fuchsia-pkg://fuchsia.com/rust-test-runner-example#meta/no_rust_tests.cm";
    let (events, _logs) = run_test(test_url, false, Some(10), vec![]).await.unwrap();

    let expected_events =
        vec![RunEvent::suite_started(), RunEvent::suite_stopped(SuiteStatus::Passed)];
    assert_eq!(expected_events, events);
}
async fn launch_and_run_sample_test_internal(parallel: u16) {
    let test_url = "fuchsia-pkg://fuchsia.com/rust-test-runner-example#meta/sample_rust_tests.cm";
    let (events, _logs) =
        run_test(test_url, false, Some(parallel), vec!["--my_custom_arg2".to_owned()])
            .await
            .unwrap();

    let expected_events = vec![
        RunEvent::suite_started(),
        RunEvent::case_found("my_tests::failing_test"),
        RunEvent::case_started("my_tests::failing_test"),
        RunEvent::case_stopped("my_tests::failing_test", CaseStatus::Failed),
        RunEvent::case_finished("my_tests::failing_test"),
        RunEvent::case_found("my_tests::sample_test_one"),
        RunEvent::case_started("my_tests::sample_test_one"),
        RunEvent::case_stdout("my_tests::sample_test_one", "My only job is not to panic!()"),
        RunEvent::case_stopped("my_tests::sample_test_one", CaseStatus::Passed),
        RunEvent::case_finished("my_tests::sample_test_one"),
        RunEvent::case_found("my_tests::sample_test_two"),
        RunEvent::case_started("my_tests::sample_test_two"),
        RunEvent::case_stdout("my_tests::sample_test_two", "My only job is not to panic!()"),
        RunEvent::case_stopped("my_tests::sample_test_two", CaseStatus::Passed),
        RunEvent::case_finished("my_tests::sample_test_two"),
        RunEvent::case_found("my_tests::passing_test"),
        RunEvent::case_started("my_tests::passing_test"),
        RunEvent::case_stdout("my_tests::passing_test", "My only job is not to panic!()"),
        RunEvent::case_stopped("my_tests::passing_test", CaseStatus::Passed),
        RunEvent::case_finished("my_tests::passing_test"),
        RunEvent::case_found("my_tests::ignored_failing_test"),
        RunEvent::case_started("my_tests::ignored_failing_test"),
        RunEvent::case_stopped("my_tests::ignored_failing_test", CaseStatus::Skipped),
        RunEvent::case_finished("my_tests::ignored_failing_test"),
        RunEvent::case_found("my_tests::ignored_passing_test"),
        RunEvent::case_started("my_tests::ignored_passing_test"),
        RunEvent::case_stopped("my_tests::ignored_passing_test", CaseStatus::Skipped),
        RunEvent::case_finished("my_tests::ignored_passing_test"),
        RunEvent::case_found("my_tests::test_custom_arguments"),
        RunEvent::case_started("my_tests::test_custom_arguments"),
        RunEvent::case_stopped("my_tests::test_custom_arguments", CaseStatus::Passed),
        RunEvent::case_finished("my_tests::test_custom_arguments"),
        RunEvent::case_found("my_tests::test_environ"),
        RunEvent::case_started("my_tests::test_environ"),
        RunEvent::case_stopped("my_tests::test_environ", CaseStatus::Passed),
        RunEvent::case_finished("my_tests::test_environ"),
        RunEvent::suite_stopped(SuiteStatus::Failed),
    ]
    .into_iter()
    .group_by_test_case_unordered();

    assert_eq!(events.last().unwrap(), &RunEvent::suite_stopped(SuiteStatus::Failed));

    let (failing_test_logs, events_without_failing_test_logs): (Vec<RunEvent>, Vec<RunEvent>) =
        events.into_iter().partition(|x| match x {
            RunEvent::CaseStdout { name, stdout_message: _ } => name == "my_tests::failing_test",
            _ => false,
        });

    let events_without_failing_test_logs =
        events_without_failing_test_logs.into_iter().group_by_test_case_unordered();

    assert_eq!(expected_events, events_without_failing_test_logs);

    let panic_message = r"thread 'main' panicked at 'I'm supposed panic!()', ../../src/sys/test_runners/rust/test_data/sample-rust-tests/src/lib.rs:20:9";
    assert!(failing_test_logs.len() > 3, "{:?}", failing_test_logs);
    assert_eq!(
        &failing_test_logs[0..3],
        &[
            RunEvent::case_stdout("my_tests::failing_test", panic_message),
            RunEvent::case_stdout("my_tests::failing_test", "stack backtrace:"),
            RunEvent::case_stdout("my_tests::failing_test", "{{{reset}}}"),
        ]
    );
    assert_eq!(
        failing_test_logs.last().unwrap(),
        &RunEvent::case_stdout("my_tests::failing_test", "test failed.")
    );
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_run_sample_test() {
    launch_and_run_sample_test_internal(10).await;
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_run_sample_test_no_concurrency() {
    launch_and_run_sample_test_internal(1).await;
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_run_sample_test_include_disabled() {
    let test_url = "fuchsia-pkg://fuchsia.com/rust-test-runner-example#meta/sample_rust_tests.cm";
    let (events, _logs) = run_test(test_url, true, Some(10), vec![]).await.unwrap();

    let grouped_events = events.into_iter().group_by_test_case_ordered();

    // Confirm that non-ignored tests are still included.
    let events_failing_test =
        grouped_events.get(&Some("my_tests::failing_test".to_string())).unwrap();
    assert_eq!(
        &events_failing_test.non_artifact_events[0],
        &RunEvent::case_found("my_tests::failing_test")
    );
    assert_eq!(
        &events_failing_test.non_artifact_events[1],
        &RunEvent::case_started("my_tests::failing_test")
    );

    assert_eq!(
        &events_failing_test.stdout_events[1],
        &RunEvent::case_stdout("my_tests::failing_test", "stack backtrace:")
    );

    let events_ignored_failing_test =
        grouped_events.get(&Some("my_tests::ignored_failing_test".to_string())).unwrap();
    assert_eq!(
        events_ignored_failing_test.non_artifact_events.first().unwrap(),
        &RunEvent::case_found("my_tests::ignored_failing_test")
    );
    assert_eq!(
        events_ignored_failing_test.non_artifact_events.last().unwrap(),
        &RunEvent::case_finished("my_tests::ignored_failing_test")
    );

    assert_eq!(
        events_ignored_failing_test
            .non_artifact_events
            .get(events_ignored_failing_test.non_artifact_events.len() - 2)
            .unwrap(),
        &RunEvent::case_stopped("my_tests::ignored_failing_test", CaseStatus::Failed),
    );

    assert!(events_ignored_failing_test.stdout_events.len() > 1, "Expected > 1 log messages");

    let events_ignored_passing_test =
        grouped_events.get(&Some("my_tests::ignored_passing_test".to_string())).unwrap();
    assert_eq!(
        events_ignored_passing_test,
        &vec![
            RunEvent::case_found("my_tests::ignored_passing_test"),
            RunEvent::case_started("my_tests::ignored_passing_test"),
            RunEvent::case_stdout("my_tests::ignored_passing_test", "Everybody ignores me"),
            RunEvent::case_stopped("my_tests::ignored_passing_test", CaseStatus::Passed),
            RunEvent::case_finished("my_tests::ignored_passing_test"),
        ]
        .into_iter()
        .group()
    );
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_parallel_execution() {
    let test_url = "fuchsia-pkg://fuchsia.com/rust-test-runner-example#meta/concurrency-test.cm";
    let (events, _logs) = run_test(test_url, false, Some(100), vec![]).await.unwrap();
    let events = events.into_iter().group_by_test_case_unordered();

    let mut expected_events = vec![RunEvent::suite_started()];

    for i in 1..=5 {
        let s = format!("test_echo{}", i);
        expected_events.extend(vec![
            RunEvent::case_found(&s),
            RunEvent::case_started(&s),
            RunEvent::case_stopped(&s, CaseStatus::Passed),
            RunEvent::case_finished(&s),
        ])
    }
    expected_events.push(RunEvent::suite_stopped(SuiteStatus::Passed));
    let expected_events = expected_events.into_iter().group_by_test_case_unordered();
    assert_eq!(expected_events, events);
}
