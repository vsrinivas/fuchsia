// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context as _, Error},
    fidl::endpoints,
    fidl_fuchsia_io::DirectoryMarker,
    fidl_fuchsia_sys2 as fsys, fidl_fuchsia_test_manager as ftest_manager,
    ftest_manager::{CaseStatus, RunOptions, SuiteStatus},
    fuchsia_async as fasync,
    fuchsia_component::client,
    futures::{channel::mpsc, prelude::*},
    pretty_assertions::assert_eq,
    test_manager_test_lib::{GroupRunEventByTestCase, RunEvent, TestBuilder},
};

async fn connect_test_manager() -> Result<ftest_manager::RunBuilderProxy, Error> {
    let realm = client::connect_to_protocol::<fsys::RealmMarker>()
        .context("could not connect to Realm service")?;

    let mut child_ref = fsys::ChildRef { name: "test_manager".to_owned(), collection: None };
    let (dir, server_end) = endpoints::create_proxy::<DirectoryMarker>()?;
    realm
        .open_exposed_dir(&mut child_ref, server_end)
        .await
        .context("open_exposed_dir fidl call failed for test manager")?
        .map_err(|e| format_err!("failed to create test manager: {:?}", e))?;

    client::connect_to_protocol_at_dir_root::<ftest_manager::RunBuilderMarker>(&dir)
        .context("failed to open test suite service")
}

async fn connect_query_server() -> Result<ftest_manager::QueryProxy, Error> {
    let realm = client::connect_to_protocol::<fsys::RealmMarker>()
        .context("could not connect to Realm service")?;

    let mut child_ref = fsys::ChildRef { name: "test_manager".to_owned(), collection: None };
    let (dir, server_end) = endpoints::create_proxy::<DirectoryMarker>()?;
    realm
        .open_exposed_dir(&mut child_ref, server_end)
        .await
        .context("open_exposed_dir fidl call failed for test manager")?
        .map_err(|e| format_err!("failed to create test manager: {:?}", e))?;

    client::connect_to_protocol_at_dir_root::<ftest_manager::QueryMarker>(&dir)
        .context("failed to open test suite service")
}

async fn run_single_test(
    test_url: &str,
    run_options: RunOptions,
) -> Result<(Vec<RunEvent>, Vec<String>), Error> {
    let builder = TestBuilder::new(
        connect_test_manager().await.context("cannot connect to run builder proxy")?,
    );
    let suite_instance =
        builder.add_suite(test_url, run_options).await.context("Cannot create suite instance")?;
    let builder_run = fasync::Task::spawn(async move { builder.run().await });
    let ret = collect_suite_events(suite_instance).await;

    builder_run.await.context("builder execution failed")?;
    ret
}

async fn collect_suite_events(
    suite_instance: test_manager_test_lib::SuiteRunInstance,
) -> Result<(Vec<RunEvent>, Vec<String>), Error> {
    let (sender, mut recv) = mpsc::channel(1);
    let execution_task =
        fasync::Task::spawn(async move { suite_instance.collect_events(sender).await });
    let mut events = vec![];
    let mut log_tasks = vec![];
    while let Some(event) = recv.next().await {
        match event.payload {
            test_manager_test_lib::SuiteEventPayload::RunEvent(RunEvent::CaseStdout {
                name,
                mut stdout_message,
            }) => {
                if stdout_message.ends_with("\n") {
                    stdout_message.truncate(stdout_message.len() - 1)
                }
                let logs = stdout_message.split("\n");
                for log in logs {
                    events.push(RunEvent::case_stdout(name.clone(), log.to_string()));
                }
            }
            test_manager_test_lib::SuiteEventPayload::RunEvent(e) => events.push(e),
            test_manager_test_lib::SuiteEventPayload::SuiteLog { log_stream } => {
                let t = fasync::Task::spawn(log_stream.collect::<Vec<_>>());
                log_tasks.push(t);
            }
            test_manager_test_lib::SuiteEventPayload::TestCaseLog { .. } => {
                panic!("not supported yet!")
            }
        }
    }
    execution_task.await.context("test execution failed")?;

    let mut collected_logs = vec![];
    for t in log_tasks {
        let logs = t.await;
        for log_result in logs {
            let log = log_result?;
            collected_logs.push(log.msg().unwrap().to_string());
        }
    }

    Ok((events, collected_logs))
}

