// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    async_trait::async_trait,
    fidl_fuchsia_test as ftest,
    ftest::{Invocation, RunListenerProxy},
    fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::future::abortable,
    futures::future::AbortHandle,
    futures::prelude::*,
    log::{debug, error, info},
    regex::Regex,
    std::{
        str::from_utf8,
        sync::{Arc, Weak},
    },
    test_runners_lib::{
        cases::TestCaseInfo,
        elf::{Component, FidlError, KernelError, SuiteServer},
        errors::*,
        launch,
        logs::{LogError, LogStreamReader, LogWriter, LoggerStream},
    },
};

/// Implements `fuchsia.test.Suite` and runs provided test.
pub struct TestServer {
    /// Cache to store enumerated test names.
    test_list: Option<Arc<Vec<TestCaseInfo>>>,
}

#[async_trait]
impl SuiteServer for TestServer {
    /// Launches a process that lists the tests without actually running any of them. It then parses
    /// the output of that process into a vector of strings.
    ///
    /// Example output for rust test process:
    ///
    /// ```text
    /// tests::purposefully_failing_test: test
    /// tests::test_full_path: test
    /// tests::test_minimal_path: test
    ///
    /// 3 tests, 0 benchmarks
    /// ```
    ///
    /// The list of tests is cached.
    async fn enumerate_tests(
        &mut self,
        test_component: Arc<Component>,
    ) -> Result<Arc<Vec<TestCaseInfo>>, EnumerationError> {
        // Caching
        if self.test_list.is_none() {
            self.test_list = Some(Arc::new(Self::enumerate_tests_internal(test_component).await?));
        }
        Ok(self.test_list.as_ref().expect("Unexpected caching error").clone())
    }

    // TODO(45852): Support disabled tests.
    async fn run_tests(
        &self,
        invocations: Vec<Invocation>,
        test_component: Arc<Component>,
        run_listener: &RunListenerProxy,
    ) -> Result<(), RunTestError> {
        for invocation in invocations {
            let test = invocation.name.as_ref().ok_or(RunTestError::TestCaseName)?.to_string();
            info!("Running test {}", test);

            let (test_logger, log_client) = zx::Socket::create(zx::SocketOpts::DATAGRAM)
                .map_err(KernelError::CreateSocket)
                .unwrap();

            let (case_listener_proxy, listener) =
                fidl::endpoints::create_proxy::<fidl_fuchsia_test::CaseListenerMarker>()
                    .map_err(FidlError::CreateProxy)
                    .unwrap();

            run_listener
                .on_test_case_started(invocation, log_client, listener)
                .map_err(RunTestError::SendStart)?;
            let test_logger = fasync::Socket::from_socket(test_logger)
                .map_err(KernelError::SocketToAsync)
                .unwrap();
            let mut test_logger = LogWriter::new(test_logger);

            match self.run_test(&test, &test_component, &mut test_logger).await {
                Ok(result) => {
                    case_listener_proxy.finished(result).map_err(RunTestError::SendFinish)?;
                }
                Err(error) => {
                    error!("failed to run test '{}'. {}", test, error);
                    case_listener_proxy
                        .finished(ftest::Result_ { status: Some(ftest::Status::Failed) })
                        .map_err(RunTestError::SendFinish)?;
                }
            }
        }
        Ok(())
    }

    /// Run this server.
    fn run(
        self,
        weak_test_component: Weak<Component>,
        test_url: &str,
        request_stream: fidl_fuchsia_test::SuiteRequestStream,
    ) -> AbortHandle {
        let test_url = test_url.clone().to_owned();
        let (fut, test_suite_abortable_handle) =
            abortable(self.serve_test_suite(request_stream, weak_test_component.clone()));

        fasync::spawn_local(async move {
            match fut.await {
                Ok(result) => {
                    if let Err(e) = result {
                        error!("server failed for test {}: {:?}", test_url, e);
                    }
                }
                Err(e) => error!("server aborted for test {}: {:?}", test_url, e),
            }
            debug!("Done running server for {}.", test_url);
        });
        test_suite_abortable_handle
    }
}

impl TestServer {
    /// Creates new test server.
    /// Clients should call this function to create new object and then call `serve_test_suite`.
    pub fn new() -> Self {
        Self { test_list: None }
    }

