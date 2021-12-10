// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use diagnostics_data::Severity;
use fidl_fuchsia_test_manager::{LaunchError, RunBuilderMarker};
use futures::prelude::*;
use matches::assert_matches;
use regex::Regex;
use run_test_suite_lib::{output, Outcome, RunTestSuiteError, SuiteRunResult, TestParams};
use std::convert::TryInto;
use std::io::Write;
use std::str::from_utf8;
use test_output_directory::{
    self as directory,
    testing::{ExpectedDirectory, ExpectedSuite, ExpectedTestCase, ExpectedTestRun},
};

/// split and sort output as output can come in any order.
/// `output` is of type vec<u8> and `expected_output` is a string.
macro_rules! assert_output {
    ($output:expr, $expected_output:expr) => {
        let mut expected_output = $expected_output.split("\n").collect::<Vec<_>>();

        // no need to check for lines starting with "[stdout - " and "[stderr - ". We just want to
        // make sure that we are printing important details.
        let mut output = from_utf8(&$output)
            .expect("we should not get utf8 error.")
            .split("\n")
            .filter(|x| {
                let is_stdout = x.starts_with("[stdout - ");
                let is_stderr = x.starts_with("[stderr - ");
                let is_debug_data_warn = is_debug_data_warning(x);
                !(is_stdout || is_stderr || is_debug_data_warn)
            })
            .map(sanitize_log_for_comparison)
            .collect::<Vec<_>>();

        expected_output.sort();
        output.sort();

        assert_eq!(output, expected_output);
    };
}

// TODO(fxbug.dev/61180): once debug data routing is fixed, this log should be gone and this
// function can be deleted.
fn is_debug_data_warning(log: impl AsRef<str>) -> bool {
    log.as_ref().contains("No capability available at path /svc/fuchsia.debugdata.DebugData")
}

fn sanitize_log_for_comparison(log: impl AsRef<str>) -> String {
    let log_timestamp_re = Regex::new(r"^\[\d+.\d+\]\[\d+\]\[\d+\]").unwrap();
    log_timestamp_re.replace_all(log.as_ref(), "[TIMESTAMP][PID][TID]").to_string()
}

fn new_test_params(test_url: &str) -> TestParams {
    TestParams {
        test_url: test_url.to_string(),
        timeout: None,
        test_filters: None,
        also_run_disabled_tests: false,
        test_args: vec![],
        parallel: None,
        max_severity_logs: None,
    }
}

fn new_run_params() -> run_test_suite_lib::RunParams {
    run_test_suite_lib::RunParams {
        timeout_behavior: run_test_suite_lib::TimeoutBehavior::TerminateRemaining,
        stop_after_failures: None,
    }
}

