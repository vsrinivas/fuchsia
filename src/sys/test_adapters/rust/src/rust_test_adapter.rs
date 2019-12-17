// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{format_err, Error, ResultExt},
    fidl_fuchsia_test as ftest,
    ftest::RunListenerProxy,
    fuchsia_async as fasync,
    fuchsia_syslog::{fx_log_err, fx_log_info},
    fuchsia_zircon as zx,
    futures::prelude::*,
    lazy_static::lazy_static,
    regex::Regex,
    serde_derive::Deserialize,
    serde_json as json,
    std::{
        ffi::{CStr, CString},
        str::from_utf8,
    },
};

lazy_static! {
    pub static ref DASH_Z: CString = CString::new("-Z").unwrap();
    pub static ref USTABLE_OPTIONS: CString = CString::new("unstable-options").unwrap();
    pub static ref LIST: CString = CString::new("--list").unwrap();
    pub static ref JSON_FORMAT: CString = CString::new("--format=json").unwrap();
    pub static ref NO_CAPTURE: CString = CString::new("--nocapture").unwrap();
}

/// Holds information about the test to be run
#[derive(Debug, PartialEq)]
pub struct TestInfo {
    pub test_path: String,
    pub test_args: Vec<String>,
}

/// Marks whether a test or suite just started (ResultEvent::Started), passed (ResultEvent::Ok), or
/// failed (ResultEvent::Failed).
#[derive(Debug, Deserialize)]
#[serde(rename_all = "lowercase")]
enum ResultEvent {
    Started,
    Failed,
    Ok,
}

/// Marks a message from the rust test as relating to a test suite or an individial test.
#[derive(Debug, Deserialize)]
#[serde(rename_all = "lowercase")]
enum ResultType {
    Suite,
    Test,
}

/// This struct holds the deserialized results from running a test. We are not storing all of the
/// values that we get back since we only need to know whether it's a Test or a Suite and whether
/// it passed or not.
///
/// Sample json output:
/// { "type": "suite", "event": "started", "test_count": 1 }
/// { "type": "test", "event": "started", "name": "tests::purposefully_failing_test" }
/// { "type": "test", "name": "tests::purposefully_failing_test", "event": "failed", "stdout": "Rust Test Output" }
/// { "type": "suite", "event": "failed", "passed": 0, "failed": 1, "allowed_fail": 0, "ignored": 0, "measured": 0, "filtered_out": 2 }
///
/// { "type": "suite", "event": "started", "test_count": 1 }
/// { "type": "test", "event": "started", "name": "tests::test_full_path" }
/// { "type": "test", "name": "tests::test_full_path", "event": "ok" }
/// { "type": "suite", "event": "ok", "passed": 1, "failed": 0, "allowed_fail": 0, "ignored": 0, "measured": 0, "filtered_out": 2 }
///
#[derive(Debug, Deserialize)]
struct JsonResult {
    #[serde(rename(deserialize = "type"))]
    test_type: ResultType,
    event: ResultEvent,
    #[serde(rename(deserialize = "stdout"))]
    output: Option<String>,
}

#[derive(Debug)]
pub struct RustTestAdapter {
    test_info: TestInfo,
    c_test_path: CString,
    c_test_file_name: CString,
}

impl RustTestAdapter {
    /// Creates a new RustTestAdapter if the `test_path` is valid
    pub fn new(test_info: TestInfo) -> Result<RustTestAdapter, Error> {
        let test_file_name = test_adapter_lib::extract_test_filename(&test_info.test_path)?;
        Ok(RustTestAdapter {
            c_test_path: CString::new(&test_info.test_path[..])?,
            c_test_file_name: CString::new(&test_file_name[..])?,
            test_info: test_info,
        })
    }

    pub async fn run_test_suite(&self, mut stream: ftest::SuiteRequestStream) -> Result<(), Error> {
        while let Some(event) = stream.try_next().await? {
            match event {
                ftest::SuiteRequest::GetTests { responder } => {
                    fx_log_info!("gathering tests");
                    let test_cases = self.enumerate_tests().await?;
                    responder.send(
                        &mut test_cases.into_iter().map(|name| ftest::Case { name: Some(name) }),
                    )?;
                }
                ftest::SuiteRequest::Run { tests, run_listener, .. } => {
                    let proxy = run_listener
                        .into_proxy()
                        .context("Can't convert listener channel to proxy")?;

                    let test_names = tests
                        .into_iter()
                        .map(|test| {
                            let case = test.case.expect("Invocation must have a Case");
                            case.name.expect("Case must have a name")
                        })
                        .collect();

                    self.run_tests(test_names, proxy).await?;
                }
            }
        }
        Ok(())
    }

