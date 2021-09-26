// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    fidl_fuchsia_test_manager::{CaseStatus, RunOptions, SuiteStatus},
    fuchsia_async as fasync,
    pretty_assertions::assert_eq,
    test_manager_test_lib::RunEvent,
};

pub async fn run_test(test_url: &str) -> Result<(Vec<RunEvent>, Vec<String>), Error> {
    let run_builder = test_runners_test_lib::connect_to_test_manager().await?;
    let builder = test_manager_test_lib::TestBuilder::new(run_builder);
    let suite_instance = builder
        .add_suite(test_url, RunOptions::EMPTY)
        .await
        .context("Cannot create suite instance")?;
    let builder_run = fasync::Task::spawn(async move { builder.run().await });
    let ret = test_runners_test_lib::process_events(suite_instance, true).await?;
    builder_run.await.context("builder execution failed")?;
    Ok(ret)
}

#[fuchsia_async::run_singlethreaded(test)]
async fn always_fail() {
    let test_url = "fuchsia-pkg://fuchsia.com/stress-runner-integration-test#meta/always_fail.cm";
    let (events, _logs) =
        run_test(test_url).await.expect(&format!("failed to run test {}", test_url));

    let expected_events = vec![
        RunEvent::suite_started(),
        RunEvent::case_found("stress_test"),
        RunEvent::case_started("stress_test"),
        RunEvent::case_stopped("stress_test", CaseStatus::Failed),
        RunEvent::case_finished("stress_test"),
        RunEvent::suite_stopped(SuiteStatus::Failed),
        RunEvent::case_stderr(
            "stress_test",
            "[instance_1]: This action is expected to fail immediately",
        ),
    ];
    assert_eq!(expected_events, events);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn always_pass() {
    let test_url = "fuchsia-pkg://fuchsia.com/stress-runner-integration-test#meta/always_pass.cm";
    let (events, _logs) =
        run_test(test_url).await.expect(&format!("failed to run test {}", test_url));

    let expected_events = vec![
        RunEvent::suite_started(),
        RunEvent::case_found("stress_test"),
        RunEvent::case_started("stress_test"),
        RunEvent::case_stopped("stress_test", CaseStatus::Passed),
        RunEvent::case_finished("stress_test"),
        RunEvent::suite_stopped(SuiteStatus::Passed),
    ];
    assert_eq!(expected_events, events);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn timeout() {
    let test_url = "fuchsia-pkg://fuchsia.com/stress-runner-integration-test#meta/timeout.cm";
    let (events, _logs) =
        run_test(test_url).await.expect(&format!("failed to run test {}", test_url));

    let expected_events = vec![
        RunEvent::suite_started(),
        RunEvent::case_found("stress_test"),
        RunEvent::case_started("stress_test"),
        RunEvent::case_stopped("stress_test", CaseStatus::Failed),
        RunEvent::case_finished("stress_test"),
        RunEvent::suite_stopped(SuiteStatus::Failed),
        RunEvent::case_stderr("stress_test", "[instance_1]: Action `takes_too_long` timed out"),
    ];
    assert_eq!(expected_events, events);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn echo() {
    let test_url = "fuchsia-pkg://fuchsia.com/stress-runner-integration-test#meta/echo_test.cm";
    let (events, _logs) =
        run_test(test_url).await.expect(&format!("failed to run test {}", test_url));

    let expected_events = vec![
        RunEvent::suite_started(),
        RunEvent::case_found("stress_test"),
        RunEvent::case_started("stress_test"),
        RunEvent::case_stopped("stress_test", CaseStatus::Passed),
        RunEvent::case_finished("stress_test"),
        RunEvent::suite_stopped(SuiteStatus::Passed),
    ];
    assert_eq!(expected_events, events);
}
