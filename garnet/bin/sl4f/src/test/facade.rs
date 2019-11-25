// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::test::types::{
        StepResult, StepResultItem, TestPlan, TestPlanTest, TestResult, TestResultItem, TestResults,
    },
    fidl_fuchsia_sys, fuchsia_async as fasync,
    fuchsia_syslog::macros::*,
    futures::{channel::mpsc, prelude::*},
    serde_json::Value,
    std::{collections::HashMap, fmt, fs::File, io::Write},
    test_executor::TestEvent,
    uuid::Uuid,
};

struct Step {
    pub result: StepResult,
    logs: Option<String>,
}

impl Step {
    pub fn default() -> Step {
        Step { result: StepResult::default(), logs: None }
    }

    pub fn add_log(&mut self, log: String) {
        match &mut self.logs {
            Some(s) => s.push_str(&log),
            None => self.logs = Some(log),
        }
    }

    pub fn take_logs(&mut self) -> String {
        self.logs.take().unwrap_or_else(|| "".to_string())
    }
}

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
        serde_json::to_value(results).map_err(|e| format_err!("Not able to format results: {}", e))
    }

    pub async fn run_test(&self, url: String) -> Result<Value, failure::Error> {
        let test_results = self.run_test_component(url).await?;
        serde_json::to_value(test_results)
            .map_err(|e| format_err!("Not able to format test results: {}", e))
    }

    async fn run_test_component(&self, url: String) -> Result<TestResult, failure::Error> {
        let launcher = match fuchsia_component::client::connect_to_service::<
            fidl_fuchsia_sys::LauncherMarker,
        >() {
            Ok(l) => l,
            Err(e) => return Err(e),
        };

        let (sender, mut recv) = mpsc::channel(1);

        let (remote, test_fut) =
            test_executor::run_test_component(launcher, url.clone(), sender).remote_handle();

        fasync::spawn(remote);

        #[derive(PartialEq)]
        enum TestOutcome {
            Passed,
            Failed,
            Inconclusive,
            Error,
        };

        impl fmt::Display for TestOutcome {
            fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
                match self {
                    TestOutcome::Passed => write!(f, "passed"),
                    TestOutcome::Failed => write!(f, "failed"),
                    TestOutcome::Inconclusive => write!(f, "inconclusive"),
                    TestOutcome::Error => write!(f, "error"),
                }
            }
        }

        let mut test_outcome = TestOutcome::Passed;

        let mut current_step_map = HashMap::new();

        while let Some(test_event) = recv.next().await {
            match test_event {
                TestEvent::TestCaseStarted { test_case_name } => {
                    let step = Step {
                        result: StepResult {
                            name: test_case_name.clone(),
                            ..StepResult::default()
                        },
                        ..Step::default()
                    };

                    current_step_map.insert(test_case_name, step);
                }
                TestEvent::TestCaseFinished { test_case_name, outcome } => {
                    match current_step_map.get_mut(&test_case_name) {
                        Some(step) => {
                            step.result.outcome = match outcome {
                                test_executor::Outcome::Passed => "passed".to_string(),
                                test_executor::Outcome::Failed => {
                                    if test_outcome == TestOutcome::Passed {
                                        test_outcome = TestOutcome::Failed;
                                    }
                                    "failed".to_string()
                                }
                                test_executor::Outcome::Error => {
                                    test_outcome = TestOutcome::Error;
                                    "error".to_string()
                                }
                            };
                        }
                        None => {
                            return Err(format_err!("test case: '{}' not found", test_case_name));
                        }
                    }
                }
                TestEvent::LogMessage { test_case_name, msg } => {
                    match current_step_map.get_mut(&test_case_name) {
                        Some(step) => {
                            step.add_log(msg);
                        }
                        None => {
                            return Err(format_err!("test case: '{}' not found", test_case_name));
                        }
                    }
                }
            }
        }

        test_fut.await.map_err(|e| format_err!("Error running test: {}", e))?;

        let mut step_results = Vec::<StepResultItem>::new();

        for (_, mut step) in current_step_map {
            if step.result.outcome == "".to_string() {
                // step not completed, test might have crashed.
                match test_outcome {
                    TestOutcome::Passed | TestOutcome::Failed => {
                        test_outcome = TestOutcome::Inconclusive;
                    }
                    _ => {}
                }
                step.result.outcome = "inconclusive".to_string();
            }
            let logs = step.take_logs();

            if logs.len() > 0 {
                // TODO(anmittal): Display log until host can pull logs and display it.
                fx_log_info!("logs: {}", logs);

                let filename = format!("/data/{}", &Uuid::new_v4().to_string());
                let mut file = File::create(&filename)?;
                write!(file, "{}", logs)?;
                step.result.primary_log_path = filename.clone();
            }

            step_results.push(StepResultItem::Result(step.result));
        }

        let mut test_result = TestResult::default();
        test_result.outcome = test_outcome.to_string();

        test_result.steps = step_results;

        Ok(test_result)
    }
}
