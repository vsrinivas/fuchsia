// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::test::types::{
        StepResult, StepResultItem, TestPlan, TestPlanTest, TestResult, TestResultItem, TestResults,
    },
    fidl_fuchsia_sys,
    fidl_fuchsia_test::{
        Invocation,
        RunListenerRequest::{OnTestCaseFinished, OnTestCaseStarted},
    },
    fuchsia_syslog::macros::*,
    futures::TryStreamExt,
    serde_json::Value,
    std::collections::HashMap,
    std::fmt,
};

#[derive(Debug)]
pub struct TestFacade {}

impl TestFacade {
    pub fn new() -> TestFacade {
        TestFacade {}
    }

    pub async fn run_plan(&self, plan: TestPlan) -> Result<Value, failure::Error> {
        let mut results = TestResults::default();
        for test in plan.tests.iter() {
            match test {
                TestPlanTest::ComponentUrl(c) => {
                    let result = self.run_test_component(c.to_string()).await?;
                    results.results.push(TestResultItem::Result(result));
                }
            }
        }
        Ok(serde_json::to_value(results)?)
    }

    pub async fn run_test(&self, url: String) -> Result<Value, failure::Error> {
        let test_results = self.run_test_component(url).await?;
        Ok(serde_json::to_value(test_results)?)
    }

    async fn run_test_component(&self, url: String) -> Result<TestResult, failure::Error> {
        let component_manager_for_test = "fuchsia-pkg://fuchsia.com/component_manager_for_test#\
                                          meta/component_manager_for_test.cmx";
        let launcher = match fuchsia_component::client::connect_to_service::<
            fidl_fuchsia_sys::LauncherMarker,
        >() {
            Ok(l) => l,
            Err(e) => return Err(e),
        };
        fx_log_info!("connecting to test component {}", url);
        let is_v2_component = url.ends_with(".cm");
        let mut app;
        if is_v2_component {
            app = fuchsia_component::client::launch(
                &launcher,
                component_manager_for_test.to_string(),
                Some(vec![url]),
            )?;
        } else {
            app = fuchsia_component::client::launch(&launcher, url, None)?;
        }

        fx_log_info!("connecting to test service");
        let suite = app.connect_to_service::<fidl_fuchsia_test::SuiteMarker>()?;
        fx_log_info!("enumerating tests");
        let cases = suite.get_tests().await?;
        fx_log_info!("got test list: {:#?}", cases);
        let mut invocations = Vec::<Invocation>::new();
        for case in cases {
            invocations.push(Invocation { case: Some(case) });
        }
        let (run_listener_client, mut run_listener) =
            fidl::endpoints::create_request_stream::<fidl_fuchsia_test::RunListenerMarker>()?;
        fx_log_info!("running tests");
        suite.run(
            &mut invocations.into_iter().map(|i| i.into()),
            fidl_fuchsia_test::RunOptions {},
            run_listener_client,
        )?;

        #[derive(PartialEq)]
        enum TestOutcome {
            Passed,
            Failed,
            Inconclusive,
            Error,
        };

        impl fmt::Display for TestOutcome {
            fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
                match self {
                    TestOutcome::Passed => write!(f, "passed"),
                    TestOutcome::Failed => write!(f, "failed"),
                    TestOutcome::Inconclusive => write!(f, "inconclusive"),
                    TestOutcome::Error => write!(f, "error"),
                }
            }
        }

        let mut test_outcome = TestOutcome::Passed;

        let mut steps = Vec::<StepResultItem>::new();
        let mut current_step_map = HashMap::new();
        // TODO(anmittal): Use this to extract logs and show to user.
        // To store logger socket so that other end doesn't receive a PEER CLOSED error.
        let mut _logger = fuchsia_zircon::Socket::from(fuchsia_zircon::Handle::invalid());
        while let Some(result_event) = run_listener.try_next().await? {
            match result_event {
                OnTestCaseStarted { name, primary_log, control_handle: _ } => {
                    let step = StepResult { name: name.clone(), ..StepResult::default() };
                    current_step_map.insert(name, step);
                    _logger = primary_log;
                }
                OnTestCaseFinished { name, outcome, control_handle: _ } => {
                    match current_step_map.get_mut(&name) {
                        Some(step) => {
                            step.outcome = match outcome.status {
                                Some(status) => match status {
                                    fidl_fuchsia_test::Status::Passed => "passed".to_string(),
                                    fidl_fuchsia_test::Status::Failed => {
                                        if test_outcome == TestOutcome::Passed {
                                            test_outcome = TestOutcome::Failed;
                                        }
                                        "failed".to_string()
                                    }
                                },
                                // This will happen when test protocol is not properly implemented
                                // by the test and it forgets to set the outcome.
                                None => {
                                    test_outcome = TestOutcome::Error;
                                    "error".to_string()
                                }
                            };
                        }
                        None => {
                            return Err(format_err!("test case: '{}' not found", name));
                        }
                    }
                }
            }
        }

        for (_, mut step) in current_step_map {
            if step.outcome == "".to_string() {
                // step not completed, test might have crashed.
                match test_outcome {
                    TestOutcome::Passed | TestOutcome::Failed => {
                        test_outcome = TestOutcome::Inconclusive;
                    }
                    _ => {}
                }
                step.outcome = "inconclusive".to_string();
            }
            steps.push(StepResultItem::Result(step));
        }

        app.kill()?;

        let mut test_result = TestResult::default();
        test_result.outcome = test_outcome.to_string();

        test_result.steps = steps;

        Ok(test_result)
    }
}
