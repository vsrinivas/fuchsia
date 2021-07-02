// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod lib;

use {
    crate::lib::{assert_events_eq, run_test},
    fidl_fuchsia_test_manager as ftest_manager,
    ftest_manager::{CaseStatus, SuiteStatus},
    test_manager_test_lib::{GroupRunEventByTestCase, RunEvent},
};

// Stress test with a very large gtest suite.
#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_run_hugetest() {
    let test_url = "fuchsia-pkg://fuchsia.com/gtest-runner-example-tests#meta/huge_gtest.cm";
    let (events, _logs) = run_test(
        test_url,
        ftest_manager::RunOptions { parallel: Some(100), ..ftest_manager::RunOptions::EMPTY },
    )
    .await
    .unwrap();
    let events = events.into_iter().group_by_test_case_unordered();

    let mut expected_events = vec![RunEvent::suite_started()];

    for i in 0..1000 {
        let s = format!("HugeStress/HugeTest.Test/{}", i);
        expected_events.extend(vec![
            RunEvent::case_found(&s),
            RunEvent::case_started(&s),
            RunEvent::case_stopped(&s, CaseStatus::Passed),
            RunEvent::case_finished(&s),
        ])
    }
    expected_events.push(RunEvent::suite_stopped(SuiteStatus::Passed));
    let expected_events = expected_events.into_iter().group_by_test_case_unordered();
    assert_events_eq(&expected_events, &events);
}
