// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::test::types::{
        StepResult, StepResultItem, TestPlan, TestPlanTest, TestResult, TestResultItem, TestResults,
    },
    failure::ResultExt,
    fidl_fuchsia_sys,
    fidl_fuchsia_test::{
        Invocation,
        RunListenerRequest::{OnTestCaseFinished, OnTestCaseStarted},
    },
    fuchsia_async as fasync,
    fuchsia_syslog::macros::*,
    fuchsia_zircon as zx,
    futures::{
        future::BoxFuture,
        io::{self, AsyncRead},
        prelude::*,
        ready,
        task::{Context, Poll},
    },
    serde_json::Value,
    std::{cell::RefCell, collections::HashMap, fmt, fs::File, io::Write, marker::Unpin, pin::Pin},
    uuid::Uuid,
    zx::HandleBased,
};

#[must_use = "futures/streams"]
pub struct LoggerStream {
    socket: fasync::Socket,
}
impl Unpin for LoggerStream {}

thread_local! {
    pub static BUFFER:
        RefCell<[u8; 256]> = RefCell::new([0; 256]);
}

impl LoggerStream {
    /// Creates a new `LoggerStream` for given `socket`.
    pub fn new(socket: zx::Socket) -> Result<LoggerStream, failure::Error> {
        let l = LoggerStream {
            socket: fasync::Socket::from_socket(socket).context("Invalid zircon socket")?,
        };
        Ok(l)
    }
}

fn process_log_bytes(bytes: &[u8]) -> String {
    // TODO(anmittal): Change this to consider break in logs and handle it.
    let log = std::str::from_utf8(bytes).unwrap();
    log.to_string()
}

impl Stream for LoggerStream {
    type Item = io::Result<String>;

    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        BUFFER.with(|b| {
            let mut b = b.borrow_mut();
            let len = ready!(Pin::new(&mut self.socket).poll_read(cx, &mut *b)?);
            if len == 0 {
                return Poll::Ready(None);
            }
            Poll::Ready(Some(process_log_bytes(&b[0..len])).map(Ok))
        })
    }
}

struct Step {
    pub result: StepResult,
    f: Option<BoxFuture<'static, io::Result<String>>>,
}

impl Step {
    pub fn default() -> Step {
        Step { result: StepResult::default(), f: None }
    }

    pub fn start_log_collection(&mut self, logger_socket: zx::Socket) {
        if logger_socket.is_invalid_handle() {
            return;
        }

        let ls = match LoggerStream::new(logger_socket) {
            Err(e) => {
                fx_log_err!("Logger: Failed to create fuchsia async socket: {:?}", e);
                return;
            }
            Ok(ls) => ls,
        };

        let (remote, remote_handle) = ls.try_collect::<String>().remote_handle();

        // spawn so that log collection can run in background.
        fasync::spawn(remote);

        self.f = Some(remote_handle.boxed());
    }

    pub async fn await_logs(&mut self) -> Result<String, failure::Error> {
        if let Some(ref mut f) = self.f.take().as_mut() {
            return Ok(f.await?);
        }
        Ok("".to_string())
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
                Some(vec![url.clone()]),
            )
            .map_err(|e| format_err!("Not able to launch v2 test:{}: {}", url, e))?;
        } else {
            app = fuchsia_component::client::launch(&launcher, url.clone(), None)
                .map_err(|e| format_err!("Not able to launch v1 test:{}: {}", url, e))?;
        }

        fx_log_info!("connecting to test service");
        let suite = app
            .connect_to_service::<fidl_fuchsia_test::SuiteMarker>()
            .map_err(|e| format_err!("Error connecting to test service: {}", e))?;
        fx_log_info!("enumerating tests");
        let cases =
            suite.get_tests().await.map_err(|e| format_err!("Error getting test steps: {}", e))?;
        fx_log_info!("got test list: {:#?}", cases);
        let mut invocations = Vec::<Invocation>::new();
        for case in cases {
            invocations.push(Invocation { case: Some(case) });
        }
        let (run_listener_client, mut run_listener) =
            fidl::endpoints::create_request_stream::<fidl_fuchsia_test::RunListenerMarker>()
                .map_err(|e| format_err!("Error creating request stream: {}", e))?;
        fx_log_info!("running tests");
        suite
            .run(
                &mut invocations.into_iter().map(|i| i.into()),
                fidl_fuchsia_test::RunOptions {},
                run_listener_client,
            )
            .map_err(|e| format_err!("Error running tests in '{}': {}", url, e))?;

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

        let mut current_step_map = HashMap::new();
        while let Some(result_event) = run_listener
            .try_next()
            .await
            .map_err(|e| format_err!("Error waiting for listener: {}", e))?
        {
            match result_event {
                OnTestCaseStarted { name, primary_log, control_handle: _ } => {
                    let mut step = Step {
                        result: StepResult { name: name.clone(), ..StepResult::default() },
                        ..Step::default()
                    };

                    step.start_log_collection(primary_log);
                    current_step_map.insert(name, step);
                }
                OnTestCaseFinished { name, outcome, control_handle: _ } => {
                    match current_step_map.get_mut(&name) {
                        Some(step) => {
                            step.result.outcome = match outcome.status {
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
            let logs = step.await_logs().await?;

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

        app.kill().map_err(|e| format_err!("Error killing test '{}': {}", url, e))?;

        let mut test_result = TestResult::default();
        test_result.outcome = test_outcome.to_string();

        test_result.steps = step_results;

        Ok(test_result)
    }
}
