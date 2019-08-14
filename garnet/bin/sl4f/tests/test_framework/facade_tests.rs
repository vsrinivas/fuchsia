// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await)]

use sl4f_lib::test::facade::TestFacade;
use sl4f_lib::test::types::{TestPlan, TestPlanTest};

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

    assert_eq!(test_result["outcome"].as_str().unwrap(), "passed");
    let steps = test_result["steps"].as_array().expect("test result should contain step");
    assert!(steps.len() > 0, "steps_len = {}", steps.len());
    for step in steps.iter() {
        assert_eq!(step["outcome"].as_str().unwrap(), "passed", "for step: {:#?}", step);
    }
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_failing_test() {
    let test_facade = TestFacade::new();
    let test_result = test_facade
        .run_test(
            "fuchsia-pkg://fuchsia.com/sl4f_test_integration_tests#meta/failing-test-example.cmx"
                .to_string(),
        )
        .await
        .expect("Running test should not fail");

    assert_eq!(test_result["outcome"].as_str().unwrap(), "failed");
    let steps = test_result["steps"].as_array().expect("test result should contain step");
    assert!(steps.len() > 0, "steps_len = {}", steps.len());

    assert!(steps.iter().find(|s| s["outcome"].as_str().unwrap() == "failed").is_some());
}

fn get_outcome(result: &serde_json::value::Value) -> &serde_json::value::Value {
    return result["Result"].get("outcome").unwrap();
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
                "fuchsia-pkg://fuchsia.com/sl4f_test_integration_tests#meta/failing-test-example.cmx".to_string()
            )
        ]
    }).await.expect("Running test should not fail");
    let results = test_result["results"].as_array().expect("test result should contain 'results'");
    assert_eq!(results.len(), 2);

    let mut iter = results.iter();
    let outcome = get_outcome(iter.next().unwrap());
    assert_eq!(outcome, "passed");
    let outcome = get_outcome(iter.next().unwrap());
    assert_eq!(outcome, "failed");
    assert!(iter.next().is_none());
}
