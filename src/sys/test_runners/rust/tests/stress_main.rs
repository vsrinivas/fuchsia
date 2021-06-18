// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod lib;

use {
    crate::lib::run_test,
    fidl_fuchsia_test_manager::{CaseStatus, SuiteStatus},
    test_manager_test_lib::{GroupRunEventByTestCase as _, RunEvent},
};

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_run_hugetest() {
    let test_url = "fuchsia-pkg://fuchsia.com/rust-test-runner-example#meta/huge_rust_tests.cm";
    let (events, _logs) = run_test(test_url, false, Some(100), vec![]).await.unwrap();
    let events = events.into_iter().group_by_test_case_unordered();

    let mut expected_events = vec![];

    for i in 1..=1000 {
        let s = format!("test_{}", i);
        expected_events.extend(vec![
            RunEvent::case_found(&s),
            RunEvent::case_started(&s),
            RunEvent::case_stopped(&s, CaseStatus::Passed),
            RunEvent::case_finished(s),
        ])
    }
    expected_events.push(RunEvent::suite_finished(SuiteStatus::Passed));
    let expected_events = expected_events.into_iter().group_by_test_case_unordered();
    assert_eq!(expected_events, events);
}
