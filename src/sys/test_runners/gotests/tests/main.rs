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
    std::collections::HashMap,
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
    let mut events =
        run_test(test_url, false, parallel, vec!["-my_custom_flag_2".to_owned()]).await.unwrap();

    assert_eq!(events.remove(0), RunEvent::suite_started());
    assert_eq!(events.pop(), Some(RunEvent::suite_stopped(SuiteStatus::Failed)));

    #[derive(Debug)]
    enum RunEventMatch {
        Found,
        Started,
        Stopped(CaseStatus),
        Finished,
        StdoutMatch(&'static str),
        AnyStdout,
    }

    let mut expectations = vec![
        (
            "TestFailing",
            vec![
                RunEventMatch::Found,
                RunEventMatch::Started,
                RunEventMatch::StdoutMatch("    sample_go_test.go:25: This will fail"),
                RunEventMatch::Stopped(CaseStatus::Failed),
                RunEventMatch::Finished,
            ],
        ),
        (
            "TestPassing",
            vec![
                RunEventMatch::Found,
                RunEventMatch::Started,
                RunEventMatch::StdoutMatch("This test will pass"),
                RunEventMatch::StdoutMatch("It will also print this line"),
                RunEventMatch::StdoutMatch("And this line"),
                RunEventMatch::Stopped(CaseStatus::Passed),
                RunEventMatch::Finished,
            ],
        ),
        (
            "TestPrefix",
            vec![
                RunEventMatch::Found,
                RunEventMatch::Started,
                RunEventMatch::StdoutMatch("Testing that given two tests where one test is prefix of another can execute independently."),
                RunEventMatch::Stopped(CaseStatus::Passed),
                RunEventMatch::Finished,
            ],
        ),
        (
            "TestSkipped",
            vec![
                RunEventMatch::Found,
                RunEventMatch::Started,
                RunEventMatch::StdoutMatch("    sample_go_test.go:33: Skipping this test"),
                RunEventMatch::Stopped(CaseStatus::Skipped),
                RunEventMatch::Finished,
            ],
        ),
        (
            "TestSubtests",
            vec![
                RunEventMatch::Found,
                RunEventMatch::Started,
                RunEventMatch::StdoutMatch("=== RUN   TestSubtests/Subtest1"),
                RunEventMatch::StdoutMatch("=== RUN   TestSubtests/Subtest2"),
                RunEventMatch::StdoutMatch("=== RUN   TestSubtests/Subtest3"),
                RunEventMatch::StdoutMatch("    --- PASS: TestSubtests/Subtest1"),
                RunEventMatch::StdoutMatch("    --- PASS: TestSubtests/Subtest2"),
                RunEventMatch::StdoutMatch("    --- PASS: TestSubtests/Subtest3"),
                RunEventMatch::Stopped(CaseStatus::Passed),
                RunEventMatch::Finished,
            ],
        ),
        (
            "TestPrefixExtra",
            vec![
                RunEventMatch::Found,
                RunEventMatch::Started,
                RunEventMatch::StdoutMatch("Testing that given two tests where one test is prefix of another can execute independently."),
                RunEventMatch::Stopped(CaseStatus::Passed),
                RunEventMatch::Finished,
            ],
        ),
        (
            "TestPrintMultiline",
            vec![
                RunEventMatch::Found,
                RunEventMatch::Started,
                RunEventMatch::StdoutMatch("This test will print the msg in multi-line."),
                RunEventMatch::Stopped(CaseStatus::Passed),
                RunEventMatch::Finished,
            ],
        ),
        (
            "TestCustomArg",
            vec![
                RunEventMatch::Found,
                RunEventMatch::Started,
                RunEventMatch::Stopped(CaseStatus::Passed),
                RunEventMatch::Finished,
            ],
        ),
        (
            "TestCustomArg2",
            vec![
                RunEventMatch::Found,
                RunEventMatch::Started,
                RunEventMatch::Stopped(CaseStatus::Passed),
                RunEventMatch::Finished,
            ],
        ),
        (
            "TestEnviron",
            vec![
                RunEventMatch::Found,
                RunEventMatch::Started,
                RunEventMatch::Stopped(CaseStatus::Passed),
                RunEventMatch::Finished,
            ],
        ),
        (
            "TestCrashing",
            vec![
                RunEventMatch::Found,
                RunEventMatch::Started,
                RunEventMatch::StdoutMatch("panic: This will crash"),
                RunEventMatch::StdoutMatch(" [recovered]"),
                RunEventMatch::StdoutMatch("\tpanic: This will crash"),
                RunEventMatch::StdoutMatch(""),
                RunEventMatch::StdoutMatch(""),
                // This test will print a stack trace, we avoid matching on it.
                RunEventMatch::AnyStdout,
                RunEventMatch::AnyStdout,
                RunEventMatch::AnyStdout,
                RunEventMatch::AnyStdout,
                RunEventMatch::AnyStdout,
                RunEventMatch::AnyStdout,
                RunEventMatch::AnyStdout,
                RunEventMatch::AnyStdout,
                RunEventMatch::AnyStdout,
                RunEventMatch::AnyStdout,
                RunEventMatch::AnyStdout,
                RunEventMatch::AnyStdout,
                RunEventMatch::AnyStdout,
                RunEventMatch::StdoutMatch("Test exited abnormally"),
                RunEventMatch::Stopped(CaseStatus::Failed),
                RunEventMatch::Finished,
            ],
        ),
    ]
    .into_iter()
    .collect::<HashMap<_, _>>();

    // Walk through all the events and match against expectations based on test case.
    for event in events {
        let test_case = event
            .test_case_name()
            .unwrap_or_else(|| panic!("unexpected event {:?} without a test case", event));
        let expect = expectations
            .get_mut(test_case.as_str())
            .unwrap_or_else(|| {
                panic!("test case {} from event {:?} not covered by expectations", test_case, event)
            })
            .drain(0..1)
            .next()
            .unwrap_or_else(|| panic!("unexpected event {:?}", event));
        match expect {
            RunEventMatch::Found => assert_eq!(event, RunEvent::case_found(test_case)),
            RunEventMatch::Started => assert_eq!(event, RunEvent::case_started(test_case)),
            RunEventMatch::Stopped(result) => {
                assert_eq!(event, RunEvent::case_stopped(test_case, result))
            }
            RunEventMatch::Finished => {
                assert_eq!(event, RunEvent::case_finished(test_case))
            }
            RunEventMatch::StdoutMatch(msg) => {
                assert_eq!(event, RunEvent::case_stdout(test_case, msg))
            }
            RunEventMatch::AnyStdout => {
                matches::assert_matches!(event, RunEvent::CaseStdout { .. })
            }
        }
    }

    // Check that we have no leftovers in expectations:
    let leftovers = expectations
        .into_iter()
        .map(|(test, expect)| expect.into_iter().map(move |e| (test, e)))
        .flatten()
        .collect::<Vec<_>>();
    assert!(leftovers.is_empty(), "leftover expectations: {:?}", leftovers);
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
