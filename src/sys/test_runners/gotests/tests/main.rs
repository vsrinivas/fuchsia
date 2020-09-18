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
    fuchsia_component::client::connect_to_protocol_at_dir_svc,
    futures::{channel::mpsc, prelude::*},
    pretty_assertions::assert_eq,
    regex::Regex,
    test_executor::GroupByTestCase,
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

    connect_to_protocol_at_dir_svc::<ftest_manager::HarnessMarker>(&dir)
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
    parallel: Option<u16>,
) -> Result<Vec<TestEvent>, Error> {
    let time_taken = Regex::new(r" \(.*?\)$").unwrap();
    let (suite_proxy, _keep_alive) = launch_test(test_url).await?;

    let (sender, recv) = mpsc::channel(1);

    let run_options = test_executor::TestRunOptions { disabled_tests, parallel };

    let (events, ()) = futures::future::try_join(
        recv.collect::<Vec<_>>().map(Ok),
        test_executor::run_and_collect_results(suite_proxy, sender, None, run_options),
    )
    .await
    .context("running test")?;

    let mut test_events = test_runners_test_lib::process_events(events, false);

    for event in test_events.iter_mut() {
        match event {
            TestEvent::LogMessage { test_case_name, msg } => {
                let log = time_taken.replace(&msg, "");
                *event = TestEvent::log_message(test_case_name, &log);
            }
            _ => {}
        }
    }

    Ok(test_events)
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_echo_test() {
    let test_url = "fuchsia-pkg://fuchsia.com/go-test-runner-example#meta/echo-test-realm.cm";
    let events = run_test(test_url, DisabledTestHandling::Exclude, Some(10)).await.unwrap();

    let expected_events = vec![
        TestEvent::test_case_started("TestEcho"),
        TestEvent::test_case_finished("TestEcho", TestResult::Passed),
        TestEvent::test_finished(),
    ];
    assert_eq!(expected_events, events);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_file_with_no_test() {
    let test_url = "fuchsia-pkg://fuchsia.com/go-test-runner-example#meta/empty_go_test.cm";
    let events = run_test(test_url, DisabledTestHandling::Exclude, Some(10)).await.unwrap();

    let expected_events = vec![TestEvent::test_finished()];
    assert_eq!(expected_events, events);
}

async fn launch_and_run_sample_test_helper(parallel: Option<u16>) {
    let test_url = "fuchsia-pkg://fuchsia.com/go-test-runner-example#meta/sample_go_test.cm";
    let mut events = run_test(test_url, DisabledTestHandling::Exclude, parallel).await.unwrap();

    let mut expected_events = vec![
        TestEvent::test_case_started("TestFailing"),
        TestEvent::log_message("TestFailing", "    sample_go_test.go:20: This will fail"),
        TestEvent::test_case_finished("TestFailing", TestResult::Failed),
        TestEvent::test_case_started("TestPassing"),
        TestEvent::test_case_started("TestPrefix"),
        TestEvent::log_message("TestPrefix", "Testing that given two tests where one test is prefix of another can execute independently."),
        TestEvent::test_case_finished("TestPrefix", TestResult::Passed),
        TestEvent::log_message("TestPassing", "This test will pass"),
        TestEvent::log_message("TestPassing", "It will also print this line"),
        TestEvent::log_message("TestPassing", "And this line"),
        TestEvent::test_case_finished("TestPassing", TestResult::Passed),
        TestEvent::test_case_started("TestCrashing"),
        TestEvent::log_message("TestCrashing", "Test exited abnormally"),
        TestEvent::test_case_finished("TestCrashing", TestResult::Failed),
        TestEvent::test_case_started("TestSkipped"),
        TestEvent::log_message("TestSkipped", "    sample_go_test.go:28: Skipping this test"),
        TestEvent::test_case_finished("TestSkipped", TestResult::Skipped),
        TestEvent::test_case_started("TestSubtests"),
        TestEvent::log_message("TestSubtests", "=== RUN   TestSubtests/Subtest1"),
        TestEvent::log_message("TestSubtests", "=== RUN   TestSubtests/Subtest2"),
        TestEvent::log_message("TestSubtests", "=== RUN   TestSubtests/Subtest3"),
        TestEvent::log_message("TestSubtests", "    --- PASS: TestSubtests/Subtest1"),
        TestEvent::log_message("TestSubtests", "    --- PASS: TestSubtests/Subtest2"),
        TestEvent::log_message("TestSubtests", "    --- PASS: TestSubtests/Subtest3"),
        TestEvent::test_case_finished("TestSubtests", TestResult::Passed),
        TestEvent::test_case_started("TestPrefixExtra"),
        TestEvent::log_message("TestPrefixExtra", "Testing that given two tests where one test is prefix of another can execute independently."),
        TestEvent::test_case_finished("TestPrefixExtra", TestResult::Passed),
        TestEvent::test_case_started("TestPrintMultiline"),
        TestEvent::log_message("TestPrintMultiline", "This test will print the msg in multi-line."),
        TestEvent::test_case_finished("TestPrintMultiline", TestResult::Passed),
        TestEvent::test_finished(),
    ];
    assert_eq!(events.last(), Some(&TestEvent::test_finished()));

    // check logs order
    let passed_test_logs: Vec<&TestEvent> = events
        .iter()
        .filter(|x| match x {
            TestEvent::LogMessage { test_case_name, msg: _ } => test_case_name == "TestPassing",
            _ => false,
        })
        .collect();
    assert_eq!(
        passed_test_logs,
        vec![
            &TestEvent::log_message("TestPassing", "This test will pass"),
            &TestEvent::log_message("TestPassing", "It will also print this line"),
            &TestEvent::log_message("TestPassing", "And this line")
        ]
    );

    expected_events.sort();
    events.sort();
    assert_eq!(events, expected_events);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_run_sample_test() {
    launch_and_run_sample_test_helper(Some(10)).await;
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_run_sample_test_no_concurrent() {
    launch_and_run_sample_test_helper(None).await;
}

// This test will hang if test cases are not executed in parallel.
#[fuchsia_async::run_singlethreaded(test)]
async fn test_parallel_execution() {
    let test_url = "fuchsia-pkg://fuchsia.com/go-test-runner-example#meta/concurrency-test.cm";
    let events = run_test(test_url, DisabledTestHandling::Exclude, Some(5))
        .await
        .unwrap()
        .into_iter()
        .group_by_test_case_unordered();

    let mut expected_events = vec![];
    for i in 1..=5 {
        let s = format!("Test{}", i);
        expected_events.extend(vec![
            TestEvent::test_case_started(&s),
            TestEvent::test_case_finished(&s, TestResult::Passed),
        ])
    }
    expected_events.push(TestEvent::test_finished());
    let expected_events = expected_events.into_iter().group_by_test_case_unordered();
    assert_eq!(events, expected_events);
}
