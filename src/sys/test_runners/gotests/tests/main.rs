// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    fidl_fuchsia_test_manager as ftest_manager,
    ftest_manager::{CaseStatus, RunOptions, SuiteStatus},
    fuchsia_async as fasync,
    pretty_assertions::assert_eq,
    regex::Regex,
    test_manager_test_lib::{GroupRunEventByTestCase, RunEvent},
};

pub async fn run_test(
    test_url: &str,
    run_disabled_tests: bool,
    parallel: Option<u16>,
    test_args: Vec<String>,
) -> Result<Vec<RunEvent>, Error> {
    let time_taken = Regex::new(r" \(.*?\)$").unwrap();
    let run_builder = test_runners_test_lib::connect_to_test_manager().await?;
    let builder = test_manager_test_lib::TestBuilder::new(run_builder);
    let run_options = RunOptions {
        run_disabled_tests: Some(run_disabled_tests),
        parallel,
        arguments: Some(test_args),
        ..RunOptions::EMPTY
    };
    let suite_instance =
        builder.add_suite(test_url, run_options).await.context("Cannot create suite instance")?;
    let builder_run = fasync::Task::spawn(async move { builder.run().await });
    let (mut events, _logs) = test_runners_test_lib::process_events(suite_instance, false).await?;
    for event in events.iter_mut() {
        match event {
            RunEvent::CaseStdout { name, stdout_message } => {
                let log = time_taken.replace(&stdout_message, "");
                *event = RunEvent::case_stdout(name.to_string(), log.to_string());
            }
            _ => {}
        }
    }
    builder_run.await.context("builder execution failed")?;
    Ok(events)
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_echo_test() {
    let test_url = "fuchsia-pkg://fuchsia.com/go-test-runner-example#meta/echo-test-realm.cm";
    let events = run_test(test_url, false, Some(10), vec![]).await.unwrap();

    let expected_events = vec![
        RunEvent::suite_started(),
        RunEvent::case_found("TestEcho"),
        RunEvent::case_started("TestEcho"),
        RunEvent::case_stopped("TestEcho", CaseStatus::Passed),
        RunEvent::case_finished("TestEcho"),
        RunEvent::suite_stopped(SuiteStatus::Passed),
    ];
    assert_eq!(expected_events, events);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_file_with_no_test() {
    let test_url = "fuchsia-pkg://fuchsia.com/go-test-runner-example#meta/empty_go_test.cm";
    let events = run_test(test_url, false, Some(10), vec![]).await.unwrap();

    let expected_events =
        vec![RunEvent::suite_started(), RunEvent::suite_stopped(SuiteStatus::Passed)];
    assert_eq!(expected_events, events);
}

async fn launch_and_run_sample_test_helper(parallel: Option<u16>) {
    let test_url = "fuchsia-pkg://fuchsia.com/go-test-runner-example#meta/sample_go_test.cm";
    let mut events = run_test(test_url, false, parallel, vec!["-my_custom_flag_2".to_owned()])
        .await
        .unwrap()
        .into_iter()
        .group_by_test_case_unordered();

    // handle crashing test case separately, as it contains a stack trace we can't match on.
    let run_events_for_crashing = events
        .remove(&Some("TestCrashing".to_string()))
        .expect("Expect run events for TestCrashing test case");
    assert_eq!(
        run_events_for_crashing.non_artifact_events,
        vec![
            RunEvent::case_found("TestCrashing"),
            RunEvent::case_started("TestCrashing"),
            RunEvent::case_stopped("TestCrashing", CaseStatus::Failed),
            RunEvent::case_finished("TestCrashing"),
        ]
    );
    assert!(run_events_for_crashing.stdout_events.is_empty());
    assert_eq!(
        &run_events_for_crashing.stderr_events[..5],
        &vec![
            RunEvent::case_stderr("TestCrashing", "panic: This will crash"),
            RunEvent::case_stderr("TestCrashing", " [recovered]"),
            RunEvent::case_stderr("TestCrashing", "\tpanic: This will crash"),
            RunEvent::case_stderr("TestCrashing", ""),
            RunEvent::case_stderr("TestCrashing", ""),
        ]
    );
    assert_eq!(
        run_events_for_crashing.stderr_events.last().unwrap(),
        &RunEvent::case_stderr("TestCrashing", "Test exited abnormally"),
    );

    let expected_events = vec![
        RunEvent::suite_started(),

        RunEvent::case_found("TestFailing"),
        RunEvent::case_started("TestFailing"),
        RunEvent::case_stdout("TestFailing", "    sample_go_test.go:25: This will fail"),
        RunEvent::case_stopped("TestFailing", CaseStatus::Failed),
        RunEvent::case_finished("TestFailing"),

        RunEvent::case_found("TestPassing"),
        RunEvent::case_started("TestPassing"),
        RunEvent::case_stdout("TestPassing", "This test will pass"),
        RunEvent::case_stdout("TestPassing", "It will also print this line"),
        RunEvent::case_stdout("TestPassing", "And this line"),
        RunEvent::case_stopped("TestPassing", CaseStatus::Passed),
        RunEvent::case_finished("TestPassing"),

        RunEvent::case_found("TestPrefix"),
        RunEvent::case_started("TestPrefix"),
        RunEvent::case_stdout("TestPrefix", "Testing that given two tests where one test is prefix of another can execute independently."),
        RunEvent::case_stopped("TestPrefix", CaseStatus::Passed),
        RunEvent::case_finished("TestPrefix"),

        RunEvent::case_found("TestSkipped"),
        RunEvent::case_started("TestSkipped"),
        RunEvent::case_stdout("TestSkipped", "    sample_go_test.go:33: Skipping this test"),
        RunEvent::case_stopped("TestSkipped", CaseStatus::Skipped),
        RunEvent::case_finished("TestSkipped"),

        RunEvent::case_found("TestSubtests"),
        RunEvent::case_started("TestSubtests"),
        RunEvent::case_stdout("TestSubtests", "=== RUN   TestSubtests/Subtest1"),
        RunEvent::case_stdout("TestSubtests", "=== RUN   TestSubtests/Subtest2"),
        RunEvent::case_stdout("TestSubtests", "=== RUN   TestSubtests/Subtest3"),
        RunEvent::case_stdout("TestSubtests", "    --- PASS: TestSubtests/Subtest1"),
        RunEvent::case_stdout("TestSubtests", "    --- PASS: TestSubtests/Subtest2"),
        RunEvent::case_stdout("TestSubtests", "    --- PASS: TestSubtests/Subtest3"),
        RunEvent::case_stopped("TestSubtests", CaseStatus::Passed),
        RunEvent::case_finished("TestSubtests"),

        RunEvent::case_found("TestPrefixExtra"),
        RunEvent::case_started("TestPrefixExtra"),
        RunEvent::case_stdout("TestPrefixExtra", "Testing that given two tests where one test is prefix of another can execute independently."),
        RunEvent::case_stopped("TestPrefixExtra", CaseStatus::Passed),
        RunEvent::case_finished("TestPrefixExtra"),

        RunEvent::case_found("TestPrintMultiline"),
        RunEvent::case_started("TestPrintMultiline"),
        RunEvent::case_stdout("TestPrintMultiline", "This test will print the msg in multi-line."),
        RunEvent::case_stopped("TestPrintMultiline", CaseStatus::Passed),
        RunEvent::case_finished("TestPrintMultiline"),

        RunEvent::case_found("TestCustomArg"),
        RunEvent::case_started("TestCustomArg"),
        RunEvent::case_stopped("TestCustomArg", CaseStatus::Passed),
        RunEvent::case_finished("TestCustomArg"),

        RunEvent::case_found("TestCustomArg2"),
        RunEvent::case_started("TestCustomArg2"),
        RunEvent::case_stopped("TestCustomArg2", CaseStatus::Passed),
        RunEvent::case_finished("TestCustomArg2"),

        RunEvent::case_found("TestEnviron"),
        RunEvent::case_started("TestEnviron"),
        RunEvent::case_stopped("TestEnviron", CaseStatus::Passed),
        RunEvent::case_finished("TestEnviron"),

        RunEvent::suite_stopped(SuiteStatus::Failed)
    ].into_iter().group_by_test_case_unordered();

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
    let events = run_test(test_url, false, Some(5), vec![])
        .await
        .unwrap()
        .into_iter()
        .group_by_test_case_unordered();

    let mut expected_events = vec![RunEvent::suite_started()];
    for i in 1..=5 {
        let s = format!("Test{}", i);
        expected_events.extend(vec![
            RunEvent::case_found(&s),
            RunEvent::case_started(&s),
            RunEvent::case_stopped(&s, CaseStatus::Passed),
            RunEvent::case_finished(&s),
        ])
    }
    expected_events.push(RunEvent::suite_stopped(SuiteStatus::Passed));
    let expected_events = expected_events.into_iter().group_by_test_case_unordered();
    assert_eq!(events, expected_events);
}
