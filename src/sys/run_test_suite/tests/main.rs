// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use async_trait::async_trait;
use diagnostics_data::Severity;
use fidl_fuchsia_test_manager::{LaunchError, RunBuilderMarker, RunBuilderProxy};
use futures::prelude::*;
use matches::assert_matches;
use regex::Regex;
use run_test_suite_lib::{
    diagnostics, output, Outcome, RunTestSuiteError, SuiteRunResult, TestParams,
};
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
struct RunBuilderConnector {}

#[async_trait]
impl run_test_suite_lib::BuilderConnector for RunBuilderConnector {
    async fn connect(&self) -> RunBuilderProxy {
        fuchsia_component::client::connect_to_protocol::<RunBuilderMarker>()
            .expect("connecting to RunBuilderProxy")
    }
}

impl RunBuilderConnector {
    fn new() -> Box<Self> {
        Box::new(Self {})
    }
}

fn new_test_params(test_url: &str) -> TestParams {
    TestParams {
        test_url: test_url.to_string(),
        builder_connector: RunBuilderConnector::new(),
        timeout: None,
        test_filters: None,
        also_run_disabled_tests: false,
        test_args: vec![],
        parallel: None,
    }
}

/// run specified test once.
async fn run_test_once<W: Write + Send>(
    test_params: TestParams,
    log_opts: diagnostics::LogCollectionOptions,
    writer: &mut W,
) -> Result<SuiteRunResult, RunTestSuiteError> {
    let test_result = {
        let mut reporter = output::RunReporter::new_noop();
        let streams =
            run_test_suite_lib::run_test(test_params, 1, log_opts, writer, &mut reporter).await?;
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
        diagnostics::LogCollectionOptions::default(),
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
    assert!(run_result.successful_completion);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_passing_v2_test() {
    let output_dir = tempfile::tempdir().expect("create temp directory");
    let mut output: Vec<u8> = vec![];
    let mut reporter = output::RunReporter::new(output_dir.path().to_path_buf()).unwrap();
    let streams = run_test_suite_lib::run_test(
        new_test_params(
            "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/passing-test-example.cm",
        ),
        1,
        diagnostics::LogCollectionOptions::default(),
        &mut output,
        &mut reporter,
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
    assert!(run_result.successful_completion);

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
    let mut reporter = output::RunReporter::new(output_dir.path().to_path_buf()).unwrap();
    let streams = run_test_suite_lib::run_test(
        new_test_params(
            "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/test-with-stderr.cm",
        ),
        1,
        diagnostics::LogCollectionOptions::default(),
        &mut output,
        &mut reporter,
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
    let mut reporter = output::RunReporter::new_noop();
    let streams = run_test_suite_lib::run_test(
            new_test_params(
                "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/passing-test-example.cm",
                ),
            10, diagnostics::LogCollectionOptions::default(),&mut output,
            &mut reporter
        )
    .await.expect("run test");
    let run_results = streams.collect::<Vec<_>>().await;

    assert_eq!(run_results.len(), 10);
    for run_result in run_results {
        let run_result = run_result.expect("Running test should not fail");
        assert_eq!(run_result.outcome, Outcome::Passed);
        assert_eq!(run_result.executed, run_result.passed);

        let expected = vec!["Example.Test1", "Example.Test2", "Example.Test3"];

        assert_eq!(run_result.executed, expected);
        assert!(run_result.successful_completion);
    }
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_with_filter() {
    let mut output: Vec<u8> = vec![];
    let mut test_params = new_test_params(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/passing-test-example.cm",
    );

    test_params.test_filters = Some(vec!["*Test3".to_string()]);
    let run_result =
        run_test_once(test_params, diagnostics::LogCollectionOptions::default(), &mut output)
            .await
            .expect("Running test should not fail");

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
    assert!(run_result.successful_completion);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_with_multiple_filter() {
    let mut output: Vec<u8> = vec![];
    let mut test_params = new_test_params(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/passing-test-example.cm",
    );

    test_params.test_filters = Some(vec!["*Test3".to_string(), "*Test1".to_string()]);
    let run_result =
        run_test_once(test_params, diagnostics::LogCollectionOptions::default(), &mut output)
            .await
            .expect("Running test should not fail");

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
    assert!(run_result.successful_completion);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_empty_test() {
    let mut output: Vec<u8> = vec![];
    let run_result = run_test_once(
        new_test_params(
            "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/no-test-example.cm",
        ),
        diagnostics::LogCollectionOptions::default(),
        &mut output,
    )
    .await
    .expect("Running test should not fail");

    assert_eq!(run_result.executed.len(), 0);
    assert_eq!(run_result.passed.len(), 0);
    assert!(run_result.successful_completion);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_huge_test() {
    let mut output: Vec<u8> = vec![];

    let run_result = run_test_once(
        new_test_params(
            "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/huge-test-example.cm",
        ),
        diagnostics::LogCollectionOptions::default(),
        &mut output,
    )
    .await
    .expect("Running test should not fail");

    assert_eq!(run_result.executed.len(), 1_000);
    assert_eq!(run_result.passed.len(), 1_000);
    assert!(run_result.successful_completion);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_disabled_test_exclude_disabled() {
    let mut output: Vec<u8> = vec![];
    let run_result = run_test_once(
            new_test_params(
                "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/disabled-test-example.cm",
            ),
            diagnostics::LogCollectionOptions::default(),
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

    assert!(run_result.successful_completion);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_disabled_test_include_disabled() {
    let mut output: Vec<u8> = vec![];
    let mut test_params = new_test_params(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/disabled-test-example.cm",
    );
    test_params.also_run_disabled_tests = true;
    let run_result =
        run_test_once(test_params, diagnostics::LogCollectionOptions::default(), &mut output)
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
[FAILED]	Example.Test3
";
    assert_output!(output, expected_output);

    assert_eq!(run_result.outcome, Outcome::Failed);

    // "skipped" is a form of "executed"
    let expected_executed = vec!["Example.Test1", "Example.Test2", "Example.Test3"];
    let expected_passed = vec!["Example.Test1", "Example.Test2"];

    assert_eq!(run_result.executed, expected_executed);
    assert_eq!(run_result.passed, expected_passed);

    assert!(run_result.successful_completion);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_failing_test() {
    let mut output: Vec<u8> = vec![];
    let run_result = run_test_once(
            new_test_params(
                "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/failing-test-example.cm",
            ),
            diagnostics::LogCollectionOptions::default(),
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
    assert!(run_result.successful_completion);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_failing_v2_test_multiple_times() {
    let mut output: Vec<u8> = vec![];
    let mut reporter = output::RunReporter::new_noop();
    let streams = run_test_suite_lib::run_test(
            new_test_params(
                "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/failing-test-example.cm",
                ),
            10, diagnostics::LogCollectionOptions::default(),&mut output, &mut reporter
        )
    .await.expect("run test");
    let run_results = streams.collect::<Vec<_>>().await;

    assert_eq!(run_results.len(), 10);
    for run_result in run_results {
        let run_result = run_result.expect("Running test should not fail");
        assert_eq!(run_result.outcome, Outcome::Failed);
        assert_eq!(run_result.executed, vec!["Example.Test1", "Example.Test2", "Example.Test3"]);
        assert_eq!(run_result.passed, vec!["Example.Test1", "Example.Test3"]);
        assert!(run_result.successful_completion);
    }
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_incomplete_test() {
    let mut output: Vec<u8> = vec![];
    let run_result = run_test_once(
            new_test_params(
                "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/incomplete-test-example.cm",
            ),
            diagnostics::LogCollectionOptions::default(),
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

    assert_eq!(run_result.outcome, Outcome::Inconclusive);

    assert_eq!(run_result.executed, vec!["Example.Test1", "Example.Test2", "Example.Test3"]);
    assert_eq!(run_result.passed, vec!["Example.Test2"]);
    assert!(run_result.successful_completion);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_invalid_test() {
    let mut output: Vec<u8> = vec![];
    let run_result = run_test_once(
            new_test_params(
                "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/invalid-test-example.cm",

            ),
            diagnostics::LogCollectionOptions::default(),
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

    assert_eq!(run_result.outcome, Outcome::Inconclusive);

    assert_eq!(run_result.executed, vec!["Example.Test1", "Example.Test2", "Example.Test3"]);
    assert_eq!(run_result.passed, vec!["Example.Test2"]);
    assert!(run_result.successful_completion);
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
        diagnostics::LogCollectionOptions::default(),
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
    assert!(run_result.successful_completion);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_timeout() {
    let mut output: Vec<u8> = vec![];
    let mut test_params = new_test_params(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/long_running_test.cm",
    );
    test_params.timeout = std::num::NonZeroU32::new(1);
    let run_result =
        run_test_once(test_params, diagnostics::LogCollectionOptions::default(), &mut output)
            .await
            .expect("Running test should not fail");
    let expected_output = "[RUNNING]	LongRunningTest.LongRunning
[TIMED_OUT]	LongRunningTest.LongRunning
";
    assert_output!(output, expected_output);
    assert_eq!(run_result.outcome, Outcome::Timedout);

    assert_eq!(run_result.passed, Vec::<String>::new());
    assert!(run_result.successful_completion);
}

#[fuchsia_async::run_singlethreaded(test)]
// when a test times out, we should not run it again.
async fn test_timeout_multiple_times() {
    let mut output: Vec<u8> = vec![];
    let mut reporter = output::RunReporter::new_noop();
    let mut test_params = new_test_params(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/long_running_test.cm",
    );
    test_params.timeout = std::num::NonZeroU32::new(1);
    let streams = run_test_suite_lib::run_test(
        test_params,
        10,
        diagnostics::LogCollectionOptions::default(),
        &mut output,
        &mut reporter,
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
    assert!(run_result.successful_completion);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_passes_with_large_timeout() {
    let mut output: Vec<u8> = vec![];
    let mut test_params = new_test_params(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/echo_test_realm.cm",
    );
    test_params.timeout = std::num::NonZeroU32::new(600);
    let run_result =
        run_test_once(test_params, diagnostics::LogCollectionOptions::default(), &mut output)
            .await
            .expect("Running test should not fail");

    let expected_output = "[RUNNING]	EchoTest
[PASSED]	EchoTest
";
    assert_output!(output, expected_output);

    assert_eq!(run_result.outcome, Outcome::Passed);

    assert_eq!(run_result.executed, vec!["EchoTest"]);
    assert_eq!(run_result.passed, vec!["EchoTest"]);
    assert!(run_result.successful_completion);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_logging_component() {
    let mut output: Vec<u8> = vec![];
    let mut test_params = new_test_params(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/logging_test.cm",
    );
    test_params.timeout = std::num::NonZeroU32::new(600);
    let run_result =
        run_test_once(test_params, diagnostics::LogCollectionOptions::default(), &mut output)
            .await
            .expect("Running test should not fail");

    let expected_output = "[RUNNING]	log_and_exit
[TIMESTAMP][PID][TID][<root>][log_and_exit_test,logging_test] DEBUG: Logging initialized\n\
[TIMESTAMP][PID][TID][<root>][log_and_exit_test,logging_test] DEBUG: my debug message\n\
[TIMESTAMP][PID][TID][<root>][log_and_exit_test,logging_test] INFO: my info message\n\
[TIMESTAMP][PID][TID][<root>][log_and_exit_test,logging_test] WARN: my warn message\n\
[PASSED]	log_and_exit
";
    assert_output!(output, expected_output);
    assert_eq!(run_result.outcome, Outcome::Passed);
    assert!(run_result.successful_completion);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_logging_component_min_severity() {
    let mut output: Vec<u8> = vec![];
    let mut test_params = new_test_params(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/logging_test.cm",
    );
    test_params.timeout = std::num::NonZeroU32::new(600);
    let log_opts = diagnostics::LogCollectionOptions {
        min_severity: Some(Severity::Info),
        ..diagnostics::LogCollectionOptions::default()
    };
    let run_result = run_test_once(test_params, log_opts, &mut output)
        .await
        .expect("Running test should not fail");

    let expected_output = "[RUNNING]	log_and_exit
[TIMESTAMP][PID][TID][<root>][log_and_exit_test,logging_test] INFO: my info message\n\
[TIMESTAMP][PID][TID][<root>][log_and_exit_test,logging_test] WARN: my warn message\n\
[PASSED]	log_and_exit
";
    assert_output!(output, expected_output);
    assert_eq!(run_result.outcome, Outcome::Passed);
    assert!(run_result.successful_completion);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_stdout_and_log_ansi() {
    let mut output: Vec<u8> = vec![];
    let mut test_params = new_test_params(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/stdout_ansi_test.cm",
    );
    test_params.timeout = std::num::NonZeroU32::new(600);
    let log_opts = diagnostics::LogCollectionOptions {
        min_severity: Some(Severity::Info),
        ..diagnostics::LogCollectionOptions::default()
    };
    let run_result = run_test_once(test_params, log_opts, &mut output)
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
    assert!(run_result.successful_completion);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_stdout_and_log_filter_ansi() {
    let mut output: Vec<u8> = vec![];
    let mut ansi_filter = output::AnsiFilterWriter::new(&mut output);
    let mut reporter = output::RunReporter::new_noop();
    let mut test_params = new_test_params(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/stdout_ansi_test.cm",
    );
    test_params.timeout = std::num::NonZeroU32::new(600);
    let log_opts = diagnostics::LogCollectionOptions {
        min_severity: Some(Severity::Info),
        ..diagnostics::LogCollectionOptions::default()
    };

    let test_result = {
        let streams =
            run_test_suite_lib::run_test(test_params, 1, log_opts, &mut ansi_filter, &mut reporter)
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
    assert!(test_result.successful_completion);
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
    let log_opts = diagnostics::LogCollectionOptions {
        max_severity: Some(max_severity),
        ..diagnostics::LogCollectionOptions::default()
    };
    let run_result = run_test_once(test_params, log_opts, &mut output)
        .await
        .expect("Running test should not fail");

    let expected_output = "[RUNNING]	log_and_exit
[TIMESTAMP][PID][TID][<root>][log_and_exit_test,error_logging_test] DEBUG: Logging initialized\n\
[TIMESTAMP][PID][TID][<root>][log_and_exit_test,error_logging_test] INFO: my info message\n\
[TIMESTAMP][PID][TID][<root>][log_and_exit_test,error_logging_test] WARN: my warn message\n\
[TIMESTAMP][PID][TID][<root>][log_and_exit_test,error_logging_test] ERROR: [../../src/sys/run_test_suite/tests/error_logging_test.rs(23)] my error message\n\
[PASSED]	log_and_exit
";

    assert_output!(output, expected_output);
    assert_eq!(run_result.outcome, Outcome::Passed);
    assert!(run_result.successful_completion);

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
                    "[TIMESTAMP][PID][TID][<root>][log_and_exit_test,error_logging_test] WARN: my warn message".to_owned(),
                    "[TIMESTAMP][PID][TID][<root>][log_and_exit_test,error_logging_test] ERROR: [../../src/sys/run_test_suite/tests/error_logging_test.rs(23)] my error message".to_owned(),
                ]
            );
        }
        Severity::Warn => {
            assert!(run_result.restricted_logs.len() > 0, "expected logs to fail the test");
            assert_eq!(
                run_result.restricted_logs.into_iter().map(sanitize_log_for_comparison).collect::<Vec<_>>(),
                vec![
                    "[TIMESTAMP][PID][TID][<root>][log_and_exit_test,error_logging_test] ERROR: [../../src/sys/run_test_suite/tests/error_logging_test.rs(23)] my error message".to_owned(),
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
    let log_opts = diagnostics::LogCollectionOptions::default();
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
        test_params,
        diagnostics::LogCollectionOptions::default(),
        std::num::NonZeroU16::new(1).unwrap(),
        false,
        Some(output_dir.path().to_path_buf()),
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
            .with_no_run_duration()
            .with_any_start_time(),
    )
    .with_case(
        ExpectedTestCase::new("log_ansi_test", directory::Outcome::Passed)
            .with_artifact(directory::ArtifactType::Stdout, "stdout.txt".into(), "")
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
    let log_opts = diagnostics::LogCollectionOptions {
        max_severity: Some(Severity::Warn),
        ..diagnostics::LogCollectionOptions::default()
    };

    let outcome = run_test_suite_lib::run_tests_and_get_outcome(
        test_params,
        log_opts,
        std::num::NonZeroU16::new(1).unwrap(),
        false,
        Some(output_dir.path().to_path_buf()),
    )
    .await;

    assert_eq!(outcome, Outcome::Failed);

    const EXPECTED_SYSLOG: &str =  "[TIMESTAMP][PID][TID][<root>][log_and_exit_test,error_logging_test] DEBUG: Logging initialized\n\
[TIMESTAMP][PID][TID][<root>][log_and_exit_test,error_logging_test] INFO: my info message\n\
[TIMESTAMP][PID][TID][<root>][log_and_exit_test,error_logging_test] WARN: my warn message\n\
[TIMESTAMP][PID][TID][<root>][log_and_exit_test,error_logging_test] ERROR: [../../src/sys/run_test_suite/tests/error_logging_test.rs(23)] my error message\n\
";
    let expected_test_run = ExpectedTestRun::new(directory::Outcome::Failed);
    let expected_test_suites = vec![ExpectedSuite::new(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/error_logging_test.cm",
        directory::Outcome::Passed,
    )
    .with_case(
        ExpectedTestCase::new("log_and_exit", directory::Outcome::Passed)
            .with_artifact(directory::ArtifactType::Stdout, "stdout.txt".into(), "")
            .with_any_start_time()
            .with_no_run_duration(),
    )
    .with_matching_artifact(directory::ArtifactType::Syslog, "syslog.txt".into(), |actual| {
        assert_output!(actual.as_bytes(), EXPECTED_SYSLOG);
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
        test_params,
        diagnostics::LogCollectionOptions::default(),
        std::num::NonZeroU16::new(1).unwrap(),
        false,
        Some(output_dir.path().to_path_buf()),
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