    async fn run_tests(
        &self,
        test_names: Vec<String>,
        proxy: RunListenerProxy,
    ) -> Result<(), Error> {
        fx_log_info!("running tests");
        for name in test_names {
            let (log_end, _logger) =
                zx::Socket::create(zx::SocketOpts::empty()).context("cannot create socket.")?;

            proxy.on_test_case_started(&name, log_end).context("on_test_case_started failed")?;

            match self.run_test(&name).await {
                Ok(outcome) => proxy
                    .on_test_case_finished(&name, outcome)
                    .context("on_test_case_finished failed")?,
                Err(error) => {
                    fx_log_err!("failed to run test. {}", error);
                    proxy
                        .on_test_case_finished(
                            &name,
                            ftest::Outcome { status: Some(ftest::Status::Failed) },
                        )
                        .context("on_test_case_finished failed")?;
                }
            }
        }

        Ok(())
    }

    /// Lauches a process that lists the tests without actually running any of them. It then parses
    /// the output of that process into a vector of strings.
    ///
    /// Example output:
    ///
    /// tests::purposefully_failing_test: test
    /// tests::test_full_path: test
    /// tests::test_minimal_path: test
    ///
    /// 3 tests, 0 benchmarks
    ///
    async fn enumerate_tests(&self) -> Result<Vec<String>, Error> {
        let (process, logger) = test_adapter_lib::launch_process(
            &self.c_test_path,
            &self.c_test_file_name,
            &[&self.c_test_file_name, &DASH_Z, &USTABLE_OPTIONS, &LIST],
        )?;

        fasync::OnSignals::new(&process, zx::Signals::PROCESS_TERMINATED)
            .await
            .context("Error waiting for test process to exit")?;

        let process_info = process.info().context("Error getting info from process")?;

        if process_info.return_code != 0 {
            let logs = logger.try_concat().await?;
            let output = from_utf8(&logs)?;
            // TODO(anmittal): Add a error logger to API before porting this to runner so that we
            // can display test stdout logs.
            fx_log_err!("Failed getting list of tests:\n{}", output);
            return Err(format_err!("Can't get list of tests. check logs"));
        }

        let output = logger.try_concat().await?;
        let test_list = from_utf8(&output)?.to_string();

        let mut test_names = vec![];
        let regex = Regex::new(r"tests::(.*): test").unwrap();

        for test in test_list.split("\n") {
            if let Some(capture) = regex.captures(test) {
                if let Some(name) = capture.get(1) {
                    test_names.push(String::from(name.as_str()));
                }
            }
        }

        Ok(test_names)
    }

    /// Launches a process that actually runs the test and parses the resulting json output.
    async fn run_test(&self, name: &String) -> Result<ftest::Outcome, Error> {
        let c_test_name = CString::new(name.as_str()).unwrap();
        let mut args: Vec<&CStr> = vec![&self.c_test_file_name, &c_test_name];

        let mut c_args: Vec<CString> = vec![];
        c_args.extend(self.test_info.test_args.iter().map(|arg| CString::new(&arg[..]).unwrap()));
        args.extend(c_args.iter().map(|arg| arg.as_ref()));
        args.extend_from_slice(&[&DASH_Z, &USTABLE_OPTIONS, &JSON_FORMAT, &NO_CAPTURE]);

        let (process, logger) =
            test_adapter_lib::launch_process(&self.c_test_path, &self.c_test_file_name, &args[..])?;

        fasync::OnSignals::new(&process, zx::Signals::PROCESS_TERMINATED)
            .await
            .context("Error waiting for test process to exit")?;

        // TODO(casbor): The process always ends with a return code of 0 for passing tests. In all
        // other cases it returns 101. It doesn't matter if it's an error running the test or if the
        // test just fails. For now we're not going to rely on the return code and see if we can
        // parse the json. If we can then it's a passing or failing test. If we can't, then it's an
        // error and can be reported as such. Tracking bug: https://github.com/rust-lang/rust/issues/67210

        let results = logger.try_concat().await?;
        let json = from_utf8(&results)?.to_string();
        let json = json.trim();

        for line in json.split("\n") {
            let json_result: Result<JsonResult, serde_json::error::Error> = json::from_str(&line);
            match json_result {
                Ok(result) => {
                    if let ResultType::Test = result.test_type {
                        match result.event {
                            ResultEvent::Failed => {
                                if let Some(output) = result.output {
                                    fx_log_info!("Test Failed:\n{}", output);
                                }
                                return Ok(ftest::Outcome { status: Some(ftest::Status::Failed) });
                            }
                            // TODO(casbor): There isn't a "Ignored" status so for now just return
                            // that it passed.
                            ResultEvent::Ok => {
                                return Ok(ftest::Outcome { status: Some(ftest::Status::Passed) })
                            }
                            _ => (),
                        }
                    }
                }
                // This isn't json so just pass it on to the console.
                Err(_) => fx_log_info!("{}", line),
            }
        }

        Err(format_err!("Failed to run test: {}", json))
    }