#[fuchsia_async::run_singlethreaded(test)]
async fn calling_kill_should_kill_test() {
    let proxy = connect_test_manager().await.unwrap();
    let builder = TestBuilder::new(proxy);
    let suite = builder
        .add_suite(
            "fuchsia-pkg://fuchsia.com/test_manager_test#meta/hanging_test.cm",
            default_run_option(),
        )
        .await
        .unwrap();
    let _builder_run = fasync::Task::spawn(async move { builder.run().await });
    let (sender, mut recv) = mpsc::channel(1024);

    let controller = suite.controller();
    let task = fasync::Task::spawn(async move { suite.collect_events(sender).await });
    // let the test start
    let _initial_event = recv.next().await.unwrap();
    controller.kill().unwrap();
    // collect rest of the events
    let events = recv.collect::<Vec<_>>().await;
    task.await.unwrap();
    let events = events
        .into_iter()
        .filter_map(|e| match e.payload {
            test_manager_test_lib::SuiteEventPayload::RunEvent(e) => Some(e),
            _ => None,
        })
        .collect::<Vec<_>>();
    // make sure that test never finished
    for event in events {
        match event {
            RunEvent::SuiteStopped { .. } => {
                panic!("should not receive SuiteStopped event as the test was killed. ")
            }
            _ => {}
        }
    }
}

#[fuchsia_async::run_singlethreaded(test)]
async fn calling_builder_kill_should_kill_test() {
    let proxy = connect_test_manager().await.unwrap();
    let builder = TestBuilder::new(proxy);
    let suite = builder
        .add_suite(
            "fuchsia-pkg://fuchsia.com/test_manager_test#meta/hanging_test.cm",
            default_run_option(),
        )
        .await
        .unwrap();
    let proxy = builder.take_proxy();

    let (controller_proxy, controller) = endpoints::create_proxy().unwrap();
    proxy.build(controller).unwrap();
    let (sender, mut recv) = mpsc::channel(1024);
    let _task = fasync::Task::spawn(async move { suite.collect_events(sender).await });
    // let the test start
    let _initial_event = recv.next().await.unwrap();
    controller_proxy.kill().unwrap();
    assert_eq!(controller_proxy.get_events().await.unwrap(), vec![]);
    // collect rest of the events
    let events = recv.collect::<Vec<_>>().await;
    let events = events
        .into_iter()
        .filter_map(|e| match e.payload {
            test_manager_test_lib::SuiteEventPayload::RunEvent(e) => Some(e),
            _ => None,
        })
        .collect::<Vec<_>>();
    // make sure that test never finished
    for event in events {
        match event {
            RunEvent::SuiteStopped { .. } => {
                panic!("should not receive SuiteStopped event as the test was killed. ")
            }
            _ => {}
        }
    }
}

#[fuchsia_async::run_singlethreaded(test)]
async fn calling_stop_should_stop_test() {
    let proxy = connect_test_manager().await.unwrap();
    let builder = TestBuilder::new(proxy);
    // can't use hanging test here as stop will only return once current running test completes.
    let suite = builder
        .add_suite(
            "fuchsia-pkg://fuchsia.com/gtest-runner-example-tests#meta/huge_gtest.cm",
            default_run_option(),
        )
        .await
        .unwrap();
    let _builder_run = fasync::Task::spawn(async move { builder.run().await });
    let (sender, mut recv) = mpsc::channel(1024);

    let controller = suite.controller();
    let task = fasync::Task::spawn(async move { suite.collect_events(sender).await });
    // let the test start
    let _initial_event = recv.next().await.unwrap();
    controller.stop().unwrap();
    // collect rest of the events
    let events = recv.collect::<Vec<_>>().await;
    task.await.unwrap();
    // get suite finished event
    let events = events
        .into_iter()
        .filter_map(|e| match e.payload {
            test_manager_test_lib::SuiteEventPayload::RunEvent(e) => match e {
                RunEvent::SuiteStopped { .. } => Some(e),
                _ => None,
            },
            _ => None,
        })
        .collect::<Vec<_>>();

    assert_eq!(events, vec![RunEvent::suite_stopped(SuiteStatus::Stopped)]);
}

