// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    futures::{channel::mpsc, prelude::*},
    pretty_assertions::assert_eq,
    test_executor::{DisabledTestHandling, TestEvent, TestResult, TestRunOptions},
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

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_run_passing_test() {
    let test_url = "fuchsia-pkg://fuchsia.com/elf-test-runner-example-tests#meta/passing_test.cm";

    let events = run_test(
        test_url,
        TestRunOptions {
            disabled_tests: DisabledTestHandling::Exclude,
            parallel: None,
            arguments: vec![],
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
            arguments: vec![],
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
            arguments: vec!["expected_arg".to_owned()],
        },
    )
    .await
    .unwrap();

    let expected_events = vec![
        TestEvent::test_case_started("main"),
        TestEvent::stdout_message("main", "Got argv[1]=\"expected_arg\""),
        TestEvent::test_case_finished("main", TestResult::Passed),
        TestEvent::test_finished(),
    ];
    assert_eq!(expected_events, events);
}