    fn _check_process_return_code(process: &zx::Process) -> Result<(), Error> {
        let process_info = process.info().context("Error getting info from process")?;
        if process_info.return_code != 0 && process_info.return_code != 101 {
            Err(format_err!("test process returned an error: {}", process_info.return_code))
        } else {
            Ok(())
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl_fuchsia_test::{
            RunListenerMarker,
            RunListenerRequest::{OnTestCaseFinished, OnTestCaseStarted},
            RunListenerRequestStream,
        },
        std::cmp::PartialEq,
    };

    #[derive(PartialEq, Debug)]
    enum ListenerEvent {
        StartTest(String),
        FinishTest(String, ftest::Outcome),
    }

    async fn collect_results(
        mut listener: RunListenerRequestStream,
    ) -> Result<Vec<ListenerEvent>, Error> {
        let mut events = vec![];
        while let Some(result_event) = listener.try_next().await? {
            match result_event {
                OnTestCaseStarted { name, .. } => {
                    events.push(ListenerEvent::StartTest(name));
                }
                OnTestCaseFinished { name, outcome, .. } => {
                    events.push(ListenerEvent::FinishTest(name, outcome))
                }
            }
        }
        Ok(events)
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn no_tests() {
        let test_info =
            TestInfo { test_path: String::from("/pkg/bin/no_rust_tests"), test_args: vec![] };

        let adapter = RustTestAdapter::new(test_info).expect("Cannot create adapter");

        let expected_tests: Vec<String> = vec![];
        let actual_tests = adapter.enumerate_tests().await.expect("Can't enumerate tests");

        assert_eq!(expected_tests, actual_tests);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn enumerate_simple_tests() {
        let test_info =
            TestInfo { test_path: String::from("/pkg/bin/simple_rust_tests"), test_args: vec![] };

        let adapter = RustTestAdapter::new(test_info).expect("Cannot create adapter");

        let mut expected_tests = vec![
            String::from("simple_test_one"),
            String::from("simple_test_two"),
            String::from("simple_test_three"),
            String::from("simple_test_four"),
        ];

        expected_tests.sort();

        let actual_tests = adapter.enumerate_tests().await.expect("Can't enumerate tests");

        assert_eq!(expected_tests, actual_tests);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn run_passing_test() {
        let test_info =
            TestInfo { test_path: String::from("/pkg/bin/test_outcomes"), test_args: vec![] };

        let adapter = RustTestAdapter::new(test_info).expect("Cannot create adapter");
        let test_name = String::from("passing_test");
        let outcome = adapter.run_test(&test_name).await.expect("Failed to run test");

        assert_eq!(ftest::Outcome { status: Some(ftest::Status::Passed) }, outcome);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn run_failing_test() {
        let test_info =
            TestInfo { test_path: String::from("/pkg/bin/test_outcomes"), test_args: vec![] };

        let adapter = RustTestAdapter::new(test_info).expect("Cannot create adapter");
        let test_name = String::from("failing_test");
        let outcome = adapter.run_test(&test_name).await.expect("Failed to run test");

        assert_eq!(ftest::Outcome { status: Some(ftest::Status::Failed) }, outcome);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn run_entire_test_suite() {
        let test_info =
            TestInfo { test_path: String::from("/pkg/bin/test_outcomes"), test_args: vec![] };

        let adapter = RustTestAdapter::new(test_info).expect("Cannot create adapter");

        let test_names = vec![
            String::from("a_passing_test"),
            String::from("b_failing_test"),
            String::from("c_passing_test"),
        ];

        let (run_listener_client, run_listener) =
            fidl::endpoints::create_request_stream::<RunListenerMarker>()
                .expect("failed to create run_listener");
        let proxy = run_listener_client.into_proxy().expect("can't convert listener into proxy");

        let run_future = adapter.run_tests(test_names, proxy);
        let result_future = collect_results(run_listener);

        let (run_option, result_option) = future::join(run_future, result_future).await;

        run_option.expect("Failed running tests");

        let expected_results = vec![
            ListenerEvent::StartTest(String::from("a_passing_test")),
            ListenerEvent::FinishTest(
                String::from("a_passing_test"),
                ftest::Outcome { status: Some(ftest::Status::Passed) },
            ),
            ListenerEvent::StartTest(String::from("b_failing_test")),
            ListenerEvent::FinishTest(
                String::from("b_failing_test"),
                ftest::Outcome { status: Some(ftest::Status::Failed) },
            ),
            ListenerEvent::StartTest(String::from("c_passing_test")),
            ListenerEvent::FinishTest(
                String::from("c_passing_test"),
                ftest::Outcome { status: Some(ftest::Status::Passed) },
            ),
        ];

        let actual_results = result_option.expect("Failed to collect results");

        assert_eq!(expected_results, actual_results);
    }

    #[test]
    fn invalid_file() {
        let test_info =
            TestInfo { test_path: String::from("/pkg/bin/invalid_test_file"), test_args: vec![] };
        RustTestAdapter::new(test_info).expect_err("This should fail");
    }
}
