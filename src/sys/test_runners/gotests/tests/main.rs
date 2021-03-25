// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    futures::{channel::mpsc, prelude::*},
    pretty_assertions::assert_eq,
    regex::Regex,
    std::collections::HashMap,
    test_executor::GroupByTestCase,
    test_executor::{DisabledTestHandling, TestEvent, TestResult},
};

async fn run_test(
    test_url: &str,
    disabled_tests: DisabledTestHandling,
    parallel: Option<u16>,
    test_args: Vec<String>,
) -> Result<Vec<TestEvent>, Error> {
    let time_taken = Regex::new(r" \(.*?\)$").unwrap();
    let harness = test_runners_test_lib::connect_to_test_manager().await?;
    let suite_instance = test_executor::SuiteInstance::new(test_executor::SuiteInstanceOpts {
        harness: &harness,
        test_url,
        force_log_protocol: None,
    })
    .await?;

    let (sender, recv) = mpsc::channel(1);

    let run_options =
        test_executor::TestRunOptions { disabled_tests, parallel, arguments: test_args };

    let (events, ()) = futures::future::try_join(
        recv.collect::<Vec<_>>().map(Ok),
        suite_instance.run_and_collect_results(sender, None, run_options),
    )
    .await
    .context("running test")?;

    let mut test_events = test_runners_test_lib::process_events(events, false);

    for event in test_events.iter_mut() {
        match event {
            TestEvent::StdoutMessage { test_case_name, msg } => {
                let log = time_taken.replace(&msg, "");
                *event = TestEvent::stdout_message(test_case_name, &log);
            }
            _ => {}
        }
    }

    Ok(test_events)
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_echo_test() {
    let test_url = "fuchsia-pkg://fuchsia.com/go-test-runner-example#meta/echo-test-realm.cm";
    let events = run_test(test_url, DisabledTestHandling::Exclude, Some(10), vec![]).await.unwrap();

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
    let events = run_test(test_url, DisabledTestHandling::Exclude, Some(10), vec![]).await.unwrap();

    let expected_events = vec![TestEvent::test_finished()];
    assert_eq!(expected_events, events);
}

async fn launch_and_run_sample_test_helper(parallel: Option<u16>) {
    let test_url = "fuchsia-pkg://fuchsia.com/go-test-runner-example#meta/sample_go_test.cm";
    let mut events = run_test(
        test_url,
        DisabledTestHandling::Exclude,
        parallel,
        vec!["-my_custom_flag_2".to_owned()],
    )
    .await
    .unwrap();

    assert_eq!(events.pop(), Some(TestEvent::test_finished()));

    #[derive(Debug)]
    enum TestEventMatch {
        Started,
        Finished(TestResult),
        StdoutMatch(&'static str),
    }

    let mut expectations = vec![
        (
            "TestFailing",
            vec![
                TestEventMatch::Started,
                TestEventMatch::StdoutMatch("    sample_go_test.go:25: This will fail"),
                TestEventMatch::Finished(TestResult::Failed),
            ],
        ),
        (
            "TestPassing",
            vec![
                TestEventMatch::Started,
                TestEventMatch::StdoutMatch("This test will pass"),
                TestEventMatch::StdoutMatch("It will also print this line"),
                TestEventMatch::StdoutMatch("And this line"),
                TestEventMatch::Finished(TestResult::Passed),
            ],
        ),
        (
            "TestPrefix",
            vec![
                TestEventMatch::Started,
                TestEventMatch::StdoutMatch("Testing that given two tests where one test is prefix of another can execute independently."),
                TestEventMatch::Finished(TestResult::Passed),
            ],
        ),
        (
            "TestSkipped",
            vec![
                TestEventMatch::Started,
                TestEventMatch::StdoutMatch("    sample_go_test.go:33: Skipping this test"),
                TestEventMatch::Finished(TestResult::Skipped),
            ],
        ),
        (
            "TestSubtests",
            vec![
                TestEventMatch::Started,
                TestEventMatch::StdoutMatch("=== RUN   TestSubtests/Subtest1"),
                TestEventMatch::StdoutMatch("=== RUN   TestSubtests/Subtest2"),
                TestEventMatch::StdoutMatch("=== RUN   TestSubtests/Subtest3"),
                TestEventMatch::StdoutMatch("    --- PASS: TestSubtests/Subtest1"),
                TestEventMatch::StdoutMatch("    --- PASS: TestSubtests/Subtest2"),
                TestEventMatch::StdoutMatch("    --- PASS: TestSubtests/Subtest3"),
                TestEventMatch::Finished(TestResult::Passed),
            ],
        ),
        (
            "TestPrefixExtra",
            vec![
                TestEventMatch::Started,
                TestEventMatch::StdoutMatch("Testing that given two tests where one test is prefix of another can execute independently."),
                TestEventMatch::Finished(TestResult::Passed),
            ],
        ),
        (
            "TestPrintMultiline",
            vec![
                TestEventMatch::Started,
                TestEventMatch::StdoutMatch("This test will print the msg in multi-line."),
                TestEventMatch::Finished(TestResult::Passed),
            ],
        ),
        (
            "TestCustomArg",
            vec![
                TestEventMatch::Started,
                TestEventMatch::Finished(TestResult::Passed),
            ],
        ),
        (
            "TestCustomArg2",
            vec![
                TestEventMatch::Started,
                TestEventMatch::Finished(TestResult::Passed),
            ],
        )
    ]
    .into_iter()
    .collect::<HashMap<_, _>>();

    // Walk through all the events and match against expectations based on test case.
    for event in events {
        let test_case = event
            .test_case_name()
            .unwrap_or_else(|| panic!("unexpected event {:?} without a test case", event));
        // TODO(https://fxbug.dev/27019): Temporarily ignore output from the crashing test to soft
        // transition go runtime changes.
        if test_case == "TestCrashing" {
            continue;
        }
        let expect = expectations
            .get_mut(test_case.as_str())
            .unwrap_or_else(|| {
                panic!("test case {} from event {:?} not covered by expectations", test_case, event)
            })
            .drain(0..1)
            .next()
            .unwrap_or_else(|| panic!("unexpected event {:?}", event));
        match expect {
            TestEventMatch::Started => assert_eq!(event, TestEvent::test_case_started(test_case)),
            TestEventMatch::Finished(result) => {
                assert_eq!(event, TestEvent::test_case_finished(test_case, result))
            }
            TestEventMatch::StdoutMatch(msg) => {
                assert_eq!(event, TestEvent::stdout_message(test_case, msg))
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
    let events = run_test(test_url, DisabledTestHandling::Exclude, Some(5), vec![])
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
