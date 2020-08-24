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
    pretty_assertions::assert_eq,
    test_executor::{DisabledTestHandling, TestEvent, TestResult},
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
    disabled_tests: DisabledTestHandling,
) -> Result<Vec<TestEvent>, Error> {
    let (suite_proxy, _keep_alive) = launch_test(test_url).await?;

    let (sender, recv) = mpsc::channel(1);

    let run_options = test_executor::TestRunOptions { disabled_tests };

    let (events, ()) = futures::future::try_join(
        recv.collect::<Vec<_>>().map(Ok),
        test_executor::run_and_collect_results(suite_proxy, sender, None, run_options),
    )
    .await
    .context("running test")?;

    let mut test_events = vec![];

    // break logs as they can be grouped in any way.
    for event in events {
        match event {
            TestEvent::LogMessage { test_case_name, msg } => {
                let logs = msg.split("\n");
                for log in logs {
                    if log.len() > 0 {
                        test_events.push(TestEvent::LogMessage {
                            test_case_name: test_case_name.clone(),
                            msg: log.to_string(),
                        });
                    }
                }
            }
            event => {
                test_events.push(event);
            }
        };
    }

    Ok(test_events)
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_echo_test() {
    let test_url = "fuchsia-pkg://fuchsia.com/rust-test-runner-example#meta/echo-test-realm.cm";
    let events = run_test(test_url, DisabledTestHandling::Exclude).await.unwrap();

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
    let events = run_test(test_url, DisabledTestHandling::Exclude).await.unwrap();

    let expected_events = vec![TestEvent::test_finished()];
    assert_eq!(expected_events, events);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_run_sample_test() {
    use test_executor::GroupByTestCase as _;

    let test_url = "fuchsia-pkg://fuchsia.com/rust-test-runner-example#meta/sample_rust_tests.cm";
    let events = run_test(test_url, DisabledTestHandling::Exclude).await.unwrap();

    let expected_events = vec![
        TestEvent::test_case_started("my_tests::failing_test"),
        TestEvent::test_case_finished("my_tests::failing_test", TestResult::Failed),
        TestEvent::test_case_started("my_tests::sample_test_one"),
        TestEvent::log_message("my_tests::sample_test_one", "My only job is not to panic!()"),
        TestEvent::test_case_finished("my_tests::sample_test_one", TestResult::Passed),
        TestEvent::test_case_started("my_tests::sample_test_two"),
        TestEvent::log_message("my_tests::sample_test_two", "My only job is not to panic!()"),
        TestEvent::test_case_finished("my_tests::sample_test_two", TestResult::Passed),
        TestEvent::test_case_started("my_tests::passing_test"),
        TestEvent::log_message("my_tests::passing_test", "My only job is not to panic!()"),
        TestEvent::test_case_finished("my_tests::passing_test", TestResult::Passed),
        TestEvent::test_case_started("my_tests::ignored_failing_test"),
        TestEvent::test_case_finished("my_tests::ignored_failing_test", TestResult::Skipped),
        TestEvent::test_case_started("my_tests::ignored_passing_test"),
        TestEvent::test_case_finished("my_tests::ignored_passing_test", TestResult::Skipped),
        TestEvent::test_finished(),
    ]
    .into_iter()
    .group_by_test_case_unordered();

    assert_eq!(events.last().unwrap(), &TestEvent::test_finished());

    let (failing_test_logs, events_without_failing_test_logs): (Vec<TestEvent>, Vec<TestEvent>) =
        events.into_iter().partition(|x| match x {
            TestEvent::LogMessage { test_case_name, msg: _ } => {
                test_case_name == "my_tests::failing_test"
            }
            _ => false,
        });

    let events_without_failing_test_logs =
        events_without_failing_test_logs.into_iter().group_by_test_case_unordered();

    assert_eq!(expected_events, events_without_failing_test_logs);

    let panic_message = r"thread 'main' panicked at 'I'm supposed panic!()', ../../src/sys/test_runners/rust/test_data/sample-rust-tests/src/lib.rs:19:9";
    assert!(failing_test_logs.len() > 3, "{:?}", failing_test_logs);
    assert_eq!(
        &failing_test_logs[0..3],
        &[
            TestEvent::log_message("my_tests::failing_test", panic_message),
            TestEvent::log_message("my_tests::failing_test", "stack backtrace:"),
            TestEvent::log_message("my_tests::failing_test", "{{{reset}}}"),
        ]
    );
    assert_eq!(
        failing_test_logs.last().unwrap(),
        &TestEvent::log_message("my_tests::failing_test", "test failed.")
    );
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_run_sample_test_include_disabled() {
    use test_executor::GroupByTestCase as _;

    let test_url = "fuchsia-pkg://fuchsia.com/rust-test-runner-example#meta/sample_rust_tests.cm";
    let events = run_test(test_url, DisabledTestHandling::Include).await.unwrap();

    let grouped_events = events.into_iter().group_by_test_case_ordered();

    // Confirm that non-ignored tests are still included.
    let events_failing_test =
        grouped_events.get(&Some("my_tests::failing_test".to_string())).unwrap();
    assert_eq!(&events_failing_test[0], &TestEvent::test_case_started("my_tests::failing_test"));
    assert_eq!(
        &events_failing_test[2],
        &TestEvent::log_message("my_tests::failing_test", "stack backtrace:")
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
                TestEvent::LogMessage { test_case_name: _, msg: _ } => true,
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
            TestEvent::log_message("my_tests::ignored_passing_test", "Everybody ignores me"),
            TestEvent::test_case_finished("my_tests::ignored_passing_test", TestResult::Passed),
        ]
    );
}

/*
// fxb/50793: ignore doesn't work right now.
// Stress test with a very large gtest suite.
#[fuchsia_async::run_singlethreaded(test)]
#[ignore = "Timeouts on CQ bots"]
async fn launch_and_run_hugetest() {
    let test_url = "fuchsia-pkg://fuchsia.com/rust-test-runner-example#meta/huge_rust_tests.cm";
    let mut events = run_test(test_url).await.unwrap();

    let mut expected_events = vec![];

    for i in 1..=1000 {
        let s = format!("test_{}", i);
        expected_events.extend(vec![
            TestEvent::test_case_started(&s),
            TestEvent::test_case_finished(&s, TestResult::Passed),
        ])
    }
    expected_events.push(TestEvent::test_finished());
    expected_events.sort();
    events.sort();
    assert_eq!(expected_events, events);
}
*/
