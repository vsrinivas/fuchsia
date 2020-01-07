// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context as _, Error},
    fidl_fuchsia_test::{Outcome, RunListenerProxy, Status},
    fuchsia_async as fasync,
    fuchsia_syslog::{fx_log_err, fx_log_info},
    fuchsia_zircon as zx,
    futures::prelude::*,
    serde_derive::{Deserialize, Serialize},
    std::{ffi::CString, fs::File, io::BufReader, str::from_utf8},
};

/// Provides info about individual test cases.
/// Example: For test FOO.Bar, this contains info about Bar.
/// Please refer to documentation of `ListTestResult` for details.
#[derive(Serialize, Deserialize, Debug)]
struct IndividualTestInfo {
    pub name: String,
    pub file: String,
    pub line: u64,
}

/// Provides info about individual test suites.
/// Example: For test FOO.Bar, this contains info about FOO.
/// Please refer to documentation of `ListTestResult` for details.
#[derive(Serialize, Deserialize, Debug)]
struct TestSuiteResult {
    pub tests: usize,
    pub name: String,
    pub testsuite: Vec<IndividualTestInfo>,
}

/// Sample json will look like
/// ```
/// {
/// "tests": 6,
/// "name": "AllTests",
/// "testsuites": [
///    {
///      "name": "SampleTest1",
///      "tests": 2,
///      "testsuite": [
///        {
///          "name": "Test1",
///          "file": "../../src/sys/test_adapters/gtest/test_data/sample_tests.cc",
///          "line": 7
///        },
///        {
///          "name": "Test2",
///          "file": "../../src/sys/test_adapters/gtest/test_data/sample_tests.cc",
///          "line": 9
///        }
///      ]
///    },
///  ]
///}
///```
#[derive(Serialize, Deserialize, Debug)]
struct ListTestResult {
    pub tests: usize,
    pub name: String,
    pub testsuites: Vec<TestSuiteResult>,
}

/// Provides info about test case failures if any.
#[derive(Serialize, Deserialize, Debug)]
struct Failures {
    pub failure: String,
}

/// Provides info about individual test cases.
/// Example: For test FOO.Bar, this contains info about Bar.
/// Please refer to documentation of `TestOutput` for details.
#[derive(Serialize, Deserialize, Debug)]
struct IndividualTestOutput {
    pub name: String,
    pub status: String,
    pub time: String,
    pub failures: Option<Vec<Failures>>,
}

/// Provides info about individual test suites.
/// Refer to https://github.com/google/googletest/blob/2002f267f05be6f41a3d458954414ba2bfa3ff1d/googletest/docs/advanced.md#generating-a-json-report
/// for output structure.
#[derive(Serialize, Deserialize, Debug)]
struct TestSuiteOutput {
    pub name: String,
    pub tests: usize,
    pub failures: usize,
    pub disabled: usize,
    pub time: String,
    pub testsuite: Vec<IndividualTestOutput>,
}

/// Provides info test and the its run result.
/// Example: For test FOO.Bar, this contains info about FOO.
/// Please refer to documentation of `TestOutput` for details.
#[derive(Serialize, Deserialize, Debug)]
struct TestOutput {
    pub testsuites: Vec<TestSuiteOutput>,
}

#[derive(Debug)]
pub struct GTestAdapter {
    c_test_path: CString,
    c_test_file_name: CString,
    test_file_name: String,
}

impl GTestAdapter {
    /// Creates a new GTest adapter if `test_path` is valid
    pub fn new(test_path: String) -> Result<GTestAdapter, Error> {
        let test_file_name = test_adapter_lib::extract_test_filename(&test_path)?;

        Ok(GTestAdapter {
            c_test_path: CString::new(&test_path[..])?,
            c_test_file_name: CString::new(&test_file_name[..])?,
            test_file_name: test_file_name,
        })
    }

