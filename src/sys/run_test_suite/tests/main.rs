// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use diagnostics_data::Severity;
use fidl_fuchsia_test_manager::{HarnessMarker, HarnessProxy};
use futures::prelude::*;
use regex::Regex;
use run_test_suite_lib::{diagnostics, output, Outcome, RunResult, TestParams};
use std::io::Write;
use std::str::from_utf8;
use test_output_directory::{
    self as directory,
    testing::{ExpectedSuite, ExpectedTestCase, ExpectedTestRun},
};

/// split and sort output as output can come in any order.
/// `output` is of type vec<u8> and `expected_output` is a string.
macro_rules! assert_output {
    ($output:expr, $expected_output:expr) => {
        let mut expected_output = $expected_output.split("\n").collect::<Vec<_>>();

        // no need to check for lines starting with "[output - ". We just want to make sure that
        // we are printing important details.
        let mut output = from_utf8(&$output)
            .expect("we should not get utf8 error.")
            .split("\n")
            .filter(|x| {
                let is_output = x.starts_with("[output - ");
                let is_debug_data_warn = is_debug_data_warning(x);
                !(is_output || is_debug_data_warn)
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

fn new_test_params(test_url: &str, harness: HarnessProxy) -> TestParams {
    TestParams {
        test_url: test_url.to_string(),
        harness: harness,
        timeout: None,
        test_filter: None,
        also_run_disabled_tests: false,
        test_args: vec![],
        parallel: None,
    }
}

#[derive(Debug)]
struct RunTestResult {
    test_result: RunResult,
    log_collection_outcome: diagnostics::LogCollectionOutcome,
}

/// run specified test once.
async fn run_test_once<W: Write + Send>(
    test_params: TestParams,
    log_opts: diagnostics::LogCollectionOptions,
    writer: &mut W,
) -> Result<RunTestResult, anyhow::Error> {
    let (log_stream, test_result) = {
        let mut reporter = output::RunReporter::new_noop();
        let streams = run_test_suite_lib::run_test(test_params, 1, writer, &mut reporter).await?;
        let mut results = streams.results.collect::<Vec<_>>().await;
        assert_eq!(results.len(), 1, "{:?}", results);
        (streams.logs, results.pop().unwrap())
    };
    let test_result = test_result?;
    let log_collection_outcome = diagnostics::collect_logs(log_stream, writer, log_opts).await?;
    Ok(RunTestResult { test_result, log_collection_outcome })
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_no_clean_exit() {
    let mut output: Vec<u8> = vec![];
    let harness = fuchsia_component::client::connect_to_protocol::<HarnessMarker>()
        .expect("connecting to HarnessProxy");
    let run_result = run_test_once(
        new_test_params(
            "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/no-onfinished-after-test-example.cm",
            harness),
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

    assert_eq!(run_result.test_result.outcome, Outcome::Passed);
    assert_eq!(run_result.test_result.executed, run_result.test_result.passed);

    let expected = vec!["Example.Test1", "Example.Test2", "Example.Test3"];

    assert_eq!(run_result.test_result.executed, expected);
    assert!(!run_result.test_result.successful_completion);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_passing_v2_test() {
    let mut output: Vec<u8> = vec![];
    let harness = fuchsia_component::client::connect_to_protocol::<HarnessMarker>()
        .expect("connecting to HarnessProxy");

    let run_result = run_test_once(
            new_test_params(
                "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/passing-test-example.cm",
                harness),
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

    assert_eq!(run_result.test_result.outcome, Outcome::Passed);
    assert_eq!(run_result.test_result.executed, run_result.test_result.passed);

    let expected = vec!["Example.Test1", "Example.Test2", "Example.Test3"];

    assert_eq!(run_result.test_result.executed, expected);
    assert!(run_result.test_result.successful_completion);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_passing_v2_test_multiple_times() {
    let mut output: Vec<u8> = vec![];
    let mut reporter = output::RunReporter::new_noop();
    let harness = fuchsia_component::client::connect_to_protocol::<HarnessMarker>()
        .expect("connecting to HarnessProxy");

    let streams = run_test_suite_lib::run_test(
            new_test_params(
                "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/passing-test-example.cm",
                harness),
            10, &mut output,
            &mut reporter
        )
    .await.expect("run test");
    let run_results = streams.results.collect::<Vec<_>>().await;

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
    let harness = fuchsia_component::client::connect_to_protocol::<HarnessMarker>()
        .expect("connecting to HarnessProxy");
    let mut test_params = new_test_params(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/passing-test-example.cm",
        harness,
    );

    test_params.test_filter = Some("*Test3".to_string());
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

    assert_eq!(run_result.test_result.outcome, Outcome::Passed);
    assert_eq!(run_result.test_result.executed, run_result.test_result.passed);

    let expected = vec!["Example.Test3"];

    assert_eq!(run_result.test_result.executed, expected);
    assert!(run_result.test_result.successful_completion);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_empty_test() {
    let mut output: Vec<u8> = vec![];
    let harness = fuchsia_component::client::connect_to_protocol::<HarnessMarker>()
        .expect("connecting to HarnessProxy");

    let run_result = run_test_once(
        new_test_params(
            "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/no-test-example.cm",
            harness,
        ),
        diagnostics::LogCollectionOptions::default(),
        &mut output,
    )
    .await
    .expect("Running test should not fail");

    assert_eq!(run_result.test_result.executed.len(), 0);
    assert_eq!(run_result.test_result.passed.len(), 0);
    assert!(run_result.test_result.successful_completion);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_huge_test() {
    let mut output: Vec<u8> = vec![];
    let harness = fuchsia_component::client::connect_to_protocol::<HarnessMarker>()
        .expect("connecting to HarnessProxy");

    let run_result = run_test_once(
        new_test_params(
            "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/huge-test-example.cm",
            harness,
        ),
        diagnostics::LogCollectionOptions::default(),
        &mut output,
    )
    .await
    .expect("Running test should not fail");

    assert_eq!(run_result.test_result.executed.len(), 1_000);
    assert_eq!(run_result.test_result.passed.len(), 1_000);
    assert!(run_result.test_result.successful_completion);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_disabled_test_exclude_disabled() {
    let mut output: Vec<u8> = vec![];
    let harness = fuchsia_component::client::connect_to_protocol::<HarnessMarker>()
        .expect("connecting to HarnessProxy");

    let run_result = run_test_once(
            new_test_params(
                "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/disabled-test-example.cm",
                harness,
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

    assert_eq!(run_result.test_result.outcome, Outcome::Passed);

    // "skipped" is a form of "executed"
    let expected_executed = vec!["Example.Test1", "Example.Test2", "Example.Test3"];
    let expected_passed = vec!["Example.Test1"];

    assert_eq!(run_result.test_result.executed, expected_executed);
    assert_eq!(run_result.test_result.passed, expected_passed);

    assert!(run_result.test_result.successful_completion);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_disabled_test_include_disabled() {
    let mut output: Vec<u8> = vec![];
    let harness = fuchsia_component::client::connect_to_protocol::<HarnessMarker>()
        .expect("connecting to HarnessProxy");
    let mut test_params = new_test_params(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/disabled-test-example.cm",
        harness,
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

    assert_eq!(run_result.test_result.outcome, Outcome::Failed);

    // "skipped" is a form of "executed"
    let expected_executed = vec!["Example.Test1", "Example.Test2", "Example.Test3"];
    let expected_passed = vec!["Example.Test1", "Example.Test2"];

    assert_eq!(run_result.test_result.executed, expected_executed);
    assert_eq!(run_result.test_result.passed, expected_passed);

    assert!(run_result.test_result.successful_completion);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_failing_test() {
    let mut output: Vec<u8> = vec![];
    let harness = fuchsia_component::client::connect_to_protocol::<HarnessMarker>()
        .expect("connecting to HarnessProxy");

    let run_result = run_test_once(
            new_test_params(
                "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/failing-test-example.cm",
                harness,
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

    assert_eq!(run_result.test_result.outcome, Outcome::Failed);

    assert_eq!(
        run_result.test_result.executed,
        vec!["Example.Test1", "Example.Test2", "Example.Test3"]
    );
    assert_eq!(run_result.test_result.passed, vec!["Example.Test1", "Example.Test3"]);
    assert!(run_result.test_result.successful_completion);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_failing_v2_test_multiple_times() {
    let mut output: Vec<u8> = vec![];
    let mut reporter = output::RunReporter::new_noop();
    let harness = fuchsia_component::client::connect_to_protocol::<HarnessMarker>()
        .expect("connecting to HarnessProxy");

    let streams = run_test_suite_lib::run_test(
            new_test_params(
                "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/failing-test-example.cm",
                harness),
            10, &mut output, &mut reporter
        )
    .await.expect("run test");
    let run_results = streams.results.collect::<Vec<_>>().await;

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
    let harness = fuchsia_component::client::connect_to_protocol::<HarnessMarker>()
        .expect("connecting to HarnessProxy");

    let run_result = run_test_once(
            new_test_params(
                "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/incomplete-test-example.cm",
                harness,
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

    assert_eq!(run_result.test_result.outcome, Outcome::Inconclusive);

    assert_eq!(
        run_result.test_result.executed,
        vec!["Example.Test1", "Example.Test2", "Example.Test3"]
    );
    assert_eq!(run_result.test_result.passed, vec!["Example.Test2"]);
    assert!(run_result.test_result.successful_completion);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_invalid_test() {
    let mut output: Vec<u8> = vec![];
    let harness = fuchsia_component::client::connect_to_protocol::<HarnessMarker>()
        .expect("connecting to HarnessProxy");

    let run_result = run_test_once(
            new_test_params(
                "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/invalid-test-example.cm",
                harness,
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
[ERROR]	Example.Test1
[RUNNING]	Example.Test2
log1 for Example.Test2
log2 for Example.Test2
log3 for Example.Test2
[PASSED]	Example.Test2
[RUNNING]	Example.Test3
log1 for Example.Test3
log2 for Example.Test3
log3 for Example.Test3
[ERROR]	Example.Test3
";
    assert_output!(output, expected_output);

    assert_eq!(run_result.test_result.outcome, Outcome::Error);

    assert_eq!(
        run_result.test_result.executed,
        vec!["Example.Test1", "Example.Test2", "Example.Test3"]
    );
    assert_eq!(run_result.test_result.passed, vec!["Example.Test2"]);
    assert!(run_result.test_result.successful_completion);
}

// This test also acts an example on how to right a v2 test.
// This will launch a echo_realm which will inject echo_server, launch v2 test which will
// then test that server out and return back results.
#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_run_echo_test() {
    let mut output: Vec<u8> = vec![];
    let harness = fuchsia_component::client::connect_to_protocol::<HarnessMarker>()
        .expect("connecting to HarnessProxy");

    let run_result = run_test_once(
        new_test_params(
            "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/echo_test_realm.cm",
            harness,
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

    assert_eq!(run_result.test_result.outcome, Outcome::Passed);

    assert_eq!(run_result.test_result.executed, vec!["EchoTest"]);
    assert_eq!(run_result.test_result.passed, vec!["EchoTest"]);
    assert!(run_result.test_result.successful_completion);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_timeout() {
    let mut output: Vec<u8> = vec![];
    let harness = fuchsia_component::client::connect_to_protocol::<HarnessMarker>()
        .expect("connecting to HarnessProxy");

    let mut test_params = new_test_params(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/long_running_test.cm",
        harness,
    );
    test_params.timeout = std::num::NonZeroU32::new(1);
    let run_result =
        run_test_once(test_params, diagnostics::LogCollectionOptions::default(), &mut output)
            .await
            .expect("Running test should not fail");

    assert_eq!(run_result.test_result.outcome, Outcome::Timedout);

    assert_eq!(run_result.test_result.passed, Vec::<String>::new());
    assert!(!run_result.test_result.successful_completion);
}

#[fuchsia_async::run_singlethreaded(test)]
// when a test times out, we should not run it again.
async fn test_timeout_multiple_times() {
    let mut output: Vec<u8> = vec![];
    let mut reporter = output::RunReporter::new_noop();
    let harness = fuchsia_component::client::connect_to_protocol::<HarnessMarker>()
        .expect("connecting to HarnessProxy");

    let mut test_params = new_test_params(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/long_running_test.cm",
        harness,
    );
    test_params.timeout = std::num::NonZeroU32::new(1);
    let streams = run_test_suite_lib::run_test(test_params, 10, &mut output, &mut reporter)
        .await
        .expect("run test");
    let mut run_results = streams.results.collect::<Vec<_>>().await;
    assert_eq!(run_results.len(), 1);
    let run_result = run_results.pop().unwrap().unwrap();
    assert_eq!(run_result.outcome, Outcome::Timedout);

    assert_eq!(run_result.passed, Vec::<String>::new());
    assert!(!run_result.successful_completion);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_passes_with_large_timeout() {
    let mut output: Vec<u8> = vec![];
    let harness = fuchsia_component::client::connect_to_protocol::<HarnessMarker>()
        .expect("connecting to HarnessProxy");

    let mut test_params = new_test_params(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/echo_test_realm.cm",
        harness,
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

    assert_eq!(run_result.test_result.outcome, Outcome::Passed);

    assert_eq!(run_result.test_result.executed, vec!["EchoTest"]);
    assert_eq!(run_result.test_result.passed, vec!["EchoTest"]);
    assert!(run_result.test_result.successful_completion);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_logging_component() {
    let mut output: Vec<u8> = vec![];
    let harness = fuchsia_component::client::connect_to_protocol::<HarnessMarker>()
        .expect("connecting to HarnessProxy");

    let mut test_params = new_test_params(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/logging_test.cm",
        harness,
    );
    test_params.timeout = std::num::NonZeroU32::new(600);
    let run_result =
        run_test_once(test_params, diagnostics::LogCollectionOptions::default(), &mut output)
            .await
            .expect("Running test should not fail");

    let expected_output = "[RUNNING]	log_and_exit
[TIMESTAMP][PID][TID][<root>][log_and_exit_test,logging_test] DEBUG: my debug message \n\
[TIMESTAMP][PID][TID][<root>][log_and_exit_test,logging_test] INFO: my info message \n\
[TIMESTAMP][PID][TID][<root>][log_and_exit_test,logging_test] WARN: my warn message \n\
[PASSED]	log_and_exit
";
    assert_output!(output, expected_output);
    assert_eq!(run_result.test_result.outcome, Outcome::Passed);
    assert!(run_result.test_result.successful_completion);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_logging_component_min_severity() {
    let mut output: Vec<u8> = vec![];
    let harness = fuchsia_component::client::connect_to_protocol::<HarnessMarker>()
        .expect("connecting to HarnessProxy");

    let mut test_params = new_test_params(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/logging_test.cm",
        harness,
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
[TIMESTAMP][PID][TID][<root>][log_and_exit_test,logging_test] INFO: my info message \n\
[TIMESTAMP][PID][TID][<root>][log_and_exit_test,logging_test] WARN: my warn message \n\
[PASSED]	log_and_exit
";
    assert_output!(output, expected_output);
    assert_eq!(run_result.test_result.outcome, Outcome::Passed);
    assert!(run_result.test_result.successful_completion);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_stdout_ansi() {
    let mut output: Vec<u8> = vec![];
    let harness = fuchsia_component::client::connect_to_protocol::<HarnessMarker>()
        .expect("connecting to HarnessProxy");

    let mut test_params = new_test_params(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/stdout_ansi_test.cm",
        harness,
    );
    test_params.timeout = std::num::NonZeroU32::new(600);
    let log_opts = diagnostics::LogCollectionOptions {
        min_severity: Some(Severity::Info),
        ..diagnostics::LogCollectionOptions::default()
    };
    let run_result = run_test_once(test_params, log_opts, &mut output)
        .await
        .expect("Running test should not fail");

    let expected_output = "[RUNNING]	stdout_ansi_test
\u{1b}[31mred stdout\u{1b}[0m
[PASSED]	stdout_ansi_test
";
    assert_output!(output, expected_output);
    assert_eq!(run_result.test_result.outcome, Outcome::Passed);
    assert!(run_result.test_result.successful_completion);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_stdout_filter_ansi() {
    let mut output: Vec<u8> = vec![];
    let mut ansi_filter = output::AnsiFilterWriter::new(&mut output);
    let mut reporter = output::RunReporter::new_noop();
    let harness = fuchsia_component::client::connect_to_protocol::<HarnessMarker>()
        .expect("connecting to HarnessProxy");

    let mut test_params = new_test_params(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/stdout_ansi_test.cm",
        harness,
    );
    test_params.timeout = std::num::NonZeroU32::new(600);
    let log_opts = diagnostics::LogCollectionOptions {
        min_severity: Some(Severity::Info),
        ..diagnostics::LogCollectionOptions::default()
    };

    let (log_stream, test_result) = {
        let streams = run_test_suite_lib::run_test(test_params, 1, &mut ansi_filter, &mut reporter)
            .await
            .unwrap();
        let mut results = streams.results.collect::<Vec<_>>().await;
        assert_eq!(results.len(), 1, "{:?}", results);
        (streams.logs, results.pop().unwrap().unwrap())
    };
    let _ = diagnostics::collect_logs(log_stream, &mut ansi_filter, log_opts).await.unwrap();

    let expected_output = "[RUNNING]	stdout_ansi_test
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
    let harness = fuchsia_component::client::connect_to_protocol::<HarnessMarker>()
        .expect("connecting to HarnessProxy");

    let mut test_params = new_test_params(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/error_logging_test.cm",
        harness,
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
[TIMESTAMP][PID][TID][<root>][log_and_exit_test,error_logging_test] INFO: my info message \n\
[TIMESTAMP][PID][TID][<root>][log_and_exit_test,error_logging_test] WARN: my warn message \n\
[TIMESTAMP][PID][TID][<root>][log_and_exit_test,error_logging_test] ERROR: [src/sys/run_test_suite/tests/error_logging_test.rs(13)] my error message \n\
[PASSED]	log_and_exit
";

    assert_output!(output, expected_output);
    assert_eq!(run_result.test_result.outcome, Outcome::Passed);
    assert!(run_result.test_result.successful_completion);

    match max_severity {
        Severity::Info => {
            let restricted_logs = match run_result.log_collection_outcome {
                diagnostics::LogCollectionOutcome::Error { restricted_logs } => restricted_logs,
                _ => panic!("expected logs to fail the test"),
            };
            let logs = restricted_logs
                .into_iter()
                .filter(|log| !is_debug_data_warning(log))
                .map(sanitize_log_for_comparison)
                .collect::<Vec<_>>();
            assert_eq!(
                logs,
                vec![
                    "[TIMESTAMP][PID][TID][<root>][log_and_exit_test,error_logging_test] WARN: my warn message ".to_owned(),
                    "[TIMESTAMP][PID][TID][<root>][log_and_exit_test,error_logging_test] ERROR: [src/sys/run_test_suite/tests/error_logging_test.rs(13)] my error message ".to_owned(),
                ]
            );
        }
        Severity::Warn => {
            let restricted_logs = match run_result.log_collection_outcome {
                diagnostics::LogCollectionOutcome::Error { restricted_logs } => restricted_logs,
                _ => panic!("expected logs to fail the test"),
            };
            assert_eq!(
                restricted_logs.into_iter().map(sanitize_log_for_comparison).collect::<Vec<_>>(),
                vec![
                    "[TIMESTAMP][PID][TID][<root>][log_and_exit_test,error_logging_test] ERROR: [src/sys/run_test_suite/tests/error_logging_test.rs(13)] my error message ".to_owned(),
                ]
            );
        }
        Severity::Error => {
            assert_eq!(
                run_result.log_collection_outcome,
                diagnostics::LogCollectionOutcome::Passed
            );
        }
        _ => unreachable!("Not used"),
    }
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_stdout_to_directory() {
    let output_dir = tempfile::tempdir().expect("create temp directory");
    let harness = fuchsia_component::client::connect_to_protocol::<HarnessMarker>()
        .expect("connecting to HarnessProxy");

    let mut test_params = new_test_params(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/stdout_ansi_test.cm",
        harness,
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

    let expected_test_run = ExpectedTestRun::new(directory::Outcome::Passed);
    let expected_test_suites = vec![ExpectedSuite::new(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/stdout_ansi_test.cm",
        directory::Outcome::Passed,
    )
    .with_case(
        ExpectedTestCase::new("stdout_ansi_test", directory::Outcome::Passed)
            .with_artifact("stdout", "\u{1b}[31mred stdout\u{1b}[0m\n"),
    )];

    let (run_result, suite_results) = directory::testing::parse_json_in_output(output_dir.path());

    directory::testing::assert_run_result(output_dir.path(), &run_result, &expected_test_run);

    directory::testing::assert_suite_results(
        output_dir.path(),
        &suite_results,
        &expected_test_suites,
    );
}
