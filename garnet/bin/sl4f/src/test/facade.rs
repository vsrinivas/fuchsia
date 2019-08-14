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

    pub async fn run_test(&self, name: String) -> Result<Value, failure::Error> {
        let test_results = self.run_test_component(name).await?;
        Ok(serde_json::to_value(test_results)?)
    }

    async fn run_test_component(&self, name: String) -> Result<TestResult, failure::Error> {
        let launcher = match fuchsia_component::client::connect_to_service::<
            fidl_fuchsia_sys::LauncherMarker,
        >() {
            Ok(l) => l,
            Err(e) => return Err(e),
        };
        fx_log_info!("connecting to test component {}", name);
        let mut app = fuchsia_component::client::launch(&launcher, name, None)?;

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
        };

        impl fmt::Display for TestOutcome {
            fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
                match self {
                    TestOutcome::Passed => write!(f, "passed"),
                    TestOutcome::Failed => write!(f, "failed"),
                    TestOutcome::Inconclusive => write!(f, "inconclusive"),
                }
            }
        }

        let mut test_outcome = TestOutcome::Passed;

        let mut steps = Vec::<StepResultItem>::new();
        let mut current_step = StepResult::default();
        // TODO(anmittal): Use this to extract logs and show to user.
        // To store logger socket so that other end doesn't receive a PEER CLOSED error.
        let mut _logger = fuchsia_zircon::Socket::from(fuchsia_zircon::Handle::invalid());
        while let Some(result_event) = run_listener.try_next().await? {
            match result_event {
                OnTestCaseStarted { name, primary_log, control_handle: _ } => {
                    current_step = StepResult::default();
                    current_step.name = name;
                    _logger = primary_log;
                }
                OnTestCaseFinished { name: _, outcome, control_handle: _ } => {
                    current_step.outcome = match outcome.status {
                        Some(status) => match status {
                            fidl_fuchsia_test::Status::Passed => "passed".to_string(),
                            fidl_fuchsia_test::Status::Failed => {
                                if test_outcome == TestOutcome::Passed {
                                    test_outcome = TestOutcome::Failed;
                                }
                                "failed".to_string()
                            }
                        },
                        None => {
                            test_outcome = TestOutcome::Inconclusive;
                            "inconclusive".to_string()
                        }
                    };
                    steps.push(StepResultItem::Result(current_step));
                    current_step = StepResult::default();
                }
            }
        }

        app.kill()?;

        let mut test_result = TestResult::default();
        test_result.outcome = test_outcome.to_string();

        test_result.steps = steps;

        Ok(test_result)
    }
}
