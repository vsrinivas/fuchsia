// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This unit tests a library which uses v2 framework APIs, so it needs to be launched as a
//! v2 component.

use {
    anyhow::{format_err, Context as _, Error},
    fasync::OnSignals,
    fidl::endpoints::{self, Proxy},
    fidl_fuchsia_io::DirectoryMarker,
    fidl_fuchsia_sys2 as fsys, fidl_fuchsia_test as ftest,
    fidl_fuchsia_test_manager as ftest_manager,
    ftest_manager::LaunchOptions,
    fuchsia_async as fasync,
    fuchsia_component::client,
    fuchsia_zircon as zx,
    futures::{channel::mpsc, prelude::*},
    maplit::hashmap,
    pretty_assertions::assert_eq,
    std::mem::drop,
    test_executor::{DisabledTestHandling, GroupByTestCase, TestEvent, TestResult, TestRunOptions},
};

async fn connect_test_manager() -> Result<ftest_manager::HarnessProxy, Error> {
    let realm = client::connect_to_protocol::<fsys::RealmMarker>()
        .context("could not connect to Realm service")?;

    let mut child_ref = fsys::ChildRef { name: "test_manager".to_owned(), collection: None };
    let (dir, server_end) = endpoints::create_proxy::<DirectoryMarker>()?;
    realm
        .bind_child(&mut child_ref, server_end)
        .await
        .context("bind_child fidl call failed for test manager")?
        .map_err(|e| format_err!("failed to create test manager: {:?}", e))?;

    client::connect_to_protocol_at_dir_root::<ftest_manager::HarnessMarker>(&dir)
        .context("failed to open test suite service")
}

/// Returns SuiteProxy and LegacySuiteControllerProxy. Keep LegacySuiteControllerProxy alive for
/// length of your test run so that your test doesn't die.
async fn launch_test(
    test_url: &str,
) -> Result<(ftest::SuiteProxy, ftest_manager::LegacySuiteControllerProxy), Error> {
    let harness = connect_test_manager().await?;
    let suite_instance = test_executor::SuiteInstance::new(test_executor::SuiteInstanceOpts {
        harness: &harness,
        test_url,
        force_log_protocol: None,
    })
    .await?;
    Ok(suite_instance.into_proxies())
}

