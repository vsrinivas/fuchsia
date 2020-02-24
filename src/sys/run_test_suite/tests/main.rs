// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use run_test_suite_lib::{run_test, TestResult};
use std::str::from_utf8;

/// split and sort output as output can come in any order.
/// `output` is of type vec<u8> and `expected_output` is a string.
macro_rules! assert_output {
    ($output:expr, $expected_output:expr) => {
        let mut expected_output = $expected_output.split("\n").collect::<Vec<_>>();
        let mut output = from_utf8(&$output)
            .expect("we should not get utf8 error.")
            .split("\n")
            .collect::<Vec<_>>();

        expected_output.sort();
        output.sort();

        assert_eq!(output, expected_output);
    };
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_passing_test() {
    let mut output: Vec<u8> = vec![];
    let (result, executed, passed) = run_test(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/passing-test-example.cmx"
            .to_string(),
        &mut output,
    )
    .await
    .expect("Running test should not fail");

    let expected_output = "[RUNNING]	Example.Test1
[Example.Test1]	log1 for Example.Test1
[Example.Test1]	log2 for Example.Test1
[Example.Test1]	log3 for Example.Test1
[PASSED]	Example.Test1
[RUNNING]	Example.Test2
[Example.Test2]	log1 for Example.Test2
[Example.Test2]	log2 for Example.Test2
[Example.Test2]	log3 for Example.Test2
[PASSED]	Example.Test2
[RUNNING]	Example.Test3
[Example.Test3]	log1 for Example.Test3
[Example.Test3]	log2 for Example.Test3
[Example.Test3]	log3 for Example.Test3
[PASSED]	Example.Test3
";
    assert_output!(output, expected_output);

    assert_eq!(result, TestResult::Passed);
    assert_eq!(executed, passed);

    let expected = vec!["Example.Test1", "Example.Test2", "Example.Test3"];

    assert_eq!(executed, expected);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_passing_v2_test() {
    let mut output: Vec<u8> = vec![];
    let (result, executed, passed) = run_test(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/passing-test-example_v2.cm"
            .to_string(), &mut output
    )
    .await
    .expect("Running test should not fail");

    let expected_output = "[RUNNING]	Example.Test1
[Example.Test1]	log1 for Example.Test1
[Example.Test1]	log2 for Example.Test1
[Example.Test1]	log3 for Example.Test1
[PASSED]	Example.Test1
[RUNNING]	Example.Test2
[Example.Test2]	log1 for Example.Test2
[Example.Test2]	log2 for Example.Test2
[Example.Test2]	log3 for Example.Test2
[PASSED]	Example.Test2
[RUNNING]	Example.Test3
[Example.Test3]	log1 for Example.Test3
[Example.Test3]	log2 for Example.Test3
[Example.Test3]	log3 for Example.Test3
[PASSED]	Example.Test3
";
    assert_output!(output, expected_output);

    assert_eq!(result, TestResult::Passed);
    assert_eq!(executed, passed);

    let expected = vec!["Example.Test1", "Example.Test2", "Example.Test3"];

    assert_eq!(executed, expected);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_failing_test() {
    let mut output: Vec<u8> = vec![];
    let (result, executed, passed) = run_test(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/failing-test-example.cmx"
            .to_string(),
        &mut output,
    )
    .await
    .expect("Running test should not fail");

    let expected_output = "[RUNNING]	Example.Test1
[Example.Test1]	log1 for Example.Test1
[Example.Test1]	log2 for Example.Test1
[Example.Test1]	log3 for Example.Test1
[PASSED]	Example.Test1
[RUNNING]	Example.Test2
[Example.Test2]	log1 for Example.Test2
[Example.Test2]	log2 for Example.Test2
[Example.Test2]	log3 for Example.Test2
[FAILED]	Example.Test2
[RUNNING]	Example.Test3
[Example.Test3]	log1 for Example.Test3
[Example.Test3]	log2 for Example.Test3
[Example.Test3]	log3 for Example.Test3
[PASSED]	Example.Test3
";

    assert_output!(output, expected_output);

    assert_eq!(result, TestResult::Failed);

    assert_eq!(executed, vec!["Example.Test1", "Example.Test2", "Example.Test3"]);
    assert_eq!(passed, vec!["Example.Test1", "Example.Test3"]);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_incomplete_test() {
    let mut output: Vec<u8> = vec![];
    let (result, executed, passed) = run_test(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/incomplete-test-example.cmx"
            .to_string(),
            &mut output,
    )
    .await
    .expect("Running test should not fail");

    let expected_output = "[RUNNING]	Example.Test1
[RUNNING]	Example.Test2
[Example.Test1]	log1 for Example.Test1
[Example.Test1]	log2 for Example.Test1
[Example.Test1]	log3 for Example.Test1
[Example.Test2]	log1 for Example.Test2
[Example.Test2]	log2 for Example.Test2
[Example.Test2]	log3 for Example.Test2
[PASSED]	Example.Test2
[RUNNING]	Example.Test3
[Example.Test3]	log1 for Example.Test3
[Example.Test3]	log2 for Example.Test3
[Example.Test3]	log3 for Example.Test3

The following test(s) never completed:
Example.Test1
Example.Test3
";

    assert_output!(output, expected_output);

    assert_eq!(result, TestResult::Inconclusive);

    assert_eq!(executed, vec!["Example.Test1", "Example.Test2", "Example.Test3"]);
    assert_eq!(passed, vec!["Example.Test2"]);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_invalid_test() {
    let mut output: Vec<u8> = vec![];
    let (result, executed, passed) = run_test(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/invalid-test-example.cmx"
            .to_string(),
        &mut output,
    )
    .await
    .expect("Running test should not fail");

    let expected_output = "[RUNNING]	Example.Test1
[Example.Test1]	log1 for Example.Test1
[Example.Test1]	log2 for Example.Test1
[Example.Test1]	log3 for Example.Test1
[ERROR]	Example.Test1
[RUNNING]	Example.Test2
[Example.Test2]	log1 for Example.Test2
[Example.Test2]	log2 for Example.Test2
[Example.Test2]	log3 for Example.Test2
[PASSED]	Example.Test2
[RUNNING]	Example.Test3
[Example.Test3]	log1 for Example.Test3
[Example.Test3]	log2 for Example.Test3
[Example.Test3]	log3 for Example.Test3
[ERROR]	Example.Test3
";
    assert_output!(output, expected_output);

    assert_eq!(result, TestResult::Error);

    assert_eq!(executed, vec!["Example.Test1", "Example.Test2", "Example.Test3"]);
    assert_eq!(passed, vec!["Example.Test2"]);
}

// This test also acts an example on how to right a v2 test.
// This will launch a echo_realm which will inject echo_server, launch v2 test which will
// then test that server out and return back results.
#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_run_echo_test() {
    let mut output: Vec<u8> = vec![];
    let (result, executed, passed) = run_test(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/echo_test_realm.cm"
            .to_string(),
        &mut output,
    )
    .await
    .expect("Running test should not fail");

    let expected_output = "[RUNNING]	EchoTest
[PASSED]	EchoTest
";
    assert_output!(output, expected_output);

    assert_eq!(result, TestResult::Passed);

    assert_eq!(executed, vec!["EchoTest"]);
    assert_eq!(passed, vec!["EchoTest"]);
}