    #[cfg(rust_panic = "unwind")]
    async fn run_test(
        &self,
        _test: &str,
        _test_component: &Component,
        _test_logger: &mut LogWriter,
    ) -> Result<ftest::Result_, RunTestError> {
        // this will go away soon, so no use of supporting it when we can't
        // even test this code.
        panic!("not supported");
    }

    /// Launches a process that actually runs the test and parses the resulting json output.
    #[cfg(rust_panic = "abort")]
    async fn run_test(
        &self,
        test: &str,
        test_component: &Component,
        test_logger: &mut LogWriter,
    ) -> Result<ftest::Result_, RunTestError> {
        // Exit codes used by Rust's libtest runner.
        const TR_OK: i64 = 50;
        const TR_FAILED: i64 = 51;

        let test_invoke = format!("__RUST_TEST_INVOKE={}", test);
        let mut args = vec!["--nocapture".to_owned()];
        args.extend(test_component.args.clone());

        // run test.
        // Load bearing to hold job guard.
        let (process, _job, mut stdlogger) =
            launch_component_process::<RunTestError>(&test_component, args, Some(test_invoke))
                .await?;

        let mut buf: Vec<u8> = vec![];
        let newline = b'\n';
        while let Some(bytes) = stdlogger.try_next().await.map_err(LogError::Read)? {
            if bytes.is_empty() {
                continue;
            }

            // buffer by newline, find last newline and send message till then,
            // store rest in buffer.
            match bytes.iter().rposition(|&x| x == newline) {
                Some(i) => {
                    buf.extend_from_slice(&bytes[0..=i]);
                    test_logger.write(&buf).await?;
                    buf.clear();
                    buf.extend_from_slice(&bytes[i + 1..]);
                }
                None => {
                    buf.extend(bytes);
                }
            }
        }

        if buf.len() > 0 {
            // last message did not contain newline, send it.
            test_logger.write(&buf).await?;
        }

        fasync::OnSignals::new(&process, zx::Signals::PROCESS_TERMINATED)
            .await
            .map_err(KernelError::ProcessExit)
            .unwrap();
        let process_info = process.info().map_err(RunTestError::ProcessInfo)?;

        match process_info.return_code {
            TR_OK => Ok(ftest::Result_ { status: Some(ftest::Status::Passed) }),
            TR_FAILED => {
                test_logger.write_str("test failed.\n".to_owned()).await?;
                Ok(ftest::Result_ { status: Some(ftest::Status::Failed) })
            }
            other => Err(RunTestError::UnexpectedReturnCode(other)),
        }
    }

    /// Internal, uncached implementation of `enumerate_tests`.
    async fn enumerate_tests_internal(
        test_component: Arc<Component>,
    ) -> Result<Vec<TestCaseInfo>, EnumerationError> {
        let mut args = vec!["-Z".to_owned(), "unstable-options".to_owned(), "--list".to_owned()];
        args.extend(test_component.args.clone());

        // Load bearing to hold job guard.
        let (process, _job, stdlogger) =
            launch_component_process::<EnumerationError>(&test_component, args, None).await?;

        // collect stdout in background before waiting for process termination.
        let std_reader = LogStreamReader::new(stdlogger);

        fasync::OnSignals::new(&process, zx::Signals::PROCESS_TERMINATED)
            .await
            .map_err(KernelError::ProcessExit)
            .unwrap();

        let logs = std_reader.get_logs().await?;
        // TODO(4610): logs might not be utf8, fix the code.
        let output = from_utf8(&logs)?;
        let process_info = process.info().map_err(KernelError::ProcessInfo).unwrap();
        if process_info.return_code != 0 {
            // TODO(45858): Add a error logger to API so that we can display test stdout logs.
            error!("Failed getting list of tests:\n{}", output);
            return Err(EnumerationError::ListTest);
        }

        let mut tests = vec![];
        let regex = Regex::new(r"^(.*): test$").unwrap();

        for test in output.split("\n") {
            if let Some(capture) = regex.captures(test) {
                if let Some(name) = capture.get(1) {
                    tests.push(TestCaseInfo { name: String::from(name.as_str()) });
                }
            }
        }

        return Ok(tests);
    }
}

