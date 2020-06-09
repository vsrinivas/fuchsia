// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use sl4f_lib::test::facade::TestFacade;
use sl4f_lib::test::types::{TestPlan, TestPlanTest};
use std::fs;

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_passing_test() {
    let test_facade = TestFacade::new();
    let test_result = test_facade
        .run_test(
            "fuchsia-pkg://fuchsia.com/sl4f_test_integration_tests#meta/passing-test-example.cmx"
                .to_string(),
        )
        .await
        .expect("Running test should not fail");

    assert_eq!(test_result["result"].as_str().unwrap(), "passed");
    let steps = test_result["steps"].as_array().expect("test result should contain step");
    assert!(steps.len() > 0, "steps_len = {}", steps.len());
    for step in steps.iter() {
        assert_eq!(step["status"].as_str().unwrap(), "passed", "for step: {:#?}", step);

        // check logs
        let log_file_name = step["primary_log_path"].as_str().expect("can't get log file name");
        assert_ne!(log_file_name, "");
        let contents =
            fs::read_to_string(log_file_name).expect("Something went wrong reading the logs");
        let mut expected = "".to_string();
        for i in 1..4 {
            expected = format!("{}log{} for {}\n", expected, i, step["name"].as_str().unwrap());
        }
        assert_eq!(contents, expected);
    }
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_passing_v2_test() {
    let test_facade = TestFacade::new();
    let test_result = test_facade
        .run_test(
            "fuchsia-pkg://fuchsia.com/sl4f_test_integration_tests#meta/passing-test-example.cm"
                .to_string(),
        )
        .await
        .expect("Running test should not fail");

    assert_eq!(test_result["result"].as_str().unwrap(), "passed");
    let steps = test_result["steps"].as_array().expect("test result should contain step");
    assert!(steps.len() > 0, "steps_len = {}", steps.len());
    for step in steps.iter() {
        assert_eq!(step["status"].as_str().unwrap(), "passed", "for step: {:#?}", step);
    }
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_with_filter() {
    let test_facade = TestFacade::new();
    let test_result = test_facade
        .run_test_with_filter(
            "fuchsia-pkg://fuchsia.com/sl4f_test_integration_tests#meta/passing-test-example.cm"
                .to_string(),
            "*Test2".to_string(),
        )
        .await
        .expect("Running test should not fail");

    assert_eq!(test_result["result"].as_str().unwrap(), "passed");
    let steps = test_result["steps"].as_array().expect("test result should contain step");
    assert_eq!(steps.len(), 1);
    assert_eq!(steps[0]["name"].as_str().unwrap(), "Example.Test2");
    assert_eq!(steps[0]["status"].as_str().unwrap(), "passed");
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_with_filter_that_matches_multiple_but_not_all_tests() {
    let test_facade = TestFacade::new();
    let test_result = test_facade
        .run_test_with_filter(
            "fuchsia-pkg://fuchsia.com/sl4f_test_integration_tests#meta/passing-test-example.cm"
                .to_string(),
            "Example.Test[32]".to_string(),
        )
        .await
        .expect("Running test should not fail");

    assert_eq!(test_result["result"].as_str().unwrap(), "passed");
    let steps = test_result["steps"].as_array().expect("test result should contain step");
    assert_eq!(steps.len(), 2);
    let mut names = vec!["Example.Test2", "Example.Test3"];
    for step in steps {
        assert_eq!(step["status"].as_str().unwrap(), "passed");
        names.remove(names.iter().position(|n| *n == step["name"].as_str().unwrap()).unwrap());
    }
    assert_eq!(names.len(), 0);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_with_filter_that_matches_all_tests() {
    let test_facade = TestFacade::new();
    let test_result = test_facade
        .run_test_with_filter(
            "fuchsia-pkg://fuchsia.com/sl4f_test_integration_tests#meta/passing-test-example.cm"
                .to_string(),
            "*".to_string(),
        )
        .await
        .expect("Running test should not fail");

    assert_eq!(test_result["result"].as_str().unwrap(), "passed");
    let steps = test_result["steps"].as_array().expect("test result should contain step");
    assert_eq!(steps.len(), 3);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_with_filter_that_matches_no_tests() {
    let test_facade = TestFacade::new();
    let test_result = test_facade
        .run_test_with_filter(
            "fuchsia-pkg://fuchsia.com/sl4f_test_integration_tests#meta/passing-test-example.cm"
                .to_string(),
            "NonExistentTest".to_string(),
        )
        .await
        .expect("Running test should not fail");

    assert_eq!(test_result["result"].as_str().unwrap(), "passed");
    let steps = test_result["steps"].as_array().expect("test result should contain step");
    assert_eq!(steps.len(), 0);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_failing_test() {
    let test_facade = TestFacade::new();
    let test_result = test_facade
        .run_test(
            "fuchsia-pkg://fuchsia.com/sl4f_test_integration_tests#meta/failing-test-example.cm"
                .to_string(),
        )
        .await
        .expect("Running test should not fail");

    assert_eq!(test_result["result"].as_str().unwrap(), "failed");
    let steps = test_result["steps"].as_array().expect("test result should contain step");
    assert!(steps.len() > 0, "steps_len = {}", steps.len());

    assert!(steps.iter().find(|s| s["status"].as_str().unwrap() == "failed").is_some());
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_incomplete_test() {
    let test_facade = TestFacade::new();
    let test_result = test_facade
        .run_test(
            "fuchsia-pkg://fuchsia.com/sl4f_test_integration_tests#meta/incomplete-test-example.cm"
                .to_string(),
        )
        .await
        .expect("Running test should not fail");

    assert_eq!(test_result["result"].as_str().unwrap(), "inconclusive");
    let steps = test_result["steps"].as_array().expect("test result should contain step");
    assert!(steps.len() > 0, "steps_len = {}", steps.len());

    assert_eq!(steps.iter().filter(|s| s["status"].as_str().unwrap() == "inconclusive").count(), 2);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_invalid_test() {
    let test_facade = TestFacade::new();
    let test_result = test_facade
        .run_test(
            "fuchsia-pkg://fuchsia.com/sl4f_test_integration_tests#meta/invalid-test-example.cm"
                .to_string(),
        )
        .await
        .expect("Running test should not fail");

    assert_eq!(test_result["result"].as_str().unwrap(), "error");
    let steps = test_result["steps"].as_array().expect("test result should contain step");
    assert!(steps.len() > 0, "steps_len = {}", steps.len());

    assert_eq!(steps.iter().filter(|s| s["status"].as_str().unwrap() == "error").count(), 2);
}

fn get_result(result: &serde_json::value::Value) -> &serde_json::value::Value {
    return result["Result"].get("result").unwrap();
}

#[fuchsia_async::run_singlethreaded(test)]
async fn run_a_test_plan() {
    let test_facade = TestFacade::new();
    let test_result = test_facade.run_plan(TestPlan {
        tests: vec![
            TestPlanTest::ComponentUrl(
                "fuchsia-pkg://fuchsia.com/sl4f_test_integration_tests#meta/passing-test-example.cmx".to_string()
            ),
            TestPlanTest::ComponentUrl(
                "fuchsia-pkg://fuchsia.com/sl4f_test_integration_tests#meta/failing-test-example.cm".to_string()
            )
        ]
    }).await.expect("Running test should not fail");
    let results = test_result["results"].as_array().expect("test result should contain 'results'");
    assert_eq!(results.len(), 2);

    let mut iter = results.iter();
    let result = get_result(iter.next().unwrap());
    assert_eq!(result, "passed");
    let result = get_result(iter.next().unwrap());
    assert_eq!(result, "failed");
    assert!(iter.next().is_none());
}

// This test also acts an example on how to right a v2 test.
// This will launch a echo_realm which will inject echo_server, launch v2 test which will
// then test that server out and return back results.
#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_run_echo_test() {
    let test_facade = TestFacade::new();
    let test_result = test_facade
        .run_test("fuchsia-pkg://fuchsia.com/example-tests#meta/echo_test_realm.cm".to_string())
        .await
        .expect("Running test should not fail");

    assert_eq!(test_result["result"].as_str().unwrap(), "passed");
    let steps = test_result["steps"].as_array().expect("test result should contain step");
    assert_eq!(steps.len(), 1);
    for step in steps.iter() {
        assert_eq!(step["status"].as_str().unwrap(), "passed", "for step: {:#?}", step);
    }
}