/// run specified test once.
async fn run_test_once<W: Write + Send>(
    test_params: TestParams,
    min_log_severity: Option<Severity>,
    writer: &mut W,
) -> Result<SuiteRunResult, RunTestSuiteError> {
    let test_result = {
        let mut reporter = output::RunReporter::new(output::NoopReporter);
        let streams = run_test_suite_lib::run_test(
            fuchsia_component::client::connect_to_protocol::<RunBuilderMarker>()
                .expect("connecting to RunBuilderProxy"),
            vec![test_params],
            new_run_params(),
            min_log_severity,
            writer,
            &mut reporter,
            futures::future::pending(),
        )
        .await?;
        let mut results = streams.collect::<Vec<_>>().await;
        assert_eq!(results.len(), 1, "{:?}", results);
        results.pop().unwrap()
    };
    test_result
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_no_clean_exit() {
    let mut output: Vec<u8> = vec![];
    let run_result = run_test_once(
        new_test_params(
            "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/no-onfinished-after-test-example.cm",
            ),
        None,
        &mut output
    )
    .await
    .expect("Running test should not fail");

    let expected_output = "[RUNNING]	Example.Test1
log1 for Example.Test1
log2 for Example.Test1
log3 for Example.Test1
[PASSED]	Example.Test1
[RUNNING]	Example.Test2
log1 for Example.Test2
log2 for Example.Test2
log3 for Example.Test2
[PASSED]	Example.Test2
[RUNNING]	Example.Test3
log1 for Example.Test3
log2 for Example.Test3
log3 for Example.Test3
[PASSED]	Example.Test3
";
    assert_output!(output, expected_output);

    assert_eq!(run_result.outcome, Outcome::Inconclusive);
    assert_eq!(run_result.executed, run_result.passed);

    let expected = vec!["Example.Test1", "Example.Test2", "Example.Test3"];

    assert_eq!(run_result.executed, expected);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_passing_v2_test() {
    let output_dir = tempfile::tempdir().expect("create temp directory");
    let mut output: Vec<u8> = vec![];
    let mut reporter = output::RunReporter::new(
        output::DirectoryReporter::new(output_dir.path().to_path_buf()).unwrap(),
    );
    let streams = run_test_suite_lib::run_test(
        fuchsia_component::client::connect_to_protocol::<RunBuilderMarker>()
                    .expect("connecting to RunBuilderProxy"),
        vec![new_test_params(
            "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/passing-test-example.cm",
        )],
        new_run_params(),
        None,
        &mut output,
        &mut reporter,
        futures::future::pending(),
    )
    .await
    .expect("run test");
    let run_result = streams.collect::<Vec<_>>().await.pop().unwrap().unwrap();

    reporter.stopped(&output::ReportedOutcome::Passed, output::Timestamp::Unknown).unwrap();
    reporter.finished().unwrap();

    let expected_output = "[RUNNING]	Example.Test1
log1 for Example.Test1
log2 for Example.Test1
log3 for Example.Test1
[PASSED]	Example.Test1
[RUNNING]	Example.Test2
log1 for Example.Test2
log2 for Example.Test2
log3 for Example.Test2
[PASSED]	Example.Test2
[RUNNING]	Example.Test3
log1 for Example.Test3
log2 for Example.Test3
log3 for Example.Test3
[PASSED]	Example.Test3
";
    assert_output!(output, expected_output);

    assert_eq!(run_result.outcome, Outcome::Passed);
    assert_eq!(run_result.executed, run_result.passed);

    let expected = vec!["Example.Test1", "Example.Test2", "Example.Test3"];

    assert_eq!(run_result.executed, expected);

    let expected_test_suites = vec![ExpectedSuite::new(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/passing-test-example.cm",
        directory::Outcome::Passed,
    )
    .with_artifact(directory::ArtifactType::Syslog, "syslog.txt".into(), "")
    .with_case(
        ExpectedTestCase::new("Example.Test1", directory::Outcome::Passed)
            .with_artifact(
                directory::ArtifactType::Stdout,
                "stdout.txt".into(),
                "log1 for Example.Test1\nlog2 for Example.Test1\nlog3 for Example.Test1\n",
            )
            .with_artifact(directory::ArtifactType::Stderr, "stderr.txt".into(), ""),
    )
    .with_case(
        ExpectedTestCase::new("Example.Test2", directory::Outcome::Passed)
            .with_artifact(
                directory::ArtifactType::Stdout,
                "stdout.txt".into(),
                "log1 for Example.Test2\nlog2 for Example.Test2\nlog3 for Example.Test2\n",
            )
            .with_artifact(directory::ArtifactType::Stderr, "stderr.txt".into(), ""),
    )
    .with_case(
        ExpectedTestCase::new("Example.Test3", directory::Outcome::Passed)
            .with_artifact(
                directory::ArtifactType::Stdout,
                "stdout.txt".into(),
                "log1 for Example.Test3\nlog2 for Example.Test3\nlog3 for Example.Test3\n",
            )
            .with_artifact(directory::ArtifactType::Stderr, "stderr.txt".into(), ""),
    )];

    let (_run_result, suite_results) = directory::testing::parse_json_in_output(output_dir.path());

    directory::testing::assert_suite_results(
        output_dir.path(),
        &suite_results,
        &expected_test_suites,
    );
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_stderr_test() {
    let output_dir = tempfile::tempdir().expect("create temp directory");
    let mut output: Vec<u8> = vec![];
    let mut reporter = output::RunReporter::new(
        output::DirectoryReporter::new(output_dir.path().to_path_buf()).unwrap(),
    );
    let streams = run_test_suite_lib::run_test(
        fuchsia_component::client::connect_to_protocol::<RunBuilderMarker>()
            .expect("connecting to RunBuilderProxy"),
        vec![new_test_params(
            "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/test-with-stderr.cm",
        )],
        new_run_params(),
        None,
        &mut output,
        &mut reporter,
        futures::future::pending(),
    )
    .await
    .expect("run test");
    let run_result = streams.collect::<Vec<_>>().await.pop().unwrap().unwrap();

    reporter.stopped(&output::ReportedOutcome::Passed, output::Timestamp::Unknown).unwrap();
    reporter.finished().unwrap();

    let expected_output = "[RUNNING]	Example.Test1
log1 for Example.Test1
log2 for Example.Test1
log3 for Example.Test1
stderr msg1 for Example.Test1
stderr msg2 for Example.Test1
[PASSED]	Example.Test1
[RUNNING]	Example.Test2
log1 for Example.Test2
log2 for Example.Test2
log3 for Example.Test2
[PASSED]	Example.Test2
[RUNNING]	Example.Test3
log1 for Example.Test3
log2 for Example.Test3
log3 for Example.Test3
stderr msg1 for Example.Test3
stderr msg2 for Example.Test3
[PASSED]	Example.Test3
";
    assert_output!(output, expected_output);

    assert_eq!(run_result.outcome, Outcome::Passed);
    assert_eq!(run_result.executed, run_result.passed);

    let expected_test_suites = vec![ExpectedSuite::new(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/test-with-stderr.cm",
        directory::Outcome::Passed,
    )
    .with_artifact(directory::ArtifactType::Syslog, "syslog.txt".into(), "")
    .with_case(
        ExpectedTestCase::new("Example.Test1", directory::Outcome::Passed)
            .with_artifact(
                directory::ArtifactType::Stdout,
                "stdout.txt".into(),
                "log1 for Example.Test1\nlog2 for Example.Test1\nlog3 for Example.Test1\n",
            )
            .with_artifact(
                directory::ArtifactType::Stderr,
                "stderr.txt".into(),
                "stderr msg1 for Example.Test1\nstderr msg2 for Example.Test1\n",
            ),
    )
    .with_case(
        ExpectedTestCase::new("Example.Test2", directory::Outcome::Passed)
            .with_artifact(
                directory::ArtifactType::Stdout,
                "stdout.txt".into(),
                "log1 for Example.Test2\nlog2 for Example.Test2\nlog3 for Example.Test2\n",
            )
            .with_artifact(directory::ArtifactType::Stderr, "stderr.txt".into(), ""),
    )
    .with_case(
        ExpectedTestCase::new("Example.Test3", directory::Outcome::Passed)
            .with_artifact(
                directory::ArtifactType::Stdout,
                "stdout.txt".into(),
                "log1 for Example.Test3\nlog2 for Example.Test3\nlog3 for Example.Test3\n",
            )
            .with_artifact(
                directory::ArtifactType::Stderr,
                "stderr.txt".into(),
                "stderr msg1 for Example.Test3\nstderr msg2 for Example.Test3\n",
            ),
    )];

    let (_run_result, suite_results) = directory::testing::parse_json_in_output(output_dir.path());

    directory::testing::assert_suite_results(
        output_dir.path(),
        &suite_results,
        &expected_test_suites,
    );
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_passing_v2_test_multiple_times() {
    let mut output: Vec<u8> = vec![];
    let output_dir = tempfile::tempdir().expect("Create temp directory");
    let mut reporter = output::RunReporter::new(
        output::DirectoryReporter::new(output_dir.path().to_path_buf()).expect("Create reporter"),
    );
    let streams = run_test_suite_lib::run_test(
        fuchsia_component::client::connect_to_protocol::<RunBuilderMarker>()
                    .expect("connecting to RunBuilderProxy"),
            vec![new_test_params(
                "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/passing-test-example.cm",
                ); 10],
                new_run_params(),
            None,&mut output,
            &mut reporter,
            futures::future::pending(),
        )
    .await.expect("run test");
    let run_results = streams.collect::<Vec<_>>().await;
    reporter.stopped(&output::ReportedOutcome::Passed, output::Timestamp::Unknown).unwrap();
    reporter.finished().unwrap();

    assert_eq!(run_results.len(), 10);
    for run_result in run_results {
        let run_result = run_result.expect("Running test should not fail");
        assert_eq!(run_result.outcome, Outcome::Passed);
        assert_eq!(run_result.executed, run_result.passed);

        let expected = vec!["Example.Test1", "Example.Test2", "Example.Test3"];

        assert_eq!(run_result.executed, expected);
    }

    let expected_test_run = ExpectedTestRun::new(directory::Outcome::Passed);
    let expected_test_suite = ExpectedSuite::new(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/passing-test-example.cm",
        directory::Outcome::Passed,
    )
    .with_case(
        ExpectedTestCase::new("Example.Test1", directory::Outcome::Passed)
            .with_matching_artifact(directory::ArtifactType::Stdout, "stdout.txt".into(), |_| ())
            .with_artifact(directory::ArtifactType::Stderr, "stderr.txt".into(), ""),
    )
    .with_case(
        ExpectedTestCase::new("Example.Test2", directory::Outcome::Passed)
            .with_matching_artifact(directory::ArtifactType::Stdout, "stdout.txt".into(), |_| ())
            .with_artifact(directory::ArtifactType::Stderr, "stderr.txt".into(), ""),
    )
    .with_case(
        ExpectedTestCase::new("Example.Test3", directory::Outcome::Passed)
            .with_matching_artifact(directory::ArtifactType::Stdout, "stdout.txt".into(), |_| ())
            .with_artifact(directory::ArtifactType::Stderr, "stderr.txt".into(), ""),
    )
    .with_matching_artifact(directory::ArtifactType::Syslog, "syslog.txt".into(), |_| ());

    let (run_result, suite_results) = directory::testing::parse_json_in_output(output_dir.path());

    directory::testing::assert_run_result(output_dir.path(), &run_result, &expected_test_run);

    assert_eq!(suite_results.len(), 10);
    for suite_result in suite_results {
        directory::testing::assert_suite_result(
            output_dir.path(),
            &suite_result,
            &expected_test_suite,
        );
    }
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_multiple_passing_tests() {
    let mut output: Vec<u8> = vec![];
    let output_dir = tempfile::tempdir().expect("Create temp directory");
    let mut reporter = output::RunReporter::new(
        output::DirectoryReporter::new(output_dir.path().to_path_buf()).expect("Create reporter"),
    );
    let streams = run_test_suite_lib::run_test(
        fuchsia_component::client::connect_to_protocol::<RunBuilderMarker>()
                    .expect("connecting to RunBuilderProxy"),
            vec![new_test_params(
                    "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/passing-test-example.cm",
                ),
                new_test_params(
                    "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/test-with-stderr.cm",
                )
            ],
            new_run_params(),
            None, &mut output,
            &mut reporter,
            futures::future::pending(),
        )
    .await.expect("run test");
    let run_results = streams.collect::<Vec<_>>().await;
    reporter.stopped(&output::ReportedOutcome::Passed, output::Timestamp::Unknown).unwrap();
    reporter.finished().unwrap();

    assert_eq!(run_results.len(), 2);
    for run_result in run_results {
        let run_result = run_result.expect("Running test should not fail");
        assert_eq!(run_result.outcome, Outcome::Passed);
        assert_eq!(run_result.executed, run_result.passed);

        let expected = vec!["Example.Test1", "Example.Test2", "Example.Test3"];

        assert_eq!(run_result.executed, expected);
    }

    let expected_test_run = ExpectedTestRun::new(directory::Outcome::Passed);
    let expected_test_suites = vec![
        ExpectedSuite::new(
            "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/passing-test-example.cm",
            directory::Outcome::Passed,
        )
        .with_case(
            ExpectedTestCase::new("Example.Test1", directory::Outcome::Passed)
                .with_matching_artifact(directory::ArtifactType::Stdout, "stdout.txt".into(), |_| ())
                .with_artifact(directory::ArtifactType::Stderr, "stderr.txt".into(), ""),
        )
        .with_case(
            ExpectedTestCase::new("Example.Test2", directory::Outcome::Passed)
                .with_matching_artifact(directory::ArtifactType::Stdout, "stdout.txt".into(), |_| ())
                .with_artifact(directory::ArtifactType::Stderr, "stderr.txt".into(), ""),
        )
        .with_case(
            ExpectedTestCase::new("Example.Test3", directory::Outcome::Passed)
                .with_matching_artifact(directory::ArtifactType::Stdout, "stdout.txt".into(), |_| ())
                .with_artifact(directory::ArtifactType::Stderr, "stderr.txt".into(), ""),
        )
        .with_matching_artifact(directory::ArtifactType::Syslog, "syslog.txt".into(), |_| ()),
        ExpectedSuite::new(
            "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/test-with-stderr.cm",
            directory::Outcome::Passed,
        )
        .with_case(
            ExpectedTestCase::new("Example.Test1", directory::Outcome::Passed)
                .with_matching_artifact(directory::ArtifactType::Stdout, "stdout.txt".into(), |_| ())
                .with_matching_artifact(directory::ArtifactType::Stderr, "stderr.txt".into(), |_| ()),
        )
        .with_case(
            ExpectedTestCase::new("Example.Test2", directory::Outcome::Passed)
                .with_matching_artifact(directory::ArtifactType::Stdout, "stdout.txt".into(), |_| ())
                .with_matching_artifact(directory::ArtifactType::Stderr, "stderr.txt".into(), |_| ()),
        )
        .with_case(
            ExpectedTestCase::new("Example.Test3", directory::Outcome::Passed)
                .with_matching_artifact(directory::ArtifactType::Stdout, "stdout.txt".into(), |_| ())
                .with_matching_artifact(directory::ArtifactType::Stderr, "stderr.txt".into(), |_| ()),
        )
        .with_matching_artifact(directory::ArtifactType::Syslog, "syslog.txt".into(), |_| ()),
    ];

    let (run_result, suite_results) = directory::testing::parse_json_in_output(output_dir.path());

    directory::testing::assert_run_result(output_dir.path(), &run_result, &expected_test_run);
    directory::testing::assert_suite_results(
        output_dir.path(),
        &suite_results,
        &expected_test_suites,
    );
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_with_filter() {
    let mut output: Vec<u8> = vec![];
    let mut test_params = new_test_params(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/passing-test-example.cm",
    );

    test_params.test_filters = Some(vec!["*Test3".to_string()]);
    let run_result =
        run_test_once(test_params, None, &mut output).await.expect("Running test should not fail");

    let expected_output = "[RUNNING]	Example.Test3
log1 for Example.Test3
log2 for Example.Test3
log3 for Example.Test3
[PASSED]	Example.Test3
";
    assert_output!(output, expected_output);

    assert_eq!(run_result.outcome, Outcome::Passed);
    assert_eq!(run_result.executed, run_result.passed);

    let expected = vec!["Example.Test3"];

    assert_eq!(run_result.executed, expected);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_with_multiple_filter() {
    let mut output: Vec<u8> = vec![];
    let mut test_params = new_test_params(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/passing-test-example.cm",
    );

    test_params.test_filters = Some(vec!["*Test3".to_string(), "*Test1".to_string()]);
    let run_result =
        run_test_once(test_params, None, &mut output).await.expect("Running test should not fail");

    let expected_output = "[RUNNING]	Example.Test1
log1 for Example.Test1
log2 for Example.Test1
log3 for Example.Test1
[PASSED]	Example.Test1
[RUNNING]	Example.Test3
log1 for Example.Test3
log2 for Example.Test3
log3 for Example.Test3
[PASSED]	Example.Test3
";
    assert_output!(output, expected_output);

    assert_eq!(run_result.outcome, Outcome::Passed);
    assert_eq!(run_result.executed, run_result.passed);

    let expected = vec!["Example.Test1", "Example.Test3"];

    assert_eq!(run_result.executed, expected);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_with_filter_no_matching_cases() {
    let mut output: Vec<u8> = vec![];
    let mut test_params = new_test_params(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/passing-test-example.cm",
    );

    test_params.test_filters = Some(vec!["matches-nothing".to_string()]);
    let run_result = run_test_once(test_params, None, &mut output).await.unwrap_err();

    assert_matches!(run_result, RunTestSuiteError::Launch(LaunchError::NoMatchingCases));
    assert!(!run_result.is_internal_error());
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_empty_test() {
    let mut output: Vec<u8> = vec![];
    let run_result = run_test_once(
        new_test_params(
            "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/no-test-example.cm",
        ),
        None,
        &mut output,
    )
    .await
    .expect("Running test should not fail");

    assert_eq!(run_result.executed.len(), 0);
    assert_eq!(run_result.passed.len(), 0);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_huge_test() {
    let mut output: Vec<u8> = vec![];

    let run_result = run_test_once(
        new_test_params(
            "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/huge-test-example.cm",
        ),
        None,
        &mut output,
    )
    .await
    .expect("Running test should not fail");

    assert_eq!(run_result.executed.len(), 1_000);
    assert_eq!(run_result.passed.len(), 1_000);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_disabled_test_exclude_disabled() {
    let mut output: Vec<u8> = vec![];
    let run_result = run_test_once(
            new_test_params(
                "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/disabled-test-example.cm",
            ),
            None,
            &mut output,
        )
    .await
    .expect("Running test should not fail");

    let expected_output = "[RUNNING]	Example.Test1
log1 for Example.Test1
log2 for Example.Test1
log3 for Example.Test1
[PASSED]	Example.Test1
[RUNNING]	Example.Test2
[SKIPPED]	Example.Test2
[RUNNING]	Example.Test3
[SKIPPED]	Example.Test3
";
    assert_output!(output, expected_output);

    assert_eq!(run_result.outcome, Outcome::Passed);

    // "skipped" is a form of "executed"
    let expected_executed = vec!["Example.Test1", "Example.Test2", "Example.Test3"];
    let expected_passed = vec!["Example.Test1"];

    assert_eq!(run_result.executed, expected_executed);
    assert_eq!(run_result.passed, expected_passed);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_disabled_test_include_disabled() {
    let mut output: Vec<u8> = vec![];
    let mut test_params = new_test_params(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/disabled-test-example.cm",
    );
    test_params.also_run_disabled_tests = true;
    let run_result =
        run_test_once(test_params, None, &mut output).await.expect("Running test should not fail");

    let expected_output = "[RUNNING]	Example.Test1
log1 for Example.Test1
log2 for Example.Test1
log3 for Example.Test1
[PASSED]	Example.Test1
[RUNNING]	Example.Test2
log1 for Example.Test2
log2 for Example.Test2
log3 for Example.Test2
[PASSED]	Example.Test2
[RUNNING]	Example.Test3
log1 for Example.Test3
log2 for Example.Test3
log3 for Example.Test3
[FAILED]	Example.Test3
";
    assert_output!(output, expected_output);

    assert_eq!(run_result.outcome, Outcome::Failed);

    // "skipped" is a form of "executed"
    let expected_executed = vec!["Example.Test1", "Example.Test2", "Example.Test3"];
    let expected_passed = vec!["Example.Test1", "Example.Test2"];

    assert_eq!(run_result.executed, expected_executed);
    assert_eq!(run_result.passed, expected_passed);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_failing_test() {
    let mut output: Vec<u8> = vec![];
    let run_result = run_test_once(
            new_test_params(
                "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/failing-test-example.cm",
            ),
            None,
            &mut output,
        )
    .await
    .expect("Running test should not fail");

    let expected_output = "[RUNNING]	Example.Test1
log1 for Example.Test1
log2 for Example.Test1
log3 for Example.Test1
[PASSED]	Example.Test1
[RUNNING]	Example.Test2
log1 for Example.Test2
log2 for Example.Test2
log3 for Example.Test2
[FAILED]	Example.Test2
[RUNNING]	Example.Test3
log1 for Example.Test3
log2 for Example.Test3
log3 for Example.Test3
[PASSED]	Example.Test3
";

    assert_output!(output, expected_output);

    assert_eq!(run_result.outcome, Outcome::Failed);

    assert_eq!(run_result.executed, vec!["Example.Test1", "Example.Test2", "Example.Test3"]);
    assert_eq!(run_result.passed, vec!["Example.Test1", "Example.Test3"]);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_failing_v2_test_multiple_times() {
    let mut output: Vec<u8> = vec![];
    let mut reporter = output::RunReporter::new(output::NoopReporter);
    let streams = run_test_suite_lib::run_test(
        fuchsia_component::client::connect_to_protocol::<RunBuilderMarker>()
                    .expect("connecting to RunBuilderProxy"),
        vec![new_test_params(
                "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/failing-test-example.cm",
                ); 10],
                new_run_params(),
                None,&mut output, &mut reporter, futures::future::pending(),
        )
    .await.expect("run test");
    let run_results = streams.collect::<Vec<_>>().await;

    assert_eq!(run_results.len(), 10);
    for run_result in run_results {
        let run_result = run_result.expect("Running test should not fail");
        assert_eq!(run_result.outcome, Outcome::Failed);
        assert_eq!(run_result.executed, vec!["Example.Test1", "Example.Test2", "Example.Test3"]);
        assert_eq!(run_result.passed, vec!["Example.Test1", "Example.Test3"]);
    }
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_incomplete_test() {
    let mut output: Vec<u8> = vec![];
    let run_result = run_test_once(
            new_test_params(
                "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/incomplete-test-example.cm",
            ),
            None,
            &mut output,
        )
    .await
    .expect("Running test should not fail");

    let expected_output = "[RUNNING]	Example.Test1
[RUNNING]	Example.Test2
log1 for Example.Test1
log2 for Example.Test1
log3 for Example.Test1
log1 for Example.Test2
log2 for Example.Test2
log3 for Example.Test2
[PASSED]	Example.Test2
[RUNNING]	Example.Test3
log1 for Example.Test3
log2 for Example.Test3
log3 for Example.Test3

The following test(s) never completed:
Example.Test1
Example.Test3
";

    assert_output!(output, expected_output);

    assert_eq!(run_result.outcome, Outcome::DidNotFinish);

    assert_eq!(run_result.executed, vec!["Example.Test1", "Example.Test2", "Example.Test3"]);
    assert_eq!(run_result.passed, vec!["Example.Test2"]);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_invalid_test() {
    let mut output: Vec<u8> = vec![];
    let run_result = run_test_once(
            new_test_params(
                "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/invalid-test-example.cm",

            ),
            None,
            &mut output,
        )
    .await
    .expect("Running test should not fail");

    let expected_output = "[RUNNING]	Example.Test1
log1 for Example.Test1
log2 for Example.Test1
log3 for Example.Test1
[RUNNING]	Example.Test2
log1 for Example.Test2
log2 for Example.Test2
log3 for Example.Test2
[PASSED]	Example.Test2
[RUNNING]	Example.Test3
log1 for Example.Test3
log2 for Example.Test3
log3 for Example.Test3

The following test(s) never completed:
Example.Test1
Example.Test3
";
    assert_output!(output, expected_output);

    assert_eq!(run_result.outcome, Outcome::DidNotFinish);

    assert_eq!(run_result.executed, vec!["Example.Test1", "Example.Test2", "Example.Test3"]);
    assert_eq!(run_result.passed, vec!["Example.Test2"]);
}

// This test also acts an example on how to right a v2 test.
// This will launch a echo_realm which will inject echo_server, launch v2 test which will
// then test that server out and return back results.
#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_run_echo_test() {
    let mut output: Vec<u8> = vec![];
    let run_result = run_test_once(
        new_test_params(
            "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/echo_test_realm.cm",
        ),
        None,
        &mut output,
    )
    .await
    .expect("Running test should not fail");

    let expected_output = "[RUNNING]	EchoTest
[PASSED]	EchoTest
";
    assert_output!(output, expected_output);

    assert_eq!(run_result.outcome, Outcome::Passed);

    assert_eq!(run_result.executed, vec!["EchoTest"]);
    assert_eq!(run_result.passed, vec!["EchoTest"]);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_timeout() {
    let mut output: Vec<u8> = vec![];
    let mut test_params = new_test_params(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/long_running_test.cm",
    );
    test_params.timeout = std::num::NonZeroU32::new(1);
    let run_result =
        run_test_once(test_params, None, &mut output).await.expect("Running test should not fail");
    let expected_output = "[RUNNING]	LongRunningTest.LongRunning
[TIMED_OUT]	LongRunningTest.LongRunning
";
    assert_output!(output, expected_output);
    assert_eq!(run_result.outcome, Outcome::Timedout);

    assert_eq!(run_result.passed, Vec::<String>::new());
}

#[fuchsia_async::run_singlethreaded(test)]
// when a test times out, we should not run it again.
async fn test_timeout_multiple_times() {
    let mut output: Vec<u8> = vec![];
    let mut reporter = output::RunReporter::new(output::NoopReporter);
    let mut test_params = new_test_params(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/long_running_test.cm",
    );
    test_params.timeout = std::num::NonZeroU32::new(1);
    let streams = run_test_suite_lib::run_test(
        fuchsia_component::client::connect_to_protocol::<RunBuilderMarker>()
            .expect("connecting to RunBuilderProxy"),
        vec![test_params; 10],
        new_run_params(),
        None,
        &mut output,
        &mut reporter,
        futures::future::pending(),
    )
    .await
    .expect("run test");
    let mut run_results = streams.collect::<Vec<_>>().await;
    assert_eq!(run_results.len(), 1);
    let run_result = run_results.pop().unwrap().unwrap();
    assert_eq!(run_result.outcome, Outcome::Timedout);

    let expected_output = "[RUNNING]	LongRunningTest.LongRunning
[TIMED_OUT]	LongRunningTest.LongRunning
";
    assert_output!(output, expected_output);

    assert_eq!(run_result.passed, Vec::<String>::new());
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_coninue_on_timeout() {
    let mut output = std::io::sink();
    let mut reporter = output::RunReporter::new(output::NoopReporter);
    let mut long_test_params = new_test_params(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/long_running_test.cm",
    );
    long_test_params.timeout = std::num::NonZeroU32::new(1);

    let short_test_params = new_test_params(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/passing-test-example.cm",
    );

    let mut test_params = vec![long_test_params];
    for _ in 0..10 {
        test_params.push(short_test_params.clone());
    }

    let streams = run_test_suite_lib::run_test(
        fuchsia_component::client::connect_to_protocol::<RunBuilderMarker>()
            .expect("connecting to RunBuilderProxy"),
        test_params,
        run_test_suite_lib::RunParams {
            timeout_behavior: run_test_suite_lib::TimeoutBehavior::Continue,
            stop_after_failures: None,
        },
        None,
        &mut output,
        &mut reporter,
        futures::future::pending(),
    )
    .await
    .expect("run test");
    let run_results = streams.collect::<Vec<_>>().await;
    assert_eq!(run_results.len(), 11);

    let (timed_out, others): (Vec<_>, Vec<_>) = run_results
        .into_iter()
        .map(|result| result.unwrap())
        .partition(|result| result.outcome == Outcome::Timedout);

    // Note - this is a somewhat weak assertion as we generally do not know the order in which the
    // tests are executed.
    assert_eq!(timed_out.len(), 1);
    assert_eq!(others.len(), 10);
    assert!(others.iter().all(|result| result.outcome == Outcome::Passed));
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_stop_after_n_failures() {
    let mut output = std::io::sink();
    let mut reporter = output::RunReporter::new(output::NoopReporter);
    let streams = run_test_suite_lib::run_test(
        fuchsia_component::client::connect_to_protocol::<RunBuilderMarker>()
                    .expect("connecting to RunBuilderProxy"),
        vec![new_test_params(
                "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/failing-test-example.cm",
                ); 10],
                run_test_suite_lib::RunParams {
                    timeout_behavior: run_test_suite_lib::TimeoutBehavior::Continue,
                    stop_after_failures: Some(5u16.try_into().unwrap()),
                },
                None, &mut output, &mut reporter, futures::future::pending(),
        )
    .await.expect("run test");
    let run_results = streams.collect::<Vec<_>>().await;

    assert_eq!(run_results.len(), 5);
    for run_result in run_results {
        let run_result = run_result.expect("Running test should not fail");
        assert_eq!(run_result.outcome, Outcome::Failed);
        assert_eq!(run_result.executed, vec!["Example.Test1", "Example.Test2", "Example.Test3"]);
        assert_eq!(run_result.passed, vec!["Example.Test1", "Example.Test3"]);
        assert_eq!(run_result.failed, vec!["Example.Test2"]);
    }
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_passes_with_large_timeout() {
    let mut output: Vec<u8> = vec![];
    let mut test_params = new_test_params(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/echo_test_realm.cm",
    );
    test_params.timeout = std::num::NonZeroU32::new(600);
    let run_result =
        run_test_once(test_params, None, &mut output).await.expect("Running test should not fail");

    let expected_output = "[RUNNING]	EchoTest
[PASSED]	EchoTest
";
    assert_output!(output, expected_output);

    assert_eq!(run_result.outcome, Outcome::Passed);

    assert_eq!(run_result.executed, vec!["EchoTest"]);
    assert_eq!(run_result.passed, vec!["EchoTest"]);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_logging_component() {
    let mut output: Vec<u8> = vec![];
    let mut test_params = new_test_params(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/logging_test.cm",
    );
    test_params.timeout = std::num::NonZeroU32::new(600);
    let run_result =
        run_test_once(test_params, None, &mut output).await.expect("Running test should not fail");

    let expected_output = "[RUNNING]	log_and_exit
[TIMESTAMP][PID][TID][<root>][log_and_exit,logging_test] DEBUG: Logging initialized\n\
[TIMESTAMP][PID][TID][<root>][log_and_exit,logging_test] DEBUG: my debug message\n\
[TIMESTAMP][PID][TID][<root>][log_and_exit,logging_test] INFO: my info message\n\
[TIMESTAMP][PID][TID][<root>][log_and_exit,logging_test] WARN: my warn message\n\
[PASSED]	log_and_exit
";
    assert_output!(output, expected_output);
    assert_eq!(run_result.outcome, Outcome::Passed);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_logging_component_min_severity() {
    let mut output: Vec<u8> = vec![];
    let mut test_params = new_test_params(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/logging_test.cm",
    );
    test_params.timeout = std::num::NonZeroU32::new(600);
    let run_result = run_test_once(test_params, Some(Severity::Info), &mut output)
        .await
        .expect("Running test should not fail");

    let expected_output = "[RUNNING]	log_and_exit
[TIMESTAMP][PID][TID][<root>][log_and_exit,logging_test] INFO: my info message\n\
[TIMESTAMP][PID][TID][<root>][log_and_exit,logging_test] WARN: my warn message\n\
[PASSED]	log_and_exit
";
    assert_output!(output, expected_output);
    assert_eq!(run_result.outcome, Outcome::Passed);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_stdout_and_log_ansi() {
    let mut output: Vec<u8> = vec![];
    let mut test_params = new_test_params(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/stdout_ansi_test.cm",
    );
    test_params.timeout = std::num::NonZeroU32::new(600);
    let run_result = run_test_once(test_params, Some(Severity::Info), &mut output)
        .await
        .expect("Running test should not fail");

    let expected_output = "[RUNNING]	log_ansi_test
[TIMESTAMP][PID][TID][<root>][log_ansi_test] INFO: \u{1b}[31mred log\u{1b}[0m
[PASSED]	log_ansi_test
[RUNNING]	stdout_ansi_test
\u{1b}[31mred stdout\u{1b}[0m
[PASSED]	stdout_ansi_test
";
    assert_output!(output, expected_output);
    assert_eq!(run_result.outcome, Outcome::Passed);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_stdout_and_log_filter_ansi() {
    let mut output: Vec<u8> = vec![];
    let mut ansi_filter = output::AnsiFilterWriter::new(&mut output);
    let mut reporter = output::RunReporter::new(output::NoopReporter);
    let mut test_params = new_test_params(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/stdout_ansi_test.cm",
    );
    test_params.timeout = std::num::NonZeroU32::new(600);

    let test_result = {
        let streams = run_test_suite_lib::run_test(
            fuchsia_component::client::connect_to_protocol::<RunBuilderMarker>()
                .expect("connecting to RunBuilderProxy"),
            vec![test_params],
            new_run_params(),
            Some(Severity::Info),
            &mut ansi_filter,
            &mut reporter,
            futures::future::pending(),
        )
        .await
        .unwrap();
        let mut results = streams.collect::<Vec<_>>().await;
        assert_eq!(results.len(), 1, "{:?}", results);
        results.pop().unwrap().unwrap()
    };
    drop(ansi_filter);

    let expected_output = "[RUNNING]	log_ansi_test
[TIMESTAMP][PID][TID][<root>][log_ansi_test] INFO: red log
[PASSED]	log_ansi_test
[RUNNING]	stdout_ansi_test
red stdout
[PASSED]	stdout_ansi_test
";
    assert_output!(output, expected_output);
    assert_eq!(test_result.outcome, Outcome::Passed);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_logging_component_max_severity_info() {
    test_max_severity(Severity::Info).await;
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_logging_component_max_severity_warn() {
    test_max_severity(Severity::Warn).await;
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_logging_component_max_severity_error() {
    test_max_severity(Severity::Error).await;
}

async fn test_max_severity(max_severity: Severity) {
    let mut output: Vec<u8> = vec![];
    let mut test_params = new_test_params(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/error_logging_test.cm",
    );
    test_params.timeout = std::num::NonZeroU32::new(600);
    test_params.max_severity_logs = Some(max_severity);
    let run_result =
        run_test_once(test_params, None, &mut output).await.expect("Running test should not fail");

    let expected_output = "[RUNNING]	log_and_exit
[TIMESTAMP][PID][TID][<root>][log_and_exit,error_logging_test] DEBUG: Logging initialized\n\
[TIMESTAMP][PID][TID][<root>][log_and_exit,error_logging_test] INFO: my info message\n\
[TIMESTAMP][PID][TID][<root>][log_and_exit,error_logging_test] WARN: my warn message\n\
[TIMESTAMP][PID][TID][<root>][log_and_exit,error_logging_test] ERROR: [../../src/sys/run_test_suite/tests/error_logging_test.rs(12)] my error message\n\
[PASSED]	log_and_exit
";

    assert_output!(output, expected_output);
    assert_eq!(run_result.outcome, Outcome::Passed);

    match max_severity {
        Severity::Info => {
            assert!(run_result.restricted_logs.len() > 0, "expected logs to fail the test");
            let logs = run_result
                .restricted_logs
                .into_iter()
                .filter(|log| !is_debug_data_warning(log))
                .map(sanitize_log_for_comparison)
                .collect::<Vec<_>>();
            assert_eq!(
                logs,
                vec![
                    "[TIMESTAMP][PID][TID][<root>][log_and_exit,error_logging_test] WARN: my warn message".to_owned(),
                    "[TIMESTAMP][PID][TID][<root>][log_and_exit,error_logging_test] ERROR: [../../src/sys/run_test_suite/tests/error_logging_test.rs(12)] my error message".to_owned(),
                ]
            );
        }
        Severity::Warn => {
            assert!(run_result.restricted_logs.len() > 0, "expected logs to fail the test");
            assert_eq!(
                run_result.restricted_logs.into_iter().map(sanitize_log_for_comparison).collect::<Vec<_>>(),
                vec![
                    "[TIMESTAMP][PID][TID][<root>][log_and_exit,error_logging_test] ERROR: [../../src/sys/run_test_suite/tests/error_logging_test.rs(12)] my error message".to_owned(),
                ]
            );
        }
        Severity::Error => {
            assert_eq!(run_result.restricted_logs.len(), 0);
        }
        _ => unreachable!("Not used"),
    }
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_does_not_resolve() {
    let mut output: Vec<u8> = vec![];
    let test_params = new_test_params(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/nonexistant_test.cm",
    );
    let log_opts = None;
    let run_err = run_test_once(test_params, log_opts, &mut output).await.unwrap_err();
    assert_matches!(run_err, RunTestSuiteError::Launch(LaunchError::InstanceCannotResolve));
    assert!(!run_err.is_internal_error());
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_stdout_to_directory() {
    let output_dir = tempfile::tempdir().expect("create temp directory");
    let mut test_params = new_test_params(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/stdout_ansi_test.cm",
    );
    test_params.timeout = std::num::NonZeroU32::new(600);

    let outcome = run_test_suite_lib::run_tests_and_get_outcome(
        fuchsia_component::client::connect_to_protocol::<RunBuilderMarker>()
            .expect("connecting to RunBuilderProxy"),
        vec![test_params],
        new_run_params(),
        None,
        false,
        Some(output_dir.path().to_path_buf()),
        futures::future::pending(),
    )
    .await;

    assert_eq!(outcome, Outcome::Passed);

    let expected_test_run = ExpectedTestRun::new(directory::Outcome::Passed)
        .with_no_start_time()
        .with_no_run_duration();
    let expected_test_suites = vec![ExpectedSuite::new(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/stdout_ansi_test.cm",
        directory::Outcome::Passed,
    )
    .with_matching_artifact(directory::ArtifactType::Syslog, "syslog.txt".into(), |logs| {
        assert_output!(
            logs.as_bytes(),
            "\n[TIMESTAMP][PID][TID][<root>][log_ansi_test] INFO: \u{1b}[31mred log\u{1b}[0m"
        );
    })
    .with_case(
        ExpectedTestCase::new("stdout_ansi_test", directory::Outcome::Passed)
            .with_artifact(
                directory::ArtifactType::Stdout,
                "stdout.txt".into(),
                "\u{1b}[31mred stdout\u{1b}[0m\n",
            )
            .with_artifact(directory::ArtifactType::Stderr, "stderr.txt".into(), "")
            .with_no_run_duration()
            .with_any_start_time(),
    )
    .with_case(
        ExpectedTestCase::new("log_ansi_test", directory::Outcome::Passed)
            .with_artifact(directory::ArtifactType::Stdout, "stdout.txt".into(), "")
            .with_artifact(directory::ArtifactType::Stderr, "stderr.txt".into(), "")
            .with_no_run_duration()
            .with_any_start_time(),
    )
    .with_any_run_duration()
    .with_any_start_time()];

    let (run_result, suite_results) = directory::testing::parse_json_in_output(output_dir.path());

    directory::testing::assert_run_result(output_dir.path(), &run_result, &expected_test_run);

    directory::testing::assert_suite_results(
        output_dir.path(),
        &suite_results,
        &expected_test_suites,
    );
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_syslog_to_directory() {
    let output_dir = tempfile::tempdir().expect("create temp directory");
    let mut test_params = new_test_params(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/error_logging_test.cm",
    );
    test_params.timeout = std::num::NonZeroU32::new(600);
    test_params.max_severity_logs = Some(Severity::Warn);

    let outcome = run_test_suite_lib::run_tests_and_get_outcome(
        fuchsia_component::client::connect_to_protocol::<RunBuilderMarker>()
            .expect("connecting to RunBuilderProxy"),
        vec![test_params],
        new_run_params(),
        None,
        false,
        Some(output_dir.path().to_path_buf()),
        futures::future::pending(),
    )
    .await;

    assert_eq!(outcome, Outcome::Failed);

    const EXPECTED_SYSLOG: &str =  "[TIMESTAMP][PID][TID][<root>][log_and_exit,error_logging_test] DEBUG: Logging initialized\n\
[TIMESTAMP][PID][TID][<root>][log_and_exit,error_logging_test] INFO: my info message\n\
[TIMESTAMP][PID][TID][<root>][log_and_exit,error_logging_test] WARN: my warn message\n\
[TIMESTAMP][PID][TID][<root>][log_and_exit,error_logging_test] ERROR: [../../src/sys/run_test_suite/tests/error_logging_test.rs(12)] my error message\n\
";
    let expected_test_run = ExpectedTestRun::new(directory::Outcome::Failed);
    let expected_test_suites = vec![ExpectedSuite::new(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/error_logging_test.cm",
        directory::Outcome::Passed,
    )
    .with_case(
        ExpectedTestCase::new("log_and_exit", directory::Outcome::Passed)
            .with_artifact(directory::ArtifactType::Stdout, "stdout.txt".into(), "")
            .with_artifact(directory::ArtifactType::Stderr, "stderr.txt".into(), "")
            .with_any_start_time()
            .with_no_run_duration(),
    )
    .with_matching_artifact(directory::ArtifactType::Syslog, "syslog.txt".into(), |actual| {
        assert_output!(actual.as_bytes(), EXPECTED_SYSLOG);
    })
    .with_matching_artifact(directory::ArtifactType::RestrictedLog, "restricted_logs.txt".into(), |actual| {
        assert!(actual.contains("ERROR: [../../src/sys/run_test_suite/tests/error_logging_test.rs(12)] my error message"))
    })
    .with_any_start_time()
    .with_any_run_duration()];

    let (run_result, suite_results) = directory::testing::parse_json_in_output(output_dir.path());

    directory::testing::assert_run_result(output_dir.path(), &run_result, &expected_test_run);

    directory::testing::assert_suite_results(
        output_dir.path(),
        &suite_results,
        &expected_test_suites,
    );
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_custom_artifacts_to_directory() {
    let output_dir = tempfile::tempdir().expect("create temp directory");
    let test_params = new_test_params(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/custom_artifact_user.cm",
    );

    let outcome = run_test_suite_lib::run_tests_and_get_outcome(
        fuchsia_component::client::connect_to_protocol::<RunBuilderMarker>()
            .expect("connecting to RunBuilderProxy"),
        vec![test_params],
        new_run_params(),
        None,
        false,
        Some(output_dir.path().to_path_buf()),
        futures::future::pending(),
    )
    .await;

    assert_eq!(outcome, Outcome::Passed);

    let expected_test_run = ExpectedTestRun::new(directory::Outcome::Passed);
    let expected_test_suites = vec![ExpectedSuite::new(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/custom_artifact_user.cm",
        directory::Outcome::Passed,
    )
    .with_case(
        ExpectedTestCase::new("use_artifact", directory::Outcome::Passed)
            .with_artifact(directory::ArtifactType::Stdout, "stdout.txt".into(), "")
            .with_artifact(directory::ArtifactType::Stderr, "stderr.txt".into(), "")
            .with_any_start_time()
            .with_no_run_duration(),
    )
    .with_artifact(directory::ArtifactType::Syslog, "syslog.txt".into(), "")
    .with_directory_artifact(
        directory::ArtifactMetadataV0 {
            artifact_type: directory::ArtifactType::Custom,
            component_moniker: Some(".".to_string()),
        },
        Option::<&str>::None,
        ExpectedDirectory::new().with_file("artifact.txt", "Hello, world!"),
    )
    .with_any_start_time()
    .with_any_run_duration()];

    let (run_result, suite_results) = directory::testing::parse_json_in_output(output_dir.path());

    directory::testing::assert_run_result(output_dir.path(), &run_result, &expected_test_run);

    directory::testing::assert_suite_results(
        output_dir.path(),
        &suite_results,
        &expected_test_suites,
    );
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_terminate_signal() {
    let output_dir = tempfile::tempdir().expect("create temp directory");
    let test_params = new_test_params(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/long_running_test.cm",
    );

    let outcome = run_test_suite_lib::run_tests_and_get_outcome(
        fuchsia_component::client::connect_to_protocol::<RunBuilderMarker>()
            .expect("connecting to RunBuilderProxy"),
        vec![test_params],
        new_run_params(),
        None,
        false,
        Some(output_dir.path().to_path_buf()),
        futures::future::ready(()),
    )
    .await;

    assert_eq!(outcome, Outcome::Cancelled);

    let expected_test_run = ExpectedTestRun::new(directory::Outcome::Inconclusive);

    let (run_result, suite_results) = directory::testing::parse_json_in_output(output_dir.path());

    directory::testing::assert_run_result(output_dir.path(), &run_result, &expected_test_run);

    // Based on the exact timing, it's possible some test cases were reported, so manually assert
    // only on the fields that shouldn't vary.
    assert_eq!(suite_results.len(), 1);
    let directory::SuiteResult::V0 { outcome: suite_outcome, name: suite_name, .. } =
        suite_results.into_iter().next().unwrap();
    assert_eq!(suite_outcome, directory::Outcome::Inconclusive);
    assert_eq!(
        suite_name,
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/long_running_test.cm"
    );

    // TODO(satsukiu): add a `Reporter` implementation for tests that signals on events and use it
    // to make more sophisticated tests. Since we need to make assertions on the directory reporter
    // simultaneously, we'll need to support writing to multiple reporters at once as well.
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_terminate_signal_multiple_suites() {
    let output_dir = tempfile::tempdir().expect("create temp directory");
    let test_params = new_test_params(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/long_running_test.cm",
    );

    let outcome = run_test_suite_lib::run_tests_and_get_outcome(
        fuchsia_component::client::connect_to_protocol::<RunBuilderMarker>()
            .expect("connecting to RunBuilderProxy"),
        vec![test_params; 10],
        new_run_params(),
        None,
        false,
        Some(output_dir.path().to_path_buf()),
        futures::future::ready(()),
    )
    .await;

    assert_eq!(outcome, Outcome::Cancelled);

    let expected_test_run = ExpectedTestRun::new(directory::Outcome::Inconclusive);

    let (run_result, suite_results) = directory::testing::parse_json_in_output(output_dir.path());

    directory::testing::assert_run_result(output_dir.path(), &run_result, &expected_test_run);

    // There should be exactly one test that started and is inconclusive. Remaining tests should
    // be NOT_STARTED. Note this assumes that test manager is running tests sequentially, this test
    // could start flaking when this changes.
    let (inconclusive_suite_results, other_suite_results): (Vec<_>, Vec<_>) = suite_results
        .into_iter()
        .partition(|&directory::SuiteResult::V0 { ref outcome, .. }| {
            *outcome == directory::Outcome::Inconclusive
        });
    assert_eq!(inconclusive_suite_results.len(), 1);
    assert_eq!(other_suite_results.len(), 9);

    let directory::SuiteResult::V0 { outcome: suite_outcome, name: suite_name, .. } =
        inconclusive_suite_results.into_iter().next().unwrap();
    assert_eq!(suite_outcome, directory::Outcome::Inconclusive);
    assert_eq!(
        suite_name,
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/long_running_test.cm"
    );

    other_suite_results.into_iter().for_each(|suite_result| {
        directory::testing::assert_suite_result(
            output_dir.path(),
            &suite_result,
            &ExpectedSuite::new(
                "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/long_running_test.cm",
                directory::Outcome::NotStarted,
            ),
        );
    });
}
