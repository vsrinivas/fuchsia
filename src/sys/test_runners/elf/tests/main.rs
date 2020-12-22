// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context as _, Error},
    fidl::endpoints,
    fidl_fuchsia_io::DirectoryMarker,
    fidl_fuchsia_sys2 as fsys, fidl_fuchsia_test_manager as ftest_manager,
    fuchsia_component::client,
    fuchsia_component::client::connect_to_protocol_at_dir_root,
    futures::{channel::mpsc, prelude::*},
    pretty_assertions::assert_eq,
    test_executor::{DisabledTestHandling, TestEvent, TestResult, TestRunOptions},
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
        .context("failed to open test suite service")
}

async fn run_test(
    test_url: &str,
    test_run_options: TestRunOptions,
) -> Result<Vec<TestEvent>, Error> {
    let harness = connect_test_manager().await?;
    let suite_instance = test_executor::SuiteInstance::new(&harness, test_url).await?;

    let (sender, recv) = mpsc::channel(1);

    let (events, ()) = futures::future::try_join(
        recv.collect::<Vec<_>>().map(Ok),
        suite_instance.run_and_collect_results(sender, None, test_run_options),
    )
    .await
    .context("running test")?;

    Ok(test_runners_test_lib::process_events(events, false))
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_run_passing_test() {
    let test_url = "fuchsia-pkg://fuchsia.com/elf-test-runner-example-tests#meta/passing_test.cm";

    let events = run_test(
        test_url,
        TestRunOptions {
            disabled_tests: DisabledTestHandling::Exclude,
            parallel: None,
            arguments: None,
        },
    )
    .await
    .unwrap();

    let expected_events = vec![
        TestEvent::test_case_started("main"),
        TestEvent::test_case_finished("main", TestResult::Passed),
        TestEvent::test_finished(),
    ];
    assert_eq!(events, expected_events);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_run_failing_test() {
    let test_url = "fuchsia-pkg://fuchsia.com/elf-test-runner-example-tests#meta/failing_test.cm";

    let events = run_test(
        test_url,
        TestRunOptions {
            disabled_tests: DisabledTestHandling::Exclude,
            parallel: None,
            arguments: None,
        },
    )
    .await
    .unwrap();

    let expected_events = vec![
        TestEvent::test_case_started("main"),
        TestEvent::test_case_finished("main", TestResult::Failed),
        TestEvent::test_finished(),
    ];
    assert_eq!(events, expected_events);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_run_test_with_custom_args() {
    let test_url = "fuchsia-pkg://fuchsia.com/elf-test-runner-example-tests#meta/arg_test.cm";

    let events = run_test(
        test_url,
        TestRunOptions {
            disabled_tests: DisabledTestHandling::Exclude,
            parallel: None,
            arguments: Some(vec!["expected_arg".to_owned()]),
        },
    )
    .await
    .unwrap();

    let expected_events = vec![
        TestEvent::test_case_started("main"),
        TestEvent::log_message("main", "Got argv[1]=\"expected_arg\""),
        TestEvent::test_case_finished("main", TestResult::Passed),
        TestEvent::test_finished(),
    ];
    assert_eq!(expected_events, events);
}
