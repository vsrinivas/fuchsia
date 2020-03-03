// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use regex::Regex;
use run_test_suite_lib::{run_test, Outcome};
use std::str::from_utf8;

// This test also acts as an example on how to write a v2 test.
// This will launch a echo_realm which will inject echo_server, launch v2 test which will
// then test that server out and return back results.
#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_run_echo_test() {
    let mut output: Vec<u8> = vec![];
    let run_result = run_test(
        "fuchsia-pkg://fuchsia.com/gtest_adapter_echo_example#meta/echo_test_realm.cm".to_string(),
        &mut output,
    )
    .await
    .expect("Running test should not fail");

    let expected_output = "[RUNNING]\tEchoTest.TestEcho\n[PASSED]\tEchoTest.TestEcho\n";

    assert_eq!(from_utf8(&output), Ok(expected_output));

    assert_eq!(run_result.outcome, Outcome::Passed);

    assert_eq!(run_result.executed, vec!["EchoTest.TestEcho"]);
    assert_eq!(run_result.passed, vec!["EchoTest.TestEcho"]);
    assert!(run_result.successful_completion)
}

// This test also acts as an example on how to write a v2 test.
// This will launch a unit test which doesn't depend on any services(injected or otherwise).
#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_run_simple_test() {
    let mut output: Vec<u8> = vec![];
    let run_result = run_test(
        "fuchsia-pkg://fuchsia.com/simple_gtest_adapter_example#meta/simple_gtest_adapter_example.cm".to_string(),
        &mut output,
    )
    .await
    .expect("Running test should not fail");

    let expected_output = "[RUNNING]\tSimpleTest.Test\n[PASSED]\tSimpleTest.Test\n";

    assert_eq!(from_utf8(&output), Ok(expected_output));

    assert_eq!(run_result.outcome, Outcome::Passed);

    assert_eq!(run_result.executed, vec!["SimpleTest.Test"]);
    assert_eq!(run_result.passed, vec!["SimpleTest.Test"]);
    assert!(run_result.successful_completion)
}

// test that we are able to handle both passing and failing tests.
#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_run_sample_test() {
    let mut output: Vec<u8> = vec![];
    let run_result = run_test(
        "fuchsia-pkg://fuchsia.com/gtest_adapter_integration_test#meta/sample_tests.cm".to_string(),
        &mut output,
    )
    .await
    .expect("Running test should not fail");

    let expected_output = "\\[RUNNING\\]\tSampleTest.Passing
\\[PASSED\\]\tSampleTest.Passing
\\[RUNNING\\]\tSampleTest.Failing
\\[SampleTest.Failing\\]\tfailure: .*sample_tests.cc:.*
\\[SampleTest.Failing\\]\tValue of: false
\\[SampleTest.Failing\\]\t  Actual: false
\\[SampleTest.Failing\\]\tExpected: true
\\[FAILED\\]\tSampleTest.Failing
";

    let expected_output_re = Regex::new(expected_output).unwrap();
    let output = from_utf8(&output).expect("should not fail");

    assert!(
        expected_output_re.is_match(&output),
        "expected re:\n{}\ngot:\n{}",
        expected_output,
        output
    );

    assert_eq!(run_result.outcome, Outcome::Failed);

    assert_eq!(run_result.executed, vec!["SampleTest.Failing", "SampleTest.Passing"]);
    assert_eq!(run_result.passed, vec!["SampleTest.Passing"]);
    assert!(run_result.successful_completion)
}

// Stress test with a very large gtest suite.
#[fuchsia_async::run_singlethreaded(test)]
#[ignore]
async fn launch_and_run_huge_test() {
    let mut output: Vec<u8> = vec![];
    let run_result = run_test(
        "fuchsia-pkg://fuchsia.com/huge_gtest_adapter_example#meta/huge_gtest_adapter_example.cm"
            .to_string(),
        &mut output,
    )
    .await
    .expect("Running test should not fail");

    assert_eq!(run_result.outcome, Outcome::Passed);
    assert_eq!(run_result.executed.len(), 1_000);
    assert_eq!(run_result.passed.len(), 1_000);
    assert!(run_result.successful_completion);
}