/// Convenience wrapper around [`launch::launch_process`].
async fn launch_component_process<E>(
    component: &Component,
    args: Vec<String>,
    test_invoke: Option<String>,
) -> Result<(zx::Process, launch::ScopedJob, LoggerStream), E>
where
    E: From<NamespaceError> + From<launch::LaunchError>,
{
    Ok(launch::launch_process(launch::LaunchProcessArgs {
        bin_path: &component.binary,
        process_name: &component.name,
        job: Some(component.job.create_child_job().map_err(KernelError::CreateJob).unwrap()),
        ns: component.ns.clone().map_err(NamespaceError::Clone)?,
        args: Some(args),
        name_infos: None,
        environs: test_invoke.map(|test_invoke| vec![test_invoke]),
    })
    .await?)
}

// TODO(45854): Add integration tests.
#[cfg(test)]
mod tests {
    use {
        super::*,
        anyhow::{Context as _, Error},
        fidl::endpoints::ClientEnd,
        fidl_fuchsia_component_runner as fcrunner,
        fidl_fuchsia_io::OPEN_RIGHT_READABLE,
        fidl_fuchsia_test::{
            Result_ as TestResult, RunListenerMarker, RunOptions, Status, SuiteMarker,
        },
        fuchsia_runtime::job_default,
        itertools::Itertools,
        runner::component::ComponentNamespace,
        runner::component::ComponentNamespaceError,
        std::convert::TryFrom,
        test_runners_lib::cases::TestCaseInfo,
        test_runners_test_lib::{collect_listener_event, names_to_invocation, ListenerEvent},
    };

    fn create_ns_from_current_ns(
        dir_paths: Vec<(&str, u32)>,
    ) -> Result<ComponentNamespace, ComponentNamespaceError> {
        let mut ns = vec![];
        for (path, permission) in dir_paths {
            let chan = io_util::open_directory_in_namespace(path, permission)
                .unwrap()
                .into_channel()
                .unwrap()
                .into_zx_channel();
            let handle = ClientEnd::new(chan);

            ns.push(fcrunner::ComponentNamespaceEntry {
                path: Some(path.to_string()),
                directory: Some(handle),
            });
        }
        ComponentNamespace::try_from(ns)
    }

    macro_rules! current_job {
        () => {
            job_default().duplicate(zx::Rights::SAME_RIGHTS)?
        };
    }

