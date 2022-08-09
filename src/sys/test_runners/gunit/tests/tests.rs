// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    fidl_fuchsia_test_manager as ftest_manager,
    fidl_fuchsia_test_manager::RunOptions,
    ftest_manager::{CaseStatus, SuiteStatus},
    fuchsia_async as fasync,
    maplit::hashset,
    pretty_assertions::assert_eq,
    std::{
        collections::{HashMap, HashSet},
        iter::FromIterator,
    },
    test_manager_test_lib::{GroupRunEventByTestCase, GroupedRunEvents, RunEvent},
};

/*
The only difference between inputs to gunit and gtest framework are the  name of the flags.
The output format from both frameworks is same. So we will just test that we can launch a
simulated gunit test and get correct output. Exhaustive testing can be found at
//src/sys/test_runners/gtest/tests.
*/

pub async fn run_test(
    test_url: &str,
    run_options: RunOptions,
) -> Result<(Vec<RunEvent>, Vec<String>), Error> {
    let run_builder = test_runners_test_lib::connect_to_test_manager().await?;
    let builder = test_manager_test_lib::TestBuilder::new(run_builder);
    let suite_instance =
        builder.add_suite(test_url, run_options).await.context("Cannot create suite instance")?;
    let builder_run = fasync::Task::spawn(async move { builder.run().await });
    let ret = test_runners_test_lib::process_events(suite_instance, false).await;
    builder_run.await.context("builder execution failed")?;
    ret.map(|(mut events, logs)| {
        let () = events.retain(|event| match event {
            RunEvent::CaseStdout { name: _, stdout_message } => {
                // gunit produces this line when tests are randomized. As of
                // this writing, our gunit_main binary *always* randomizes.
                !stdout_message.contains("Note: Randomizing tests' orders with a seed of")
            }
            _ => true,
        });
        (events, logs)
    })
}

/// Helper for comparing grouped test events. Produces more readable diffs than diffing the entire
/// two maps.
pub fn assert_events_eq(
    a: &HashMap<Option<String>, GroupedRunEvents>,
    b: &HashMap<Option<String>, GroupedRunEvents>,
) {
    let a_keys: HashSet<Option<String>> = b.keys().cloned().collect();
    let b_keys: HashSet<Option<String>> = a.keys().cloned().collect();
    assert_eq!(a_keys, b_keys);
    for key in b.keys() {
        assert_eq!(b.get(key).unwrap(), a.get(key).unwrap())
    }
}

