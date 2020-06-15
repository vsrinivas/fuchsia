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
    fuchsia_component::client::connect_to_protocol_at_dir,
    futures::{channel::mpsc, prelude::*},
    test_executor::TestEvent,
    test_executor::TestResult,
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

    connect_to_protocol_at_dir::<ftest_manager::HarnessMarker>(&dir)
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

async fn run_test(test_url: &str) -> Result<Vec<TestEvent>, Error> {
    let (suite_proxy, _keep_alive) = launch_test(test_url).await?;

    let (sender, recv) = mpsc::channel(1);

    let (events, ()) = futures::future::try_join(
        recv.collect::<Vec<_>>().map(Ok),
        test_executor::run_and_collect_results(suite_proxy, sender, None),
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
async fn launch_and_run_sample_test() {
    let test_url = "fuchsia-pkg://fuchsia.com/gtest-runner-example-tests#meta/sample_tests.cm";
    let mut events = run_test(test_url).await.unwrap();

    let mut expected_events = vec![
        TestEvent::test_case_started("SampleTest1.Crashing"),
        TestEvent::test_case_started("SampleTest1.SimpleFail"),
        TestEvent::log_message(
            "SampleTest1.SimpleFail",
            "../../src/sys/test_runners/gtest/test_data/sample_tests.cc:9: Failure",
        ),
        TestEvent::log_message("SampleTest1.SimpleFail", "Value of: true"),
        TestEvent::log_message("SampleTest1.SimpleFail", "  Actual: true"),
        TestEvent::log_message("SampleTest1.SimpleFail", "Expected: false"),
        TestEvent::test_case_started("SampleFixture.Test2"),
        TestEvent::test_case_started("Tests/SampleParameterizedTestFixture.Test/0"),
        TestEvent::test_case_finished(
            "Tests/SampleParameterizedTestFixture.Test/0",
            TestResult::Passed,
        ),
        TestEvent::test_case_started("Tests/SampleParameterizedTestFixture.Test/2"),
        TestEvent::test_case_finished(
            "Tests/SampleParameterizedTestFixture.Test/2",
            TestResult::Passed,
        ),
        TestEvent::test_case_started("Tests/SampleParameterizedTestFixture.Test/3"),
        TestEvent::test_case_finished(
            "Tests/SampleParameterizedTestFixture.Test/3",
            TestResult::Passed,
        ),
        TestEvent::test_case_started("WriteToStdout.TestPass"),
        TestEvent::log_message("WriteToStdout.TestPass", "first msg"),
        TestEvent::log_message("WriteToStdout.TestPass", "second msg"),
        TestEvent::test_case_finished("WriteToStdout.TestPass", TestResult::Passed),
        TestEvent::test_case_started("WriteToStdout.TestFail"),
        TestEvent::log_message("WriteToStdout.TestFail", "first msg"),
        TestEvent::log_message(
            "WriteToStdout.TestFail",
            "../../src/sys/test_runners/gtest/test_data/sample_tests.cc:37: Failure",
        ),
        TestEvent::log_message("WriteToStdout.TestFail", "Value of: true"),
        TestEvent::log_message("WriteToStdout.TestFail", "  Actual: true"),
        TestEvent::log_message("WriteToStdout.TestFail", "Expected: false"),
        TestEvent::log_message("WriteToStdout.TestFail", "second msg"),
        TestEvent::test_case_finished("WriteToStdout.TestFail", TestResult::Failed),
        TestEvent::test_case_started("SampleDisabled.DISABLED_Test1"),
        TestEvent::test_case_finished("SampleTest1.SimpleFail", TestResult::Failed),
        TestEvent::log_message("SampleTest1.Crashing", "Test exited abnormally"),
        TestEvent::test_case_finished("SampleTest1.Crashing", TestResult::Failed),
        TestEvent::test_case_started("SampleFixture.Test1"),
        TestEvent::test_case_finished("SampleDisabled.DISABLED_Test1", TestResult::Passed),
        TestEvent::test_case_started("Tests/SampleParameterizedTestFixture.Test/1"),
        TestEvent::test_case_started("SampleTest2.SimplePass"),
        TestEvent::test_case_finished("SampleTest2.SimplePass", TestResult::Passed),
        TestEvent::test_case_finished("SampleFixture.Test1", TestResult::Passed),
        TestEvent::test_case_finished("SampleFixture.Test2", TestResult::Passed),
        TestEvent::test_case_finished(
            "Tests/SampleParameterizedTestFixture.Test/1",
            TestResult::Passed,
        ),
        TestEvent::test_finished(),
    ];
    expected_events.sort();
    events.sort();
    assert_eq!(expected_events, events);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_run_empty_test() {
    let test_url = "fuchsia-pkg://fuchsia.com/gtest-runner-example-tests#meta/empty_test.cm";
    let events = run_test(test_url).await.unwrap();

    let expected_events = vec![TestEvent::test_finished()];

    assert_eq!(expected_events, events);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_echo_test() {
    let test_url = "fuchsia-pkg://fuchsia.com/gtest-runner-example-tests#meta/echo_test_realm.cm";
    let events = run_test(test_url).await.unwrap();

    let expected_events = vec![
        TestEvent::test_case_started("EchoTest.TestEcho"),
        TestEvent::test_case_finished("EchoTest.TestEcho", TestResult::Passed),
        TestEvent::test_finished(),
    ];
    assert_eq!(expected_events, events);
}

/*
// fxb/50793: ignore doesn't work right now.
// Stress test with a very large gtest suite.
#[fuchsia_async::run_singlethreaded(test)]
#[ignore = "Timeouts on CQ bots"]
async fn launch_and_run_hugetest() {
    let test_url = "fuchsia-pkg://fuchsia.com/gtest-runner-example-tests#meta/huge_gtest.cm";
    let events = run_test(test_url).await.unwrap();

    let mut expected_events = vec![];

    for i in 0..1000 {
        let s = format!("HugeStress/HugeTest.Test/{}", i);
        expected_events.extend(vec![
            TestEvent::test_case_started(&s),
            TestEvent::test_case_finished(&s, TestResult::Passed),
        ])
    }
    expected_events.push(TestEvent::test_finished());
    assert_eq!(expected_events, events);
}
*/
