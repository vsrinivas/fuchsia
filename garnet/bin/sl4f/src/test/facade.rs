// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::test::types::{
        StepResult, StepResultItem, TestPlan, TestPlanTest, TestResult, TestResultItem, TestResults,
    },
    anyhow::format_err,
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

    pub async fn run_plan(&self, plan: TestPlan) -> Result<Value, anyhow::Error> {
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

    pub async fn run_test(&self, url: String) -> Result<Value, anyhow::Error> {
        let test_results = self.run_test_component(url).await?;
        serde_json::to_value(test_results)
            .map_err(|e| format_err!("Not able to format test results: {}", e))
    }

    async fn run_test_component(&self, url: String) -> Result<TestResult, anyhow::Error> {
        let (sender, mut recv) = mpsc::channel(1);

        let test_fut = match url.ends_with("cm") {
            true => {
                let harness = fuchsia_component::client::connect_to_service::<
                    fidl_fuchsia_test_manager::HarnessMarker,
                >()?;
                let (remote, test_fut) =
                    test_executor::run_v2_test_component(harness, url.clone(), sender)
                        .remote_handle();
                fasync::spawn(remote);
                test_fut
            }
            false => {
                let launcher = fuchsia_component::client::connect_to_service::<
                    fidl_fuchsia_sys::LauncherMarker,
                >()?;
                let (remote, test_fut) =
                    test_executor::run_v1_test_component(launcher, url.clone(), sender)
                        .remote_handle();
                fasync::spawn(remote);
                test_fut
            }
        };

        #[derive(PartialEq)]
        enum Status {
            Passed,
            Failed,
            Inconclusive,
            Error,
        };

        impl fmt::Display for Status {
            fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
                match self {
                    Status::Passed => write!(f, "passed"),
                    Status::Failed => write!(f, "failed"),
                    Status::Inconclusive => write!(f, "inconclusive"),
                    Status::Error => write!(f, "error"),
                }
            }
        }

        let mut status = Status::Passed;

        let mut current_step_map = HashMap::new();
        let mut test_result = TestResult::default();

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
                TestEvent::TestCaseFinished { test_case_name, result } => {
                    match current_step_map.get_mut(&test_case_name) {
                        Some(step) => {
                            step.result.status = match result {
                                test_executor::TestResult::Passed => "passed".to_string(),
                                test_executor::TestResult::Failed => {
                                    if status == Status::Passed {
                                        status = Status::Failed;
                                    }
                                    "failed".to_string()
                                }
                                test_executor::TestResult::Skipped => "skipped".to_string(),
                                test_executor::TestResult::Error => {
                                    status = Status::Error;
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
                TestEvent::Finish => {
                    test_result.successful_completion = true;
                    break;
                }
            }
        }

        test_fut.await.map_err(|e| format_err!("Error running test: {}", e))?;

        let mut step_results = Vec::<StepResultItem>::new();

        for (_, mut step) in current_step_map {
            if step.result.status == "".to_string() {
                // step not completed, test might have crashed.
                match status {
                    Status::Passed | Status::Failed => {
                        status = Status::Inconclusive;
                    }
                    _ => {}
                }
                step.result.status = "inconclusive".to_string();
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

        test_result.result = status.to_string();

        test_result.steps = step_results;

        Ok(test_result)
    }
}