fn default_options() -> ftest_manager::RunOptions {
    ftest_manager::RunOptions {
        run_disabled_tests: Some(false),
        parallel: Some(10),
        arguments: None,
        timeout: None,
        case_filters_to_run: None,
        log_iterator: None,
        ..ftest_manager::RunOptions::EMPTY
    }
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_run_sample_test() {
    let test_url = "fuchsia-pkg://fuchsia.com/gunit-runner-example-tests#meta/sample_tests.cm";

    let (events, _logs) = run_test(test_url, default_options()).await.unwrap();
    let events = events.into_iter().group_by_test_case_unordered();

    let expected_events = include!("../../gtest/test_data/sample_tests_golden_events.rsf")
        .into_iter()
        .group_by_test_case_unordered();

    assert_events_eq(&events, &expected_events);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_run_test_with_custom_args() {
    let test_url =
        "fuchsia-pkg://fuchsia.com/gunit-runner-example-tests#meta/test_with_custom_args.cm";
    let mut run_options = default_options();
    run_options.arguments = Some(vec!["--my_custom_arg2".to_owned()]);
    let (events, _logs) = run_test(test_url, run_options).await.unwrap();

    let expected_events = vec![
        RunEvent::suite_started(),
        RunEvent::case_found("TestArg.TestArg"),
        RunEvent::case_started("TestArg.TestArg"),
        RunEvent::case_stopped("TestArg.TestArg", CaseStatus::Passed),
        RunEvent::case_finished("TestArg.TestArg"),
        RunEvent::suite_stopped(SuiteStatus::Passed),
    ];
    assert_eq!(expected_events, events);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_run_test_with_environ() {
    let test_url = "fuchsia-pkg://fuchsia.com/gunit-runner-example-tests#meta/test_with_environ.cm";
    let (events, _logs) = run_test(test_url, default_options()).await.unwrap();

    let expected_events = vec![
        RunEvent::suite_started(),
        RunEvent::case_found("TestEnviron.TestEnviron"),
        RunEvent::case_started("TestEnviron.TestEnviron"),
        RunEvent::case_stopped("TestEnviron.TestEnviron", CaseStatus::Passed),
        RunEvent::case_finished("TestEnviron.TestEnviron"),
        RunEvent::suite_stopped(SuiteStatus::Passed),
    ];
    assert_eq!(expected_events, events);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_run_sample_test_include_disabled() {
    const TEST_URL: &str =
        "fuchsia-pkg://fuchsia.com/gunit-runner-example-tests#meta/sample_tests.cm";
    let mut run_options = default_options();
    run_options.run_disabled_tests = Some(true);
    let (events, _logs) = run_test(TEST_URL, run_options).await.unwrap();
    let events = events.into_iter().group_by_test_case_unordered();

    let expected_pass_events = vec![
        RunEvent::case_found("SampleDisabled.DISABLED_TestPass"),
        RunEvent::case_started("SampleDisabled.DISABLED_TestPass"),
        RunEvent::case_stopped("SampleDisabled.DISABLED_TestPass", CaseStatus::Passed),
        RunEvent::case_finished("SampleDisabled.DISABLED_TestPass"),
    ]
    .into_iter()
    .group();
    let expected_fail_events = hashset![
        RunEvent::case_found("SampleDisabled.DISABLED_TestFail"),
        RunEvent::case_started("SampleDisabled.DISABLED_TestFail"),
        RunEvent::case_stopped("SampleDisabled.DISABLED_TestFail", CaseStatus::Failed),
        RunEvent::case_finished("SampleDisabled.DISABLED_TestFail"),
    ];
    let expected_skip_events = vec![
        RunEvent::case_found("SampleDisabled.DynamicSkip"),
        RunEvent::case_started("SampleDisabled.DynamicSkip"),
        RunEvent::case_stdout(
            "SampleDisabled.DynamicSkip",
            "../../src/sys/test_runners/gtest/test_data/sample_tests.cc:25: Skipped",
        ),
        RunEvent::case_stdout("SampleDisabled.DynamicSkip", ""),
        // gunit does not force run test skipped with `GTEST_SKIP()`
        // https://github.com/google/googletest/issues/3831
        RunEvent::case_stopped("SampleDisabled.DynamicSkip", CaseStatus::Skipped),
        RunEvent::case_finished("SampleDisabled.DynamicSkip"),
    ]
    .into_iter()
    .group();

    let actual_pass_events =
        events.get(&Some("SampleDisabled.DISABLED_TestPass".to_string())).unwrap();
    assert_eq!(actual_pass_events, &expected_pass_events);

    // Not going to check all of the exact log events.
    let actual_fail_events = HashSet::from_iter(
        events
            .get(&Some("SampleDisabled.DISABLED_TestFail".to_string()))
            .unwrap()
            .non_artifact_events
            .clone(),
    );
    assert!(
        actual_fail_events.is_superset(&expected_fail_events),
        "actual_fail_events: {:?}",
        &actual_fail_events
    );

    let actual_skip_events = events.get(&Some("SampleDisabled.DynamicSkip".to_string())).unwrap();
    assert_eq!(actual_skip_events, &expected_skip_events);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_run_empty_test() {
    let test_url = "fuchsia-pkg://fuchsia.com/gunit-runner-example-tests#meta/empty_test.cm";
    let (events, _logs) = run_test(test_url, default_options()).await.unwrap();

    let expected_events =
        vec![RunEvent::suite_started(), RunEvent::suite_stopped(SuiteStatus::Passed)];

    assert_eq!(expected_events, events);
}
