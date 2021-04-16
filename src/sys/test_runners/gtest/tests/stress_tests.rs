// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod lib;

use {
    crate::lib::{assert_events_eq, run_test},
    test_executor::{DisabledTestHandling, GroupByTestCase, TestEvent, TestResult, TestRunOptions},
};

// Stress test with a very large gtest suite.
#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_run_hugetest() {
    let test_url = "fuchsia-pkg://fuchsia.com/gtest-runner-example-tests#meta/huge_gtest.cm";
    let events = run_test(
        test_url,
        TestRunOptions {
            disabled_tests: DisabledTestHandling::Exclude,
            parallel: Some(100),
            arguments: vec![],
        },
    )
    .await
    .unwrap()
    .into_iter()
    .group_by_test_case_unordered();

    let mut expected_events = vec![];

    for i in 0..1000 {
        let s = format!("HugeStress/HugeTest.Test/{}", i);
        expected_events.extend(vec![
            TestEvent::test_case_started(&s),
            TestEvent::test_case_finished(&s, TestResult::Passed),
        ])
    }
    expected_events.push(TestEvent::test_finished());
    let expected_events = expected_events.into_iter().group_by_test_case_unordered();
    assert_events_eq(&expected_events, &events);
}
