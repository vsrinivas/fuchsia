// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod lib;

use {
    crate::lib::run_test,
    pretty_assertions::assert_eq,
    test_executor::{DisabledTestHandling, GroupByTestCase as _, TestEvent, TestResult},
};

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_echo_test() {
    let test_url = "fuchsia-pkg://fuchsia.com/rust-test-runner-example#meta/echo-test-realm.cm";
    let events = run_test(test_url, DisabledTestHandling::Exclude, Some(10), vec![]).await.unwrap();

    let expected_events = vec![
        TestEvent::test_case_started("test_echo"),
        TestEvent::test_case_finished("test_echo", TestResult::Passed),
        TestEvent::test_finished(),
    ];
    assert_eq!(expected_events, events);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_file_with_no_test() {
    let test_url = "fuchsia-pkg://fuchsia.com/rust-test-runner-example#meta/no_rust_tests.cm";
    let events = run_test(test_url, DisabledTestHandling::Exclude, Some(10), vec![]).await.unwrap();

    let expected_events = vec![TestEvent::test_finished()];
    assert_eq!(expected_events, events);
}
async fn launch_and_run_sample_test_internal(parallel: u16) {
    let test_url = "fuchsia-pkg://fuchsia.com/rust-test-runner-example#meta/sample_rust_tests.cm";
    let events = run_test(
        test_url,
        DisabledTestHandling::Exclude,
        Some(parallel),
        vec!["--my_custom_arg2".to_owned()],
    )
    .await
    .unwrap();

    let expected_events = vec![
        TestEvent::test_case_started("my_tests::failing_test"),
        TestEvent::test_case_finished("my_tests::failing_test", TestResult::Failed),
        TestEvent::test_case_started("my_tests::sample_test_one"),
        TestEvent::stdout_message("my_tests::sample_test_one", "My only job is not to panic!()"),
        TestEvent::test_case_finished("my_tests::sample_test_one", TestResult::Passed),
        TestEvent::test_case_started("my_tests::sample_test_two"),
        TestEvent::stdout_message("my_tests::sample_test_two", "My only job is not to panic!()"),
        TestEvent::test_case_finished("my_tests::sample_test_two", TestResult::Passed),
        TestEvent::test_case_started("my_tests::passing_test"),
        TestEvent::stdout_message("my_tests::passing_test", "My only job is not to panic!()"),
        TestEvent::test_case_finished("my_tests::passing_test", TestResult::Passed),
        TestEvent::test_case_started("my_tests::ignored_failing_test"),
        TestEvent::test_case_finished("my_tests::ignored_failing_test", TestResult::Skipped),
        TestEvent::test_case_started("my_tests::ignored_passing_test"),
        TestEvent::test_case_finished("my_tests::ignored_passing_test", TestResult::Skipped),
        TestEvent::test_case_started("my_tests::test_custom_arguments"),
        TestEvent::test_case_finished("my_tests::test_custom_arguments", TestResult::Passed),
        TestEvent::test_finished(),
    ]
    .into_iter()
    .group_by_test_case_unordered();

    assert_eq!(events.last().unwrap(), &TestEvent::test_finished());

    let (failing_test_logs, events_without_failing_test_logs): (Vec<TestEvent>, Vec<TestEvent>) =
        events.into_iter().partition(|x| match x {
            TestEvent::StdoutMessage { test_case_name, msg: _ } => {
                test_case_name == "my_tests::failing_test"
            }
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
            TestEvent::stdout_message("my_tests::failing_test", panic_message),
            TestEvent::stdout_message("my_tests::failing_test", "stack backtrace:"),
            TestEvent::stdout_message("my_tests::failing_test", "{{{reset}}}"),
        ]
    );
    assert_eq!(
        failing_test_logs.last().unwrap(),
        &TestEvent::stdout_message("my_tests::failing_test", "test failed.")
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
    let events = run_test(test_url, DisabledTestHandling::Include, Some(10), vec![]).await.unwrap();

    let grouped_events = events.into_iter().group_by_test_case_ordered();

    // Confirm that non-ignored tests are still included.
    let events_failing_test =
        grouped_events.get(&Some("my_tests::failing_test".to_string())).unwrap();
    assert_eq!(&events_failing_test[0], &TestEvent::test_case_started("my_tests::failing_test"));
    assert_eq!(
        &events_failing_test[2],
        &TestEvent::stdout_message("my_tests::failing_test", "stack backtrace:")
    );

    let events_ignored_failing_test =
        grouped_events.get(&Some("my_tests::ignored_failing_test".to_string())).unwrap();
    assert_eq!(
        events_ignored_failing_test.first().unwrap(),
        &TestEvent::test_case_started("my_tests::ignored_failing_test")
    );
    assert_eq!(
        events_ignored_failing_test.last().unwrap(),
        &TestEvent::test_case_finished("my_tests::ignored_failing_test", TestResult::Failed)
    );
    assert!(
        events_ignored_failing_test
            .iter()
            .filter(|event| match event {
                TestEvent::StdoutMessage { test_case_name: _, msg: _ } => true,
                _ => false,
            })
            .count()
            > 1,
        "Expected > 1 log messages"
    );

    let events_ignored_passing_test =
        grouped_events.get(&Some("my_tests::ignored_passing_test".to_string())).unwrap();
    assert_eq!(
        events_ignored_passing_test,
        &vec![
            TestEvent::test_case_started("my_tests::ignored_passing_test"),
            TestEvent::stdout_message("my_tests::ignored_passing_test", "Everybody ignores me"),
            TestEvent::test_case_finished("my_tests::ignored_passing_test", TestResult::Passed),
        ]
    );
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_parallel_execution() {
    let test_url = "fuchsia-pkg://fuchsia.com/rust-test-runner-example#meta/concurrency-test.cm";
    let events = run_test(test_url, DisabledTestHandling::Exclude, Some(100), vec![])
        .await
        .unwrap()
        .into_iter()
        .group_by_test_case_unordered();

    let mut expected_events = vec![];

    for i in 1..=5 {
        let s = format!("test_echo{}", i);
        expected_events.extend(vec![
            TestEvent::test_case_started(&s),
            TestEvent::test_case_finished(&s, TestResult::Passed),
        ])
    }
    expected_events.push(TestEvent::test_finished());
    let expected_events = expected_events.into_iter().group_by_test_case_unordered();
    assert_eq!(expected_events, events);
}
