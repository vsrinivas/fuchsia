// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_test_manager::{HarnessMarker, HarnessProxy};
use run_test_suite_lib::{run_test, Outcome, TestParams};
use std::str::from_utf8;

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
            .filter(|x| !x.starts_with("[output - "))
            .collect::<Vec<_>>();

        expected_output.sort();
        output.sort();

        assert_eq!(output, expected_output);
    };
}

fn new_test_params(test_url: &str, harness: HarnessProxy) -> TestParams {
    TestParams {
        test_url: test_url.to_string(),
        harness: harness,
        timeout: None,
        test_filter: None,
        also_run_disabled_tests: false,
        test_args: None,
        parallel: None,
    }
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_no_clean_exit() {
    let mut output: Vec<u8> = vec![];
    let harness = fuchsia_component::client::connect_to_service::<HarnessMarker>()
        .expect("connecting to HarnessProxy");
    let run_result = run_test(
        new_test_params(
            "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/no-onfinished-after-test-example.cm", 
            harness),
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

    assert_eq!(run_result.outcome, Outcome::Passed);
    assert_eq!(run_result.executed, run_result.passed);

    let expected = vec!["Example.Test1", "Example.Test2", "Example.Test3"];

    assert_eq!(run_result.executed, expected);
    assert!(!run_result.successful_completion);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_passing_v2_test() {
    let mut output: Vec<u8> = vec![];
    let harness = fuchsia_component::client::connect_to_service::<HarnessMarker>()
        .expect("connecting to HarnessProxy");

    let run_result = run_test(
            new_test_params(
                "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/passing-test-example_v2.cm",
                harness),
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

    assert_eq!(run_result.outcome, Outcome::Passed);
    assert_eq!(run_result.executed, run_result.passed);

    let expected = vec!["Example.Test1", "Example.Test2", "Example.Test3"];

    assert_eq!(run_result.executed, expected);
    assert!(run_result.successful_completion);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_with_filter() {
    let mut output: Vec<u8> = vec![];
    let harness = fuchsia_component::client::connect_to_service::<HarnessMarker>()
        .expect("connecting to HarnessProxy");
    let mut test_params = new_test_params(
            "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/passing-test-example_v2.cm",
            harness);

    test_params.test_filter = Some("*Test3".to_string());
    let run_result =
        run_test(test_params, &mut output).await.expect("Running test should not fail");

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
async fn launch_and_test_empty_test() {
    let mut output: Vec<u8> = vec![];
    let harness = fuchsia_component::client::connect_to_service::<HarnessMarker>()
        .expect("connecting to HarnessProxy");

    let run_result = run_test(
        new_test_params(
            "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/no-test-example.cm",
            harness,
        ),
        &mut output,
    )
    .await
    .expect("Running test should not fail");

    assert_eq!(run_result.executed.len(), 0);
    assert_eq!(run_result.passed.len(), 0);
    assert!(run_result.successful_completion);
}

#[fuchsia_async::run_singlethreaded(test)]
#[ignore = "fxbug.dev/47166: test is timing out"]
async fn launch_and_test_huge_test() {
    let mut output: Vec<u8> = vec![];
    let harness = fuchsia_component::client::connect_to_service::<HarnessMarker>()
        .expect("connecting to HarnessProxy");

    let run_result = run_test(
        new_test_params(
            "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/huge-test-example.cm",
            harness,
        ),
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
    let harness = fuchsia_component::client::connect_to_service::<HarnessMarker>()
        .expect("connecting to HarnessProxy");

    let run_result = run_test(
            new_test_params(
                "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/disabled-test-example.cm",
                harness,
            ),
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
    let harness = fuchsia_component::client::connect_to_service::<HarnessMarker>()
        .expect("connecting to HarnessProxy");
    let mut test_params = new_test_params(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/disabled-test-example.cm",
        harness,
    );
    test_params.also_run_disabled_tests = true;
    let run_result =
        run_test(test_params, &mut output).await.expect("Running test should not fail");

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
    let harness = fuchsia_component::client::connect_to_service::<HarnessMarker>()
        .expect("connecting to HarnessProxy");

    let run_result = run_test(
            new_test_params(
                "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/failing-test-example.cm",
                harness,
            ),
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
async fn launch_and_test_incomplete_test() {
    let mut output: Vec<u8> = vec![];
    let harness = fuchsia_component::client::connect_to_service::<HarnessMarker>()
        .expect("connecting to HarnessProxy");

    let run_result = run_test(
            new_test_params(
                "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/incomplete-test-example.cm",
                harness,
            ),
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
    let harness = fuchsia_component::client::connect_to_service::<HarnessMarker>()
        .expect("connecting to HarnessProxy");

    let run_result = run_test(
            new_test_params(
                "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/invalid-test-example.cm",
                harness,
            ),
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

    assert_eq!(run_result.outcome, Outcome::Error);

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
    let harness = fuchsia_component::client::connect_to_service::<HarnessMarker>()
        .expect("connecting to HarnessProxy");

    let run_result = run_test(
        new_test_params(
            "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/echo_test_realm.cm",
            harness,
        ),
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
    let harness = fuchsia_component::client::connect_to_service::<HarnessMarker>()
        .expect("connecting to HarnessProxy");

    let mut test_params = new_test_params(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/long_running_test.cm",
        harness,
    );
    test_params.timeout = std::num::NonZeroU32::new(1);
    let run_result =
        run_test(test_params, &mut output).await.expect("Running test should not fail");

    assert_eq!(run_result.outcome, Outcome::Timedout);

    assert_eq!(run_result.passed, Vec::<String>::new());
    assert!(!run_result.successful_completion);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_passes_with_large_timeout() {
    let mut output: Vec<u8> = vec![];
    let harness = fuchsia_component::client::connect_to_service::<HarnessMarker>()
        .expect("connecting to HarnessProxy");

    let mut test_params = new_test_params(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/echo_test_realm.cm",
        harness,
    );
    test_params.timeout = std::num::NonZeroU32::new(600);
    let run_result =
        run_test(test_params, &mut output).await.expect("Running test should not fail");

    let expected_output = "[RUNNING]	EchoTest
[PASSED]	EchoTest
";
    assert_output!(output, expected_output);

    assert_eq!(run_result.outcome, Outcome::Passed);

    assert_eq!(run_result.executed, vec!["EchoTest"]);
    assert_eq!(run_result.passed, vec!["EchoTest"]);
    assert!(run_result.successful_completion);
}
