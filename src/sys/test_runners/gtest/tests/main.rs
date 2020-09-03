// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context as _, Error},
    fidl::endpoints,
    fidl_fuchsia_io::DirectoryMarker,
    fidl_fuchsia_sys2 as fsys, fidl_fuchsia_test as ftest,
    fidl_fuchsia_test_manager as ftest_manager,
    ftest_manager::LaunchOptions,
    fuchsia_component::client,
    fuchsia_component::client::connect_to_protocol_at_dir_root,
    futures::{channel::mpsc, prelude::*},
    maplit::{hashmap, hashset},
    pretty_assertions::assert_eq,
    std::{
        collections::{HashMap, HashSet},
        iter::FromIterator,
    },
    test_executor::{DisabledTestHandling, GroupByTestCase, TestEvent, TestResult, TestRunOptions},
};

async fn connect_test_manager() -> Result<ftest_manager::HarnessProxy, Error> {
    let realm = client::connect_to_service::<fsys::RealmMarker>()
        .context("could not connect to Realm service")?;

    let mut child_ref = fsys::ChildRef { name: "test_manager".to_owned(), collection: None };
    let (dir, server_end) = endpoints::create_proxy::<DirectoryMarker>()?;
    realm
        .bind_child(&mut child_ref, server_end)
        .await
        .context("bind_child fidl call failed for test manager")?
        .map_err(|e| format_err!("failed to create test manager: {:?}", e))?;

    connect_to_protocol_at_dir_root::<ftest_manager::HarnessMarker>(&dir)
        .await
        .context("failed to open test suite service")
}

/// Returns SuiteProxy and SuiteControllerProxy. Keep SuiteControllerProxy alive for
/// length of your test run so that your test doesn't die.
async fn launch_test(
    test_url: &str,
) -> Result<(ftest::SuiteProxy, ftest_manager::SuiteControllerProxy), Error> {
    let proxy = connect_test_manager().await?;
    let (suite_proxy, suite_server_end) = fidl::endpoints::create_proxy()?;
    let (controller_proxy, controller_server_end) = fidl::endpoints::create_proxy()?;
    proxy
        .launch_suite(test_url, LaunchOptions {}, suite_server_end, controller_server_end)
        .await
        .context("launch_test call failed")?
        .map_err(|e| format_err!("error launching test: {:?}", e))?;
    Ok((suite_proxy, controller_proxy))
}

async fn run_test(
    test_url: &str,
    test_run_options: TestRunOptions,
) -> Result<Vec<TestEvent>, Error> {
    let (suite_proxy, _keep_alive) = launch_test(test_url).await?;

    let (sender, recv) = mpsc::channel(1);

    let (events, ()) = futures::future::try_join(
        recv.collect::<Vec<_>>().map(Ok),
        test_executor::run_and_collect_results(suite_proxy, sender, None, test_run_options),
    )
    .await
    .context("running test")?;

    let mut test_events = vec![];

    // break logs as they can be grouped in any way.
    for event in events {
        match event {
            TestEvent::LogMessage { test_case_name, mut msg } => {
                if msg.ends_with("\n") {
                    msg.truncate(msg.len() - 1);
                }
                let logs = msg.split("\n");
                for log in logs {
                    test_events.push(TestEvent::LogMessage {
                        test_case_name: test_case_name.clone(),
                        msg: log.to_string(),
                    });
                }
            }
            event => {
                test_events.push(event);
            }
        };
    }

    Ok(test_events)
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
        TestRunOptions { disabled_tests: DisabledTestHandling::Exclude, parallel: Some(10) },
    )
    .await
    .unwrap()
    .into_iter()
    .group_by_test_case_unordered();

    let expected_events = include!("../test_data/sample_tests_golden_events.rsf");

    assert_events_eq(&events, &expected_events);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_run_sample_test_no_concurrent() {
    let test_url = "fuchsia-pkg://fuchsia.com/gtest-runner-example-tests#meta/sample_tests.cm";

    let events = run_test(
        test_url,
        TestRunOptions { disabled_tests: DisabledTestHandling::Exclude, parallel: None },
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
        TestRunOptions { disabled_tests: DisabledTestHandling::Include, parallel: Some(10) },
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
        TestRunOptions { disabled_tests: DisabledTestHandling::Exclude, parallel: Some(10) },
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
        TestRunOptions { disabled_tests: DisabledTestHandling::Exclude, parallel: Some(10) },
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
        TestRunOptions { disabled_tests: DisabledTestHandling::Exclude, parallel: Some(100) },
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
        TestRunOptions { disabled_tests: DisabledTestHandling::Exclude, parallel: Some(5) },
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