async fn run_test(
    test_url: &str,
    test_run_options: TestRunOptions,
    force_log_protocol: Option<test_executor::LogStreamProtocol>,
) -> Result<(Vec<TestEvent>, Vec<String>), Error> {
    let harness = connect_test_manager().await?;
    let mut suite_instance = test_executor::SuiteInstance::new(test_executor::SuiteInstanceOpts {
        harness: &harness,
        test_url,
        force_log_protocol,
    })
    .await?;

    let log_stream = suite_instance.take_log_stream().expect("got log stream");

    let (sender, recv) = mpsc::channel(1);

    let results_fut = async move {
        suite_instance.run_and_collect_results(sender, None, test_run_options).await?;
        suite_instance.kill()?;
        Ok::<(), anyhow::Error>(())
    };
    let (events, logs, ()) = futures::try_join!(
        recv.collect::<Vec<_>>().map(Ok),
        log_stream.collect::<Vec<_>>().map(Ok),
        results_fut,
    )
    .context("running test")?;

    let mut collected_logs = vec![];
    for log_result in logs {
        let log = log_result?;
        collected_logs.push(log.msg().unwrap().to_string());
    }

    let mut test_events = vec![];

    // break stdout messages as they can come in any order.
    for event in events {
        match event {
            TestEvent::StdoutMessage { test_case_name, mut msg } => {
                if msg.ends_with("\n") {
                    msg.truncate(msg.len() - 1)
                }
                let logs = msg.split("\n");
                for log in logs {
                    test_events.push(TestEvent::StdoutMessage {
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

    Ok((test_events, collected_logs))
}

#[fuchsia_async::run_singlethreaded(test)]
async fn killing_controller_should_kill_test() {
    let (suite_proxy, keep_alive) =
        launch_test("fuchsia-pkg://fuchsia.com/example-tests#meta/echo_test_realm.cm")
            .await
            .unwrap();

    drop(keep_alive);

    let suite_channel = suite_proxy.into_channel().unwrap();
    // wait for suite server to die.
    let signals = OnSignals::new(&suite_channel, zx::Signals::OBJECT_PEER_CLOSED);
    signals.await.expect("wait signal failed");
}

#[fuchsia_async::run_singlethreaded(test)]
async fn calling_kill_should_kill_test() {
    let (suite_proxy, controller) =
        launch_test("fuchsia-pkg://fuchsia.com/example-tests#meta/echo_test_realm.cm")
            .await
            .unwrap();

    controller.kill().unwrap();

    let suite_channel = suite_proxy.into_channel().unwrap();
    // wait for suite server to die.
    let signals = OnSignals::new(&suite_channel, zx::Signals::OBJECT_PEER_CLOSED);
    signals.await.expect("wait signal failed");
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_echo_test() {
    let test_url = "fuchsia-pkg://fuchsia.com/example-tests#meta/echo_test_realm.cm";
    let (events, logs) = run_test(
        test_url,
        TestRunOptions {
            disabled_tests: DisabledTestHandling::Exclude,
            parallel: None,
            arguments: vec![],
        },
        None,
    )
    .await
    .unwrap();
    let events = events.into_iter().group_by_test_case_unordered();

    let expected_events = hashmap! {
        Some("EchoTest".to_string()) => vec![
            TestEvent::test_case_started("EchoTest"),
            TestEvent::test_case_finished("EchoTest", TestResult::Passed),
        ],
        None =>  vec![
            TestEvent::test_finished(),
        ]
    };

    assert_eq!(logs, Vec::<String>::new());
    assert_eq!(&expected_events, &events);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_no_on_finished() {
    let test_url =
        "fuchsia-pkg://fuchsia.com/example-tests#meta/no-onfinished-after-test-example.cm";

    let (events, logs) = run_test(
        test_url,
        TestRunOptions {
            disabled_tests: DisabledTestHandling::Exclude,
            parallel: None,
            arguments: vec![],
        },
        None,
    )
    .await
    .unwrap();
    let events = events.into_iter().group_by_test_case_unordered();

    let test_cases = ["Example.Test1", "Example.Test2", "Example.Test3"];
    let mut expected_events = vec![];
    for case in &test_cases {
        expected_events.push(TestEvent::test_case_started(case));
        for i in 1..=3 {
            expected_events
                .push(TestEvent::stdout_message(case, &format!("log{} for {}", i, case)));
        }
        expected_events.push(TestEvent::test_case_finished(case, TestResult::Passed));
    }
    let expected_events = expected_events.into_iter().group_by_test_case_unordered();

    assert_eq!(&expected_events, &events);
    assert_eq!(logs, Vec::<String>::new());
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_gtest_runner_sample_test() {
    let test_url = "fuchsia-pkg://fuchsia.com/gtest-runner-example-tests#meta/sample_tests.cm";

    let (events, logs) = run_test(
        test_url,
        TestRunOptions {
            disabled_tests: DisabledTestHandling::Exclude,
            parallel: None,
            arguments: vec![],
        },
        None,
    )
    .await
    .unwrap();
    let events = events.into_iter().group_by_test_case_unordered();

    let expected_events =
        include!("../../../test_runners/gtest/test_data/sample_tests_golden_events_legacy.rsf");

    assert_eq!(logs, Vec::<String>::new());
    assert_eq!(&expected_events, &events);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn no_suite_service_test() {
    let (suite_proxy, _keep_alive) =
        launch_test("fuchsia-pkg://fuchsia.com/test_manager_test#meta/no_suite_service.cm")
            .await
            .unwrap();

    let suite_channel = suite_proxy.into_channel().unwrap();
    // wait for suite server to die as test doesn't expose suite service.
    let signals = OnSignals::new(&suite_channel, zx::Signals::OBJECT_PEER_CLOSED);
    signals.await.expect("wait signal failed");
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_not_resolved() {
    let proxy = connect_test_manager().await.unwrap();
    let (_suite_proxy, suite_server_end) = fidl::endpoints::create_proxy().unwrap();
    let (_controller_proxy, controller_server_end) = fidl::endpoints::create_proxy().unwrap();

    assert_eq!(
        proxy
            .launch_suite(
                "fuchsia-pkg://fuchsia.com/test_manager_test#meta/invalid_cml.cm",
                LaunchOptions::EMPTY,
                suite_server_end,
                controller_server_end
            )
            .await
            .unwrap(),
        Err(ftest_manager::LaunchError::InstanceCannotResolve)
    );
}

#[fuchsia_async::run_singlethreaded(test)]
async fn collect_isolated_logs_using_batch() {
    let test_url = "fuchsia-pkg://fuchsia.com/test-manager-diagnostics-tests#meta/test-root.cm";
    let (_events, logs) = run_test(
        test_url,
        TestRunOptions {
            disabled_tests: DisabledTestHandling::Exclude,
            parallel: None,
            arguments: vec![],
        },
        Some(test_executor::LogStreamProtocol::BatchIterator),
    )
    .await
    .unwrap();

    assert_eq!(
        logs,
        vec!["Started diagnostics publisher ".to_owned(), "Finishing through Stop ".to_owned()]
    );
}

#[fuchsia_async::run_singlethreaded(test)]
async fn collect_isolated_logs_using_archive_iterator() {
    let test_url = "fuchsia-pkg://fuchsia.com/test-manager-diagnostics-tests#meta/test-root.cm";
    let (_events, logs) = run_test(
        test_url,
        TestRunOptions {
            disabled_tests: DisabledTestHandling::Exclude,
            parallel: None,
            arguments: vec![],
        },
        Some(test_executor::LogStreamProtocol::ArchiveIterator),
    )
    .await
    .unwrap();

    assert_eq!(
        logs,
        vec!["Started diagnostics publisher ".to_owned(), "Finishing through Stop ".to_owned()]
    );
}
