// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod lib;

use {
    crate::lib::{assert_events_eq, run_test},
    maplit::{hashmap, hashset},
    pretty_assertions::assert_eq,
    std::{collections::HashSet, iter::FromIterator},
    test_executor::{DisabledTestHandling, GroupByTestCase, TestEvent, TestResult, TestRunOptions},
};

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_run_sample_test() {
    let test_url = "fuchsia-pkg://fuchsia.com/gtest-runner-example-tests#meta/sample_tests.cm";

    let events = run_test(
        test_url,
        TestRunOptions {
            disabled_tests: DisabledTestHandling::Exclude,
            parallel: Some(10),
            arguments: vec![],
        },
    )
    .await
    .unwrap()
    .into_iter()
    .group_by_test_case_unordered();

    let expected_events = include!("../test_data/sample_tests_golden_events_legacy.rsf");

    assert_events_eq(&events, &expected_events);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_run_test_with_custom_args() {
    let test_url =
        "fuchsia-pkg://fuchsia.com/gtest-runner-example-tests#meta/test_with_custom_args.cm";

    let events = run_test(
        test_url,
        TestRunOptions {
            disabled_tests: DisabledTestHandling::Exclude,
            parallel: Some(10),
            arguments: vec!["--my_custom_arg2".to_owned()],
        },
    )
    .await
    .unwrap();

    let expected_events = vec![
        TestEvent::test_case_started("TestArg.TestArg"),
        TestEvent::test_case_finished("TestArg.TestArg", TestResult::Passed),
        TestEvent::test_finished(),
    ];
    assert_eq!(expected_events, events);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_run_sample_test_no_concurrent() {
    let test_url = "fuchsia-pkg://fuchsia.com/gtest-runner-example-tests#meta/sample_tests.cm";

    let events = run_test(
        test_url,
        TestRunOptions {
            disabled_tests: DisabledTestHandling::Exclude,
            parallel: None,
            arguments: vec![],
        },
    )
    .await
    .unwrap()
    .into_iter()
    .group_by_test_case_unordered();

    let expected_events = include!("../test_data/sample_tests_golden_events_legacy.rsf");

    assert_events_eq(&events, &expected_events);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_run_sample_test_include_disabled() {
    const TEST_URL: &str =
        "fuchsia-pkg://fuchsia.com/gtest-runner-example-tests#meta/sample_tests.cm";
    let events = run_test(
        TEST_URL,
        TestRunOptions {
            disabled_tests: DisabledTestHandling::Include,
            parallel: Some(10),
            arguments: vec![],
        },
    )
    .await
    .unwrap()
    .into_iter()
    .group_by_test_case_unordered();

    let expected_pass_events = vec![
        TestEvent::test_case_started("SampleDisabled.DISABLED_TestPass"),
        TestEvent::test_case_finished("SampleDisabled.DISABLED_TestPass", TestResult::Passed),
    ];
    let expected_fail_events = hashset![
        TestEvent::test_case_started("SampleDisabled.DISABLED_TestFail"),
        TestEvent::test_case_finished("SampleDisabled.DISABLED_TestFail", TestResult::Failed),
    ];
    let expected_skip_events = vec![
        TestEvent::test_case_started("SampleDisabled.DynamicSkip"),
        TestEvent::stdout_message(
            "SampleDisabled.DynamicSkip",
            "../../src/sys/test_runners/gtest/test_data/sample_tests.cc:25: Skipped",
        ),
        TestEvent::stdout_message("SampleDisabled.DynamicSkip", ""),
        // gtest treats tests that call `GTEST_SKIP()` as `Passed`.
        TestEvent::test_case_finished("SampleDisabled.DynamicSkip", TestResult::Passed),
    ];

    let actual_pass_events =
        events.get(&Some("SampleDisabled.DISABLED_TestPass".to_string())).unwrap();
    assert_eq!(actual_pass_events, &expected_pass_events);

    // Not going to check all of the exact log events.
    let actual_fail_events = HashSet::from_iter(
        events.get(&Some("SampleDisabled.DISABLED_TestFail".to_string())).unwrap().clone(),
    );
    assert!(
        actual_fail_events.is_superset(&expected_fail_events),
        "actual_fail_events: {:?}",
        &actual_fail_events
    );

    let actual_skip_events = events.get(&Some("SampleDisabled.DynamicSkip".to_string())).unwrap();
    assert_eq!(actual_skip_events, &expected_skip_events)
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_run_empty_test() {
    let test_url = "fuchsia-pkg://fuchsia.com/gtest-runner-example-tests#meta/empty_test.cm";
    let events = run_test(
        test_url,
        TestRunOptions {
            disabled_tests: DisabledTestHandling::Exclude,
            parallel: Some(10),
            arguments: vec![],
        },
    )
    .await
    .unwrap();

    let expected_events = vec![TestEvent::test_finished()];

    assert_eq!(expected_events, events);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_echo_test() {
    let test_url = "fuchsia-pkg://fuchsia.com/gtest-runner-example-tests#meta/echo_test_realm.cm";
    let events = run_test(
        test_url,
        TestRunOptions {
            disabled_tests: DisabledTestHandling::Exclude,
            parallel: Some(10),
            arguments: vec![],
        },
    )
    .await
    .unwrap();

    let expected_events = vec![
        TestEvent::test_case_started("EchoTest.TestEcho"),
        TestEvent::test_case_finished("EchoTest.TestEcho", TestResult::Passed),
        TestEvent::test_finished(),
    ];
    assert_eq!(expected_events, events);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_parallel_execution() {
    let test_url = "fuchsia-pkg://fuchsia.com/gtest-runner-example-tests#meta/concurrency-test.cm";
    let events = run_test(
        test_url,
        TestRunOptions {
            disabled_tests: DisabledTestHandling::Exclude,
            parallel: Some(5),
            arguments: vec![],
        },
    )
    .await
    .unwrap()
    .into_iter()
    .group_by_test_case_unordered();

    let mut expected_events = vec![];

    for i in 1..=5 {
        let s = format!("EchoTest.TestEcho{}", i);
        expected_events.extend(vec![
            TestEvent::test_case_started(&s),
            TestEvent::test_case_finished(&s, TestResult::Passed),
        ])
    }
    expected_events.push(TestEvent::test_finished());
    let expected_events = expected_events.into_iter().group_by_test_case_unordered();
    assert_events_eq(&expected_events, &events);
}