    fn sample_test_component() -> Result<Arc<Component>, Error> {
        let ns = create_ns_from_current_ns(vec![("/pkg", OPEN_RIGHT_READABLE)])?;

        Ok(Arc::new(Component {
            url: "fuchsia-pkg://fuchsia.com/simple_test#test.cm".to_owned(),
            name: "bin/sample_rust_tests".to_owned(),
            binary: "bin/sample_rust_tests".to_owned(),
            args: vec![],
            ns: ns,
            job: current_job!(),
        }))
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn enumerate_simple_test() -> Result<(), Error> {
        let component = sample_test_component().unwrap();
        let mut server = TestServer::new();
        let expected: Vec<TestCaseInfo> = vec![
            TestCaseInfo { name: "my_tests::sample_test_one".to_string() },
            TestCaseInfo { name: "my_tests::passing_test".to_string() },
            TestCaseInfo { name: "my_tests::failing_test".to_string() },
            TestCaseInfo { name: "my_tests::sample_test_two".to_string() },
        ]
        .into_iter()
        .sorted()
        .collect();

        let actual: Vec<TestCaseInfo> = server
            .enumerate_tests(component.clone())
            .await?
            .iter()
            .sorted()
            .map(Clone::clone)
            .collect();

        assert_eq!(&expected, &actual);

        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn enumerate_empty_test_file() -> Result<(), Error> {
        let ns = create_ns_from_current_ns(vec![("/pkg", OPEN_RIGHT_READABLE)])?;

        let component = Arc::new(Component {
            url: "fuchsia-pkg://fuchsia.com/sample_test#test.cm".to_owned(),
            name: "bin/no_rust_tests".to_owned(),
            binary: "bin/no_rust_tests".to_owned(),
            args: vec![],
            ns: ns,
            job: current_job!(),
        });
        let mut server = TestServer::new();

        assert_eq!(*server.enumerate_tests(component.clone()).await?, Vec::<TestCaseInfo>::new());

        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn enumerate_huge_test() -> Result<(), Error> {
        let ns = create_ns_from_current_ns(vec![("/pkg", OPEN_RIGHT_READABLE)])?;

        let component = Arc::new(Component {
            url: "fuchsia-pkg://fuchsia.com/simple_test#test.cm".to_owned(),
            name: "bin/huge_rust_tests".to_owned(),
            binary: "bin/huge_rust_tests".to_owned(),
            args: vec![],
            ns: ns,
            job: current_job!(),
        });
        let mut server = TestServer::new();

        let actual_tests: Vec<TestCaseInfo> = server
            .enumerate_tests(component.clone())
            .await?
            .iter()
            .sorted()
            .map(Clone::clone)
            .collect();

        let expected: Vec<TestCaseInfo> =
            (1..=1000).map(|i| TestCaseInfo { name: format!("test_{}", i) }).sorted().collect();

        assert_eq!(&expected, &actual_tests);

        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn enumerate_invalid_file() -> Result<(), Error> {
        let ns = create_ns_from_current_ns(vec![("/pkg", OPEN_RIGHT_READABLE)])?;

        let component = Arc::new(Component {
            url: "fuchsia-pkg://fuchsia.com/sample_test#test.cm".to_owned(),
            name: "bin/invalid".to_owned(),
            binary: "bin/invalid".to_owned(),
            args: vec![],
            ns: ns,
            job: current_job!(),
        });
        let mut server = TestServer::new();

        let err = server
            .enumerate_tests(component.clone())
            .await
            .expect_err("this function have error-ed out due to non-existent file.");

        match err {
            EnumerationError::LaunchTest(launch::LaunchError::LoadInfo(
                runner::component::LaunchError::LoadingExecutable(_),
            )) => { /*this is the error we expect, do nothing*/ }
            err => panic!("invalid error:{}", err),
        };

        Ok(())
    }

    async fn run_tests(invocations: Vec<Invocation>) -> Result<Vec<ListenerEvent>, anyhow::Error> {
        let component = sample_test_component().context("Cannot create test component")?;
        let weak_component = Arc::downgrade(&component);
        let server = TestServer::new();

        let (run_listener_client, run_listener) =
            fidl::endpoints::create_request_stream::<RunListenerMarker>()
                .context("Failed to create run_listener")?;
        let (test_suite_client, test_suite) =
            fidl::endpoints::create_request_stream::<SuiteMarker>()
                .context("failed to create suite")?;

        let suite_proxy =
            test_suite_client.into_proxy().context("can't convert suite into proxy")?;
        fasync::spawn(async move {
            server
                .serve_test_suite(test_suite, weak_component)
                .await
                .expect("Failed to run test suite")
        });

        suite_proxy
            .run(&mut invocations.into_iter().map(|i| i.into()), RunOptions {}, run_listener_client)
            .context("cannot call run")?;

        collect_listener_event(run_listener).await.context("Failed to collect results")
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn run_one_test() -> Result<(), Error> {
        let events = run_tests(names_to_invocation(vec!["my_tests::passing_test"])).await.unwrap();

        let expected_events = vec![
            ListenerEvent::start_test("my_tests::passing_test"),
            ListenerEvent::finish_test(
                "my_tests::passing_test",
                TestResult { status: Some(Status::Passed) },
            ),
            ListenerEvent::finish_all_test(),
        ];

        assert_eq!(expected_events, events);

        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn run_multiple_tests() -> Result<(), Error> {
        let events = run_tests(names_to_invocation(vec![
            "my_tests::sample_test_one",
            "my_tests::passing_test",
            "my_tests::failing_test",
            "my_tests::sample_test_two",
        ]))
        .await
        .unwrap();

        let expected_events = vec![
            ListenerEvent::start_test("my_tests::sample_test_one"),
            ListenerEvent::finish_test(
                "my_tests::sample_test_one",
                TestResult { status: Some(Status::Passed) },
            ),
            ListenerEvent::start_test("my_tests::passing_test"),
            ListenerEvent::finish_test(
                "my_tests::passing_test",
                TestResult { status: Some(Status::Passed) },
            ),
            ListenerEvent::start_test("my_tests::failing_test"),
            ListenerEvent::finish_test(
                "my_tests::failing_test",
                TestResult { status: Some(Status::Failed) },
            ),
            ListenerEvent::start_test("my_tests::sample_test_two"),
            ListenerEvent::finish_test(
                "my_tests::sample_test_two",
                TestResult { status: Some(Status::Passed) },
            ),
            ListenerEvent::finish_all_test(),
        ];

        assert_eq!(expected_events, events);
        Ok(())
    }
}