    /// Runs tests defined by `tests_names` and uses `run_listener` to send test events.
    // TODO(anmittal): Support disabled tests.
    // TODO(anmittal): Don't run tests which are not present in test file or handle them.
    // TODO(anmittal): Support test stdout, or devise a mechanism to replace it.
    pub async fn run_tests(
        &self,
        test_names: Vec<String>,
        run_listener: RunListenerProxy,
    ) -> Result<(), Error> {
        for test in &test_names {
            fx_log_info!("Running test {}", test);
            let test_result_file = format!("/tmp/{}_test_result.json", test);

            let (process, logger) = test_adapter_lib::launch_process(
                &self.c_test_path,
                &self.c_test_file_name,
                &[
                    &self.c_test_file_name,
                    &CString::new(format!("--gtest_filter={}", test))?,
                    &CString::new(format!("--gtest_output=json:{}", test_result_file))?,
                ],
            )?;

            let (test_logger, log_client) =
                zx::Socket::create(zx::SocketOpts::DATAGRAM).context("cannot create socket")?;
            run_listener
                .on_test_case_started(test, log_client)
                .context("Cannot send start event")?;

            let mut test_logger = fasync::Socket::from_socket(test_logger)?;

            // collect stdout in background before waiting for process termination.
            let (logger_handle, logger_fut) = logger.try_concat().remote_handle();
            fasync::spawn_local(async move {
                logger_handle.await;
            });

            fx_log_info!("waiting for test to finish: {}", test);

            fasync::OnSignals::new(&process, zx::Signals::PROCESS_TERMINATED)
                .await
                .context("Error waiting for test process to exit")?;

            fx_log_info!("collecting logs for {}", test);
            let logs = logger_fut.await?;
            let output = from_utf8(&logs)?;

            fx_log_info!("open output file for {}", test);
            // Open the file in read-only mode with buffer.
            // TODO(anmittal): Convert this to async ops.
            let output_file = if let Ok(f) = File::open(&test_result_file) {
                f
            } else {
                // TODO(anmittal): Introduce Status::InternalError.
                test_logger
                    .write(format!("test did not complete, test output:\n{}", output).as_bytes())
                    .await
                    .context("cannot write logs")?;
                run_listener
                    .on_test_case_finished(test, Outcome { status: Some(Status::Failed) })
                    .context("Cannot send finish event")?;
                continue; // run next test
            };

            let reader = BufReader::new(output_file);

            fx_log_info!("parse output file for {}", test);
            let test_list: TestOutput =
                serde_json::from_reader(reader).context("Can't get test result")?;

            fx_log_info!("parsed output file for {}", test);
            if test_list.testsuites.len() != 1 || test_list.testsuites[0].testsuite.len() != 1 {
                // TODO(anmittal): Introduce Status::InternalError.
                test_logger
                    .write(format!("unexpected output:\n{}", output).as_bytes())
                    .await
                    .context("cannot write logs")?;
                run_listener
                    .on_test_case_finished(test, Outcome { status: Some(Status::Failed) })
                    .context("Cannot send finish event")?;
                continue; // run next test
            }

            // as we only run one test per iteration result would be always at 0 index in the arrays.
            match &test_list.testsuites[0].testsuite[0].failures {
                Some(failures) => {
                    for f in failures {
                        test_logger
                            .write(format!("failure: {}\n", f.failure).as_bytes())
                            .await
                            .context("cannot write logs")?;
                    }
                    run_listener
                        .on_test_case_finished(test, Outcome { status: Some(Status::Failed) })
                        .context("Cannot send finish event")?;
                }
                None => {
                    run_listener
                        .on_test_case_finished(test, Outcome { status: Some(Status::Passed) })
                        .context("Cannot send finish event")?;
                }
            }
            fx_log_info!("test finish {}", test);
        }
        Ok(())
    }

