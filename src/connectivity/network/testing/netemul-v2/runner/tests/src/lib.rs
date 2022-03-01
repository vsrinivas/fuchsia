// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_test_manager as ftest_manager;
use test_case::test_case;
use test_manager_test_lib::RunEvent;

pub async fn run_test(test_url: &str) -> Vec<RunEvent> {
    let run_builder =
        fuchsia_component::client::connect_to_protocol::<ftest_manager::RunBuilderMarker>()
            .expect("connect to test manager protocol");
    let builder = test_manager_test_lib::TestBuilder::new(run_builder);
    let suite_instance = builder
        .add_suite(test_url, ftest_manager::RunOptions::EMPTY)
        .await
        .expect("create suite instance");

    let (test_run_result, test_suite_result) = futures_util::future::join(
        builder.run(),
        test_runners_test_lib::process_events(suite_instance, true),
    )
    .await;
    // `TestBuilder::run` returns a `Vec` of `TestRunEvent`s, which only ever
    // include debug data currently, so expect this to be empty.
    let test_run_events = test_run_result.expect("builder execution failed");
    assert_eq!(&test_run_events[..], []);
    let (events, logs) = test_suite_result.expect("failed to process test runner events");
    assert_eq!(&logs[..], [] as [&str; 0]);
    events
}

// Test events don't always occur in a deterministic order, or with
// deterministic contents. For example:
//  * Some test runners randomize the execution order of test cases in a test
//    suite.
//  * The GoogleTest runner prints the seed with which it's randomizing the
//    order of test cases to stdout.
//  * Test runners often emit stdout and stderr events interleaved with test
//    case results, sometimes before or after the test case in which they
//    occurred.
//
// So we check that the expected events are observed in order, but allow other
// events to be emitted as well.
fn assert_contains_in_order<T, I>(container: &[T], contents: I)
where
    T: PartialEq + std::fmt::Debug,
    I: IntoIterator<Item = T>,
{
    let mut expected = contents.into_iter().peekable();
    for element in container {
        let _: Option<T> = expected.next_if_eq(element);
    }
    let remaining: Vec<_> = expected.collect();
    assert_eq!(remaining, [], "failed to find expected elements in {:?}", container);
}

// TODO(https://fxbug.dev/94623): use a package-local (relative) URL and resolve
// it to an absolute URL via a service provided by the package resolver, to
// avoid having to hardcode the package name here.
fn test_url(component: &str) -> String {
    format!("fuchsia-pkg://fuchsia.com/netemul-runner-tests#meta/{component}.cm")
}

fn test_suite_sequence(
    contents: impl IntoIterator<Item = RunEvent>,
    status: ftest_manager::SuiteStatus,
) -> Vec<RunEvent> {
    std::iter::once(RunEvent::suite_started())
        .chain(contents)
        .chain(std::iter::once(RunEvent::suite_stopped(status)))
        .collect()
}

fn test_case_sequence(name: &str, status: ftest_manager::CaseStatus) -> Vec<RunEvent> {
    vec![
        RunEvent::case_found(name),
        RunEvent::case_started(name),
        RunEvent::case_stopped(name, status),
        RunEvent::case_finished(name),
    ]
}

#[test_case(
    "rust-test",
    [
        test_suite_sequence(vec![], ftest_manager::SuiteStatus::Failed),
        test_case_sequence("pass", ftest_manager::CaseStatus::Passed),
        test_case_sequence("fail", ftest_manager::CaseStatus::Failed),
    ];
    "rust"
)]
#[test_case(
    "gtest-test",
    [
        test_suite_sequence(vec![], ftest_manager::SuiteStatus::Failed),
        test_case_sequence("Gtest.Pass", ftest_manager::CaseStatus::Passed),
        test_case_sequence("Gtest.Fail", ftest_manager::CaseStatus::Failed),
        vec![RunEvent::case_stdout("Gtest.Pass", "passing test stdout")],
        vec![RunEvent::case_stdout("Gtest.Fail", "failing test stdout")],
    ];
    "gtest"
)]
#[test_case(
    "elf-test",
    [
        test_suite_sequence(
            test_case_sequence("main", ftest_manager::CaseStatus::Passed),
            ftest_manager::SuiteStatus::Passed,
        ),
        vec![RunEvent::case_stdout("main", "stdout msg")],
    ];
    "elf"
)]
#[fuchsia_async::run_singlethreaded(test)]
async fn delegate_to_appropriate_test_runner(
    test_component: &str,
    expected_events: impl IntoIterator<Item = Vec<RunEvent>>,
) {
    let test_url = test_url(test_component);
    let events = run_test(&test_url).await;
    for expected in expected_events {
        assert_contains_in_order(&events, expected);
    }
}