fn default_run_option() -> ftest_manager::RunOptions {
    ftest_manager::RunOptions {
        parallel: None,
        arguments: None,
        run_disabled_tests: Some(false),
        timeout: None,
        case_filters_to_run: None,
        log_iterator: None,
        ..ftest_manager::RunOptions::EMPTY
    }
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_echo_test() {
    let test_url = "fuchsia-pkg://fuchsia.com/example-tests#meta/echo_test_realm.cm";
    let (events, logs) = run_single_test(test_url, default_run_option()).await.unwrap();

    let expected_events = vec![
        RunEvent::suite_started(),
        RunEvent::case_found("EchoTest"),
        RunEvent::case_started("EchoTest"),
        RunEvent::case_stopped("EchoTest", CaseStatus::Passed),
        RunEvent::case_finished("EchoTest"),
        RunEvent::suite_stopped(SuiteStatus::Passed),
    ];

    assert_eq!(logs, Vec::<String>::new());
    assert_eq!(&expected_events, &events);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_no_on_finished() {
    let test_url =
        "fuchsia-pkg://fuchsia.com/example-tests#meta/no-onfinished-after-test-example.cm";

    let (events, logs) = run_single_test(test_url, default_run_option()).await.unwrap();
    let events = events.into_iter().group_by_test_case_unordered();

    let test_cases = ["Example.Test1", "Example.Test2", "Example.Test3"];
    let mut expected_events = vec![RunEvent::suite_started()];
    for case in test_cases {
        expected_events.push(RunEvent::case_found(case));
        expected_events.push(RunEvent::case_started(case));

        for i in 1..=3 {
            expected_events.push(RunEvent::case_stdout(case, format!("log{} for {}", i, case)));
        }
        expected_events.push(RunEvent::case_stopped(case, CaseStatus::Passed));
        expected_events.push(RunEvent::case_finished(case));
    }
    expected_events.push(RunEvent::suite_stopped(SuiteStatus::DidNotFinish));
    let expected_events = expected_events.into_iter().group_by_test_case_unordered();

    assert_eq!(&expected_events, &events);
    assert_eq!(logs, Vec::<String>::new());
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_gtest_runner_sample_test() {
    let test_url = "fuchsia-pkg://fuchsia.com/gtest-runner-example-tests#meta/sample_tests.cm";

    let (events, logs) = run_single_test(test_url, default_run_option()).await.unwrap();
    let events = events.into_iter().group_by_test_case_unordered();

    let expected_events =
        include!("../../../test_runners/gtest/test_data/sample_tests_golden_events.rsf")
            .into_iter()
            .group_by_test_case_unordered();

    assert_eq!(logs, Vec::<String>::new());
    assert_eq!(&expected_events, &events);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn positive_filter_test() {
    let test_url = "fuchsia-pkg://fuchsia.com/gtest-runner-example-tests#meta/sample_tests.cm";
    let mut options = default_run_option();
    options.case_filters_to_run =
        Some(vec!["SampleTest2.SimplePass".into(), "SampleFixture*".into()]);
    let (events, logs) = run_single_test(test_url, options).await.unwrap();
    let events = events.into_iter().group_by_test_case_unordered();

    let expected_events = vec![
        RunEvent::suite_started(),
        RunEvent::case_found("SampleTest2.SimplePass"),
        RunEvent::case_started("SampleTest2.SimplePass"),
        RunEvent::case_stopped("SampleTest2.SimplePass", CaseStatus::Passed),
        RunEvent::case_finished("SampleTest2.SimplePass"),
        RunEvent::case_found("SampleFixture.Test1"),
        RunEvent::case_started("SampleFixture.Test1"),
        RunEvent::case_stopped("SampleFixture.Test1", CaseStatus::Passed),
        RunEvent::case_finished("SampleFixture.Test1"),
        RunEvent::case_found("SampleFixture.Test2"),
        RunEvent::case_started("SampleFixture.Test2"),
        RunEvent::case_stopped("SampleFixture.Test2", CaseStatus::Passed),
        RunEvent::case_finished("SampleFixture.Test2"),
        RunEvent::suite_stopped(SuiteStatus::Passed),
    ]
    .into_iter()
    .group_by_test_case_unordered();

    assert_eq!(logs, Vec::<String>::new());
    assert_eq!(&expected_events, &events);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn negative_filter_test() {
    let test_url = "fuchsia-pkg://fuchsia.com/gtest-runner-example-tests#meta/sample_tests.cm";
    let mut options = default_run_option();
    options.case_filters_to_run = Some(vec![
        "-SampleTest1.*".into(),
        "-Tests/SampleParameterizedTestFixture.*".into(),
        "-SampleDisabled.*".into(),
        "-WriteToStdout.*".into(),
    ]);
    let (events, logs) = run_single_test(test_url, options).await.unwrap();
    let events = events.into_iter().group_by_test_case_unordered();

    let expected_events = vec![
        RunEvent::suite_started(),
        RunEvent::case_found("SampleTest2.SimplePass"),
        RunEvent::case_started("SampleTest2.SimplePass"),
        RunEvent::case_stopped("SampleTest2.SimplePass", CaseStatus::Passed),
        RunEvent::case_finished("SampleTest2.SimplePass"),
        RunEvent::case_found("SampleFixture.Test1"),
        RunEvent::case_started("SampleFixture.Test1"),
        RunEvent::case_stopped("SampleFixture.Test1", CaseStatus::Passed),
        RunEvent::case_finished("SampleFixture.Test1"),
        RunEvent::case_found("SampleFixture.Test2"),
        RunEvent::case_started("SampleFixture.Test2"),
        RunEvent::case_stopped("SampleFixture.Test2", CaseStatus::Passed),
        RunEvent::case_finished("SampleFixture.Test2"),
        RunEvent::suite_stopped(SuiteStatus::Passed),
    ]
    .into_iter()
    .group_by_test_case_unordered();

    assert_eq!(logs, Vec::<String>::new());
    assert_eq!(&expected_events, &events);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn positive_and_negative_filter_test() {
    let test_url = "fuchsia-pkg://fuchsia.com/gtest-runner-example-tests#meta/sample_tests.cm";
    let mut options = default_run_option();
    options.case_filters_to_run =
        Some(vec!["SampleFixture.*".into(), "SampleTest2.*".into(), "-*Test1".into()]);
    let (events, logs) = run_single_test(test_url, options).await.unwrap();
    let events = events.into_iter().group_by_test_case_unordered();

    let expected_events = vec![
        RunEvent::suite_started(),
        RunEvent::case_found("SampleTest2.SimplePass"),
        RunEvent::case_started("SampleTest2.SimplePass"),
        RunEvent::case_stopped("SampleTest2.SimplePass", CaseStatus::Passed),
        RunEvent::case_finished("SampleTest2.SimplePass"),
        RunEvent::case_found("SampleFixture.Test2"),
        RunEvent::case_started("SampleFixture.Test2"),
        RunEvent::case_stopped("SampleFixture.Test2", CaseStatus::Passed),
        RunEvent::case_finished("SampleFixture.Test2"),
        RunEvent::suite_stopped(SuiteStatus::Passed),
    ]
    .into_iter()
    .group_by_test_case_unordered();

    assert_eq!(logs, Vec::<String>::new());
    assert_eq!(&expected_events, &events);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn parallel_tests() {
    let test_url = "fuchsia-pkg://fuchsia.com/gtest-runner-example-tests#meta/sample_tests.cm";
    let mut options = default_run_option();
    options.parallel = Some(10);
    let (events, logs) = run_single_test(test_url, options).await.unwrap();
    let events = events.into_iter().group_by_test_case_unordered();

    let expected_events =
        include!("../../../test_runners/gtest/test_data/sample_tests_golden_events.rsf")
            .into_iter()
            .group_by_test_case_unordered();

    assert_eq!(logs, Vec::<String>::new());
    assert_eq!(&expected_events, &events);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn multiple_test() {
    let gtest_test_url =
        "fuchsia-pkg://fuchsia.com/gtest-runner-example-tests#meta/sample_tests.cm";

    let builder = TestBuilder::new(
        connect_test_manager().await.expect("cannot connect to run builder proxy"),
    );
    let gtest_suite_instance1 = builder
        .add_suite(gtest_test_url, default_run_option())
        .await
        .expect("Cannot create suite instance");
    let gtest_suite_instance2 = builder
        .add_suite(gtest_test_url, default_run_option())
        .await
        .expect("Cannot create suite instance");

    let builder_run = fasync::Task::spawn(async move { builder.run().await });
    let fut1 = collect_suite_events(gtest_suite_instance1);
    let fut2 = collect_suite_events(gtest_suite_instance2);
    let (ret1, ret2) = futures::join!(fut1, fut2);
    let (gtest_events1, gtest_log1) = ret1.unwrap();
    let (gtest_events2, gtest_log2) = ret2.unwrap();

    builder_run.await.expect("builder execution failed");

    let gtest_events1 = gtest_events1.into_iter().group_by_test_case_unordered();
    let gtest_events2 = gtest_events2.into_iter().group_by_test_case_unordered();

    let expected_events =
        include!("../../../test_runners/gtest/test_data/sample_tests_golden_events.rsf")
            .into_iter()
            .group_by_test_case_unordered();

    assert_eq!(gtest_log1, Vec::<String>::new());
    assert_eq!(gtest_log2, Vec::<String>::new());
    assert_eq!(&expected_events, &gtest_events1);
    assert_eq!(&expected_events, &gtest_events2);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn no_suite_service_test() {
    let proxy = connect_test_manager().await.unwrap();
    let builder = TestBuilder::new(proxy);
    let suite = builder
        .add_suite(
            "fuchsia-pkg://fuchsia.com/test_manager_test#meta/no_suite_service.cm",
            default_run_option(),
        )
        .await
        .unwrap();
    let _builder_run = fasync::Task::spawn(async move { builder.run().await });
    let (sender, _recv) = mpsc::channel(1024);
    let err = suite
        .collect_events(sender)
        .await
        .expect_err("this should return instance not found error");
    let err = err.downcast::<test_manager_test_lib::SuiteLaunchError>().unwrap();
    // as test doesn't expose suite service, enumeration of test cases will fail.
    assert_eq!(err, test_manager_test_lib::SuiteLaunchError::CaseEnumeration);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_not_resolved() {
    let proxy = connect_test_manager().await.unwrap();
    let builder = TestBuilder::new(proxy);
    let suite = builder
        .add_suite(
            "fuchsia-pkg://fuchsia.com/test_manager_test#meta/invalid_cml.cm",
            default_run_option(),
        )
        .await
        .unwrap();
    let _builder_run = fasync::Task::spawn(async move { builder.run().await });
    let (sender, _recv) = mpsc::channel(1024);
    let err = suite
        .collect_events(sender)
        .await
        .expect_err("this should return instance not found error");
    let err = err.downcast::<test_manager_test_lib::SuiteLaunchError>().unwrap();
    assert_eq!(err, test_manager_test_lib::SuiteLaunchError::InstanceCannotResolve);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn collect_isolated_logs_using_default_log_iterator() {
    let test_url = "fuchsia-pkg://fuchsia.com/test-manager-diagnostics-tests#meta/test-root.cm";
    let (_events, logs) = run_single_test(test_url, default_run_option()).await.unwrap();

    assert_eq!(
        logs,
        vec!["Started diagnostics publisher ".to_owned(), "Finishing through Stop ".to_owned()]
    );
}

#[fuchsia_async::run_singlethreaded(test)]
async fn collect_isolated_logs_using_batch() {
    let test_url = "fuchsia-pkg://fuchsia.com/test-manager-diagnostics-tests#meta/test-root.cm";
    let mut options = default_run_option();
    options.log_iterator = Some(ftest_manager::LogsIteratorOption::BatchIterator);
    let (_events, logs) = run_single_test(test_url, options).await.unwrap();

    assert_eq!(
        logs,
        vec!["Started diagnostics publisher ".to_owned(), "Finishing through Stop ".to_owned()]
    );
}

#[fuchsia_async::run_singlethreaded(test)]
async fn collect_isolated_logs_using_archive_iterator() {
    let test_url = "fuchsia-pkg://fuchsia.com/test-manager-diagnostics-tests#meta/test-root.cm";
    let mut options = default_run_option();
    options.log_iterator = Some(ftest_manager::LogsIteratorOption::ArchiveIterator);
    let (_events, logs) = run_single_test(test_url, options).await.unwrap();

    assert_eq!(
        logs,
        vec!["Started diagnostics publisher ".to_owned(), "Finishing through Stop ".to_owned()]
    );
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_v1_v2_bridge_test() {
    let test_url = "fuchsia-pkg://fuchsia.com/test_manager_test#meta/v2_test_runs_v1_component.cm";

    let (events, logs) = run_single_test(test_url, default_run_option()).await.unwrap();
    let events = events.into_iter().group_by_test_case_unordered();

    let expected_events = vec![
        RunEvent::suite_started(),
        RunEvent::case_found("launch_and_test_v1_component"),
        RunEvent::case_started("launch_and_test_v1_component"),
        RunEvent::case_stopped("launch_and_test_v1_component", CaseStatus::Passed),
        RunEvent::case_finished("launch_and_test_v1_component"),
        RunEvent::case_found("launch_v1_logging_component"),
        RunEvent::case_started("launch_v1_logging_component"),
        RunEvent::case_stopped("launch_v1_logging_component", CaseStatus::Passed),
        RunEvent::case_found("test_debug_data_for_v1_component"),
        RunEvent::case_started("test_debug_data_for_v1_component"),
        RunEvent::case_stopped("test_debug_data_for_v1_component", CaseStatus::Passed),
        RunEvent::case_finished("test_debug_data_for_v1_component"),
        RunEvent::case_finished("launch_v1_logging_component"),
        RunEvent::case_found("enclosing_env_services"),
        RunEvent::case_started("enclosing_env_services"),
        RunEvent::case_stopped("enclosing_env_services", CaseStatus::Passed),
        RunEvent::case_finished("enclosing_env_services"),
        RunEvent::suite_stopped(SuiteStatus::Passed),
    ]
    .into_iter()
    .group_by_test_case_unordered();

    assert_eq!(&expected_events, &events);

    // logged by child v1 component.
    assert_eq!(
        logs,
        vec![
            "my debug message.".to_string(),
            "my info message.".to_string(),
            "my warn message.".to_string()
        ]
    );
}

#[fuchsia_async::run_singlethreaded(test)]
async fn enumerate_invalid_test() {
    let proxy = connect_query_server().await.unwrap();
    let (_iterator, server_end) = endpoints::create_proxy().unwrap();
    let err = proxy
        .enumerate("fuchsia-pkg://fuchsia.com/test_manager_test#meta/invalid_cml.cm", server_end)
        .await
        .unwrap()
        .expect_err("This should error out as we have invalid test");
    assert_eq!(err, ftest_manager::LaunchError::InstanceCannotResolve);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn enumerate_echo_test() {
    let proxy = connect_query_server().await.unwrap();
    let (iterator, server_end) = endpoints::create_proxy().unwrap();
    proxy
        .enumerate("fuchsia-pkg://fuchsia.com/example-tests#meta/echo_test_realm.cm", server_end)
        .await
        .unwrap()
        .expect("This should not fail");

    let mut cases = vec![];
    loop {
        let mut c = iterator.get_next().await.unwrap();
        if c.is_empty() {
            break;
        }
        cases.append(&mut c);
    }
    assert_eq!(
        cases.into_iter().map(|c| c.name.unwrap()).collect::<Vec<_>>(),
        vec!["EchoTest".to_string()]
    );
}

#[fuchsia_async::run_singlethreaded(test)]
async fn enumerate_huge_test() {
    let proxy = connect_query_server().await.unwrap();
    let (iterator, server_end) = endpoints::create_proxy().unwrap();
    proxy
        .enumerate(
            "fuchsia-pkg://fuchsia.com/gtest-runner-example-tests#meta/huge_gtest.cm",
            server_end,
        )
        .await
        .unwrap()
        .expect("This should not fail");

    let mut cases = vec![];
    loop {
        let mut c = iterator.get_next().await.unwrap();
        if c.is_empty() {
            break;
        }
        cases.append(&mut c);
    }
    let expected_cases = (0..=999)
        .into_iter()
        .map(|n| format!("HugeStress/HugeTest.Test/{}", n))
        .collect::<Vec<_>>();

    assert_eq!(cases.into_iter().map(|c| c.name.unwrap()).collect::<Vec<_>>(), expected_cases);
}
