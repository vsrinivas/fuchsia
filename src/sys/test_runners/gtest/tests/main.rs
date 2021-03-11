// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    futures::{channel::mpsc, prelude::*},
    maplit::{hashmap, hashset},
    pretty_assertions::assert_eq,
    std::{
        collections::{HashMap, HashSet},
        iter::FromIterator,
    },
    test_executor::{DisabledTestHandling, GroupByTestCase, TestEvent, TestResult, TestRunOptions},
};

async fn run_test(
    test_url: &str,
    test_run_options: TestRunOptions,
) -> Result<Vec<TestEvent>, Error> {
    let harness = test_runners_test_lib::connect_to_test_manager().await?;
    let suite_instance = test_executor::SuiteInstance::new(test_executor::SuiteInstanceOpts {
        harness: &harness,
        test_url,
        force_log_protocol: None,
    })
    .await?;

    let (sender, recv) = mpsc::channel(1);

    let (events, ()) = futures::future::try_join(
        recv.collect::<Vec<_>>().map(Ok),
        suite_instance.run_and_collect_results(sender, None, test_run_options),
    )
    .await
    .context("running test")?;

    Ok(test_runners_test_lib::process_events(events, false))
}

/// Helper for comparing grouped test events. Produces more readable diffs than diffing the entire
/// two maps.
#[allow(dead_code)]
fn assert_events_eq(
    a: &HashMap<Option<String>, Vec<TestEvent>>,
    b: &HashMap<Option<String>, Vec<TestEvent>>,
) {
    let a_keys: HashSet<Option<String>> = b.keys().cloned().collect();
    let b_keys: HashSet<Option<String>> = a.keys().cloned().collect();
    assert_eq!(a_keys, b_keys);
    for key in b.keys() {
        assert_eq!(b.get(key).unwrap(), a.get(key).unwrap())
    }
}

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

    let expected_events = include!("../test_data/sample_tests_golden_events.rsf");

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

    let expected_events = include!("../test_data/sample_tests_golden_events.rsf");

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

// Stress test with a very large gtest suite.
#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_run_hugetest() {
    let test_url = "fuchsia-pkg://fuchsia.com/gtest-runner-example-tests#meta/huge_gtest.cm";
    let events = run_test(
        test_url,
        TestRunOptions {
            disabled_tests: DisabledTestHandling::Exclude,
            parallel: Some(100),
            arguments: vec![],
        },
    )
    .await
    .unwrap()
    .into_iter()
    .group_by_test_case_unordered();

    let mut expected_events = vec![];

    for i in 0..1000 {
        let s = format!("HugeStress/HugeTest.Test/{}", i);
        expected_events.extend(vec![
            TestEvent::test_case_started(&s),
            TestEvent::test_case_finished(&s, TestResult::Passed),
        ])
    }
    expected_events.push(TestEvent::test_finished());
    let expected_events = expected_events.into_iter().group_by_test_case_unordered();
    assert_events_eq(&expected_events, &events);
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