    /// Launches test process and gets test list out. Returns list of tests names in the format
    /// defined by gtests, i.e FOO.Bar
    pub async fn enumerate_tests(&self) -> Result<Vec<String>, Error> {
        let test_list_file = format!("/tmp/{}_test_list.json", self.test_file_name);

        let (process, logger) = test_adapter_lib::launch_process(
            &self.c_test_path,
            &self.c_test_file_name,
            &[
                &self.c_test_file_name,
                &CString::new("--gtest_list_tests")?,
                &CString::new(format!("--gtest_output=json:{}", test_list_file))?,
            ],
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

        // Open the file in read-only mode with buffer.
        let open_file_result = File::open(&test_list_file);
        if let Err(e) = open_file_result {
            let logs = logger.try_concat().await?;
            let output = from_utf8(&logs)?;
            fx_log_err!("Failed getting list of tests from {}:\n{}", test_list_file, output);
            return Err(e.into());
        }

        let output_file = open_file_result?;

        let reader = BufReader::new(output_file);

        let test_list: ListTestResult =
            serde_json::from_reader(reader).context("Can't get test from gtest")?;

        let mut tests = Vec::<String>::with_capacity(test_list.tests);

        for suite in &test_list.testsuites {
            for test in &suite.testsuite {
                tests.push(format!("{}.{}", suite.name, test.name))
            }
        }

        return Ok(tests);
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

    #[fuchsia_async::run_singlethreaded(test)]
    async fn empty_test_file() {
        let adapter =
            GTestAdapter::new("/pkg/bin/no_tests".to_owned()).expect("Cannot create adapter");
        let tests = adapter.enumerate_tests().await.expect("Can't enumerate tests");
        assert_eq!(tests.len(), 0, "got {:?}", tests);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn sample_test_file() {
        let adapter =
            GTestAdapter::new("/pkg/bin/sample_tests".to_owned()).expect("Cannot create adapter");
        let tests = adapter.enumerate_tests().await.expect("Can't enumerate tests");
        assert_eq!(
            tests,
            vec![
                "SampleTest1.SimpleFail".to_owned(),
                "SampleTest1.Crashing".to_owned(),
                "SampleTest2.SimplePass".to_owned(),
                "SampleFixture.Test1".to_owned(),
                "SampleFixture.Test2".to_owned(),
                "SampleDisabled.DISABLED_Test1".to_owned(),
                "Tests/SampleParameterizedTestFixture.Test/0".to_owned(),
                "Tests/SampleParameterizedTestFixture.Test/1".to_owned(),
                "Tests/SampleParameterizedTestFixture.Test/2".to_owned(),
                "Tests/SampleParameterizedTestFixture.Test/3".to_owned(),
            ]
        );
    }

    #[derive(PartialEq, Debug)]
    enum ListenerEvent {
        StartTest(String),
        FinishTest(String, Outcome),
    }

    async fn collect_listener_event(
        mut listener: RunListenerRequestStream,
    ) -> Result<Vec<ListenerEvent>, Error> {
        let mut ret = vec![];
        // collect loggers so that they do not die.
        let mut loggers = vec![];
        while let Some(result_event) = listener.try_next().await? {
            match result_event {
                OnTestCaseStarted { name, primary_log, .. } => {
                    ret.push(ListenerEvent::StartTest(name));
                    loggers.push(primary_log);
                }
                OnTestCaseFinished { name, outcome, .. } => {
                    ret.push(ListenerEvent::FinishTest(name, outcome))
                }
            }
        }
        Ok(ret)
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn run_multiple_tests() {
        fuchsia_syslog::init_with_tags(&["gtest_adapter"]).expect("cannot init logger");

        let (run_listener_client, run_listener) =
            fidl::endpoints::create_request_stream::<RunListenerMarker>()
                .expect("Failed to create run_listener");
        let adapter =
            GTestAdapter::new("/pkg/bin/sample_tests".to_owned()).expect("Cannot create adapter");

        let run_fut = adapter.run_tests(
            vec![
                "SampleTest1.SimpleFail".to_owned(),
                "SampleTest1.Crashing".to_owned(),
                "SampleTest2.SimplePass".to_owned(),
                "Tests/SampleParameterizedTestFixture.Test/2".to_owned(),
            ],
            run_listener_client.into_proxy().expect("Can't convert listener into proxy"),
        );

        let result_fut = collect_listener_event(run_listener);

        let (result, events_result) = future::join(run_fut, result_fut).await;
        result.expect("Failed to run tests");

        let events = events_result.expect("Failed to collect events");

        let expected_events = vec![
            ListenerEvent::StartTest("SampleTest1.SimpleFail".to_owned()),
            ListenerEvent::FinishTest(
                "SampleTest1.SimpleFail".to_owned(),
                Outcome { status: Some(Status::Failed) },
            ),
            ListenerEvent::StartTest("SampleTest1.Crashing".to_owned()),
            ListenerEvent::FinishTest(
                "SampleTest1.Crashing".to_owned(),
                Outcome { status: Some(Status::Failed) },
            ),
            ListenerEvent::StartTest("SampleTest2.SimplePass".to_owned()),
            ListenerEvent::FinishTest(
                "SampleTest2.SimplePass".to_owned(),
                Outcome { status: Some(Status::Passed) },
            ),
            ListenerEvent::StartTest("Tests/SampleParameterizedTestFixture.Test/2".to_owned()),
            ListenerEvent::FinishTest(
                "Tests/SampleParameterizedTestFixture.Test/2".to_owned(),
                Outcome { status: Some(Status::Passed) },
            ),
        ];

        assert_eq!(expected_events, events);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn run_no_test() {
        let (run_listener_client, run_listener) =
            fidl::endpoints::create_request_stream::<RunListenerMarker>()
                .expect("Failed to create run_listener");
        let adapter =
            GTestAdapter::new("/pkg/bin/sample_tests".to_owned()).expect("Cannot create adapter");

        let run_fut = adapter.run_tests(
            vec![],
            run_listener_client.into_proxy().expect("Can't convert listener into proxy"),
        );

        let result_fut = collect_listener_event(run_listener);

        let (result, events_result) = future::join(run_fut, result_fut).await;
        result.expect("Failed to run tests");

        let events = events_result.expect("Failed to collect events");
        assert_eq!(events.len(), 0);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn run_one_test() {
        let (run_listener_client, run_listener) =
            fidl::endpoints::create_request_stream::<RunListenerMarker>()
                .expect("Failed to create run_listener");
        let adapter =
            GTestAdapter::new("/pkg/bin/sample_tests".to_owned()).expect("Cannot create adapter");

        let run_fut = adapter.run_tests(
            vec!["SampleTest2.SimplePass".to_owned()],
            run_listener_client.into_proxy().expect("Can't convert listener into proxy"),
        );

        let result_fut = collect_listener_event(run_listener);

        let (result, events_result) = future::join(run_fut, result_fut).await;
        result.expect("Failed to run tests");

        let events = events_result.expect("Failed to collect events");

        let expected_events = vec![
            ListenerEvent::StartTest("SampleTest2.SimplePass".to_owned()),
            ListenerEvent::FinishTest(
                "SampleTest2.SimplePass".to_owned(),
                Outcome { status: Some(Status::Passed) },
            ),
        ];

        assert_eq!(expected_events, events);
    }

    #[test]
    fn invalid_file() {
        GTestAdapter::new("/pkg/bin/invalid_test_file".to_owned()).expect_err("This should fail");
    }
}
