// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    async_trait::async_trait,
    fidl_fuchsia_test as ftest,
    ftest::{Invocation, RunListenerProxy},
    fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::{
        future::{abortable, AbortHandle, FutureExt as _},
        lock::Mutex,
        prelude::*,
    },
    log::{debug, error},
    regex::Regex,
    std::{
        collections::HashSet,
        str::from_utf8,
        sync::{Arc, Weak},
    },
    test_runners_lib::{
        cases::TestCaseInfo,
        elf::{
            Component, EnumeratedTestCases, FidlError, KernelError, MemoizedFutureContainer,
            PinnedFuture, SuiteServer,
        },
        errors::*,
        launch,
        logs::{LogError, LogStreamReader, LogWriter, LoggerStream},
    },
};

type EnumeratedTestNames = Arc<HashSet<String>>;

/// Implements `fuchsia.test.Suite` and runs provided test.
pub struct TestServer {
    /// Cache to store enumerated tests.
    tests_future_container: MemoizedFutureContainer<EnumeratedTestCases, EnumerationError>,
    /// Index of disabled tests for faster membership checking.
    disabled_tests_future_container: MemoizedFutureContainer<EnumeratedTestNames, EnumerationError>,
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
        &self,
        test_component: Arc<Component>,
    ) -> Result<EnumeratedTestCases, EnumerationError> {
        self.tests(test_component).await
    }

    async fn run_tests(
        &self,
        invocations: Vec<Invocation>,
        run_options: ftest::RunOptions,
        test_component: Arc<Component>,
        run_listener: &RunListenerProxy,
    ) -> Result<(), RunTestError> {
        let num_parallel = Self::get_parallel_count(&run_options);

        let invocations = stream::iter(invocations);
        invocations
            .map(Ok)
            .try_for_each_concurrent(num_parallel, |invocation| async {
                let test = invocation.name.as_ref().ok_or(RunTestError::TestCaseName)?.to_string();
                debug!("Running test {}", test);

                let (test_logger, log_client) = zx::Socket::create(zx::SocketOpts::STREAM)
                    .map_err(KernelError::CreateSocket)
                    .unwrap();
                let (case_listener_proxy, listener) =
                    fidl::endpoints::create_proxy::<fidl_fuchsia_test::CaseListenerMarker>()
                        .map_err(FidlError::CreateProxy)
                        .unwrap();
                let test_logger = fasync::Socket::from_socket(test_logger)
                    .map_err(KernelError::SocketToAsync)
                    .unwrap();

                run_listener
                    .on_test_case_started(invocation, log_client, listener)
                    .map_err(RunTestError::SendStart)?;

                let mut test_logger = LogWriter::new(test_logger);

                match self
                    .run_test(&test, &run_options, test_component.clone(), &mut test_logger)
                    .await
                {
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
                return Ok::<(), RunTestError>(());
            })
            .await
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

        fasync::Task::local(async move {
            match fut.await {
                Ok(result) => {
                    if let Err(e) = result {
                        error!("server failed for test {}: {:?}", test_url, e);
                    }
                }
                Err(e) => error!("server aborted for test {}: {:?}", test_url, e),
            }
            debug!("Done running server for {}.", test_url);
        })
        .detach();
        test_suite_abortable_handle
    }
}

impl TestServer {
    /// Creates new test server.
    /// Clients should call this function to create new object and then call `serve_test_suite`.
    pub fn new() -> Self {
        Self {
            tests_future_container: Arc::new(Mutex::new(None)),
            disabled_tests_future_container: Arc::new(Mutex::new(None)),
        }
    }

    /// Retrieves and memoizes the full list of tests from the test binary.
    ///
    /// The entire `Future` is memoized, so repeated calls do not execute the test binary
    /// repeatedly.
    ///
    /// This outer method is _not_ `async`, to avoid capturing a reference to `&self` and fighting
    /// the borrow checker until the end of time.
    fn tests(
        &self,
        test_component: Arc<Component>,
    ) -> impl Future<Output = Result<EnumeratedTestCases, EnumerationError>> {
        /// Fetches the full list of tests from the test binary.
        ///
        /// The `disabled_tests_future` is passed in to determine which tests should be marked
        /// disabled.
        async fn fetch(
            test_component: Arc<Component>,
            disabled_tests_future: impl Future<Output = Result<EnumeratedTestNames, EnumerationError>>
                + Send
                + 'static,
        ) -> Result<EnumeratedTestCases, EnumerationError> {
            let test_names = get_tests(test_component, TestFilter::AllTests).await?;
            let disabled_tests = disabled_tests_future.await?;
            let tests: Vec<TestCaseInfo> = test_names
                .into_iter()
                .map(|name| {
                    let enabled = !disabled_tests.contains(&name);
                    TestCaseInfo { name, enabled }
                })
                .collect();
            Ok(Arc::new(tests))
        }

        /// Populates the given `tests_future_container` with a future, or returns a copy of that
        /// future if already present.
        async fn get_or_insert_tests_future(
            test_component: Arc<Component>,
            tests_future_container: MemoizedFutureContainer<EnumeratedTestCases, EnumerationError>,
            disabled_tests_future: impl Future<Output = Result<EnumeratedTestNames, EnumerationError>>
                + Send
                + 'static,
        ) -> Result<EnumeratedTestCases, EnumerationError> {
            tests_future_container
                .lock()
                .await
                .get_or_insert_with(|| {
                    // The type must be specified in order to compile.
                    let fetched: PinnedFuture<EnumeratedTestCases, EnumerationError> =
                        Box::pin(fetch(test_component, disabled_tests_future));
                    fetched.shared()
                })
                // This clones the `SharedFuture`.
                .clone()
                .await
        }

        let tests_future_container = self.tests_future_container.clone();
        let disabled_tests_future = self.disabled_tests(test_component.clone());

        get_or_insert_tests_future(test_component, tests_future_container, disabled_tests_future)
    }

    /// Retrieves and memoizes the list of just the disabled tests from the test binary.
    ///
    /// This outer method is _not_ `async`, to avoid capturing a reference to `&self` and fighting
    /// the borrow checker until the end of time.
    fn disabled_tests(
        &self,
        test_component: Arc<Component>,
    ) -> impl Future<Output = Result<EnumeratedTestNames, EnumerationError>> {
        type DisabledTestsFutureContainer =
            MemoizedFutureContainer<EnumeratedTestNames, EnumerationError>;

        /// Fetches the list of disabled tests from the test binary.
        async fn fetch(
            test_component: Arc<Component>,
        ) -> Result<EnumeratedTestNames, EnumerationError> {
            let disabled_tests = get_tests(test_component, TestFilter::DisabledTests)
                .await?
                .into_iter()
                .collect::<HashSet<String>>();
            Ok(Arc::new(disabled_tests))
        }

        /// Populates the given `disabled_tests_future_container` with a future, or returns a copy
        /// of that future if already present.
        async fn get_or_insert_disabled_tests_future(
            test_component: Arc<Component>,
            disabled_tests_future_container: DisabledTestsFutureContainer,
        ) -> Result<EnumeratedTestNames, EnumerationError> {
            disabled_tests_future_container
                .lock()
                .await
                .get_or_insert_with(|| {
                    // The type must be specified.
                    let fetched: PinnedFuture<EnumeratedTestNames, EnumerationError> =
                        Box::pin(fetch(test_component));
                    fetched.shared()
                })
                .clone()
                .await
        }

        let disabled_tests_future_container = self.disabled_tests_future_container.clone();
        get_or_insert_disabled_tests_future(test_component, disabled_tests_future_container)
    }

    /// Returns `true` if the given test is disabled (marked `#[ignore]`) by the developer.
    ///
    /// If the set of disabled tests isn't yet cached, this will retrieve it -- hence `async`.
    async fn is_test_disabled<'a>(
        &'a self,
        test_component: Arc<Component>,
        test_name: &str,
    ) -> Result<bool, EnumerationError> {
        let disabled_tests = self.disabled_tests(test_component).await?;
        Ok(disabled_tests.contains(test_name))
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

    /// Launches a process that actually runs the test and parses the resulting JSON output.
    ///
    /// The mechanism by which Rust tests are launched in individual processes ignores whether a
    /// particular test was marked `#[ignore]`, so this method preemptively checks whether a
    /// the given test is disabled and returns early if the test should be skipped.
    #[cfg(rust_panic = "abort")]
    async fn run_test<'a>(
        &'a self,
        test: &str,
        run_options: &ftest::RunOptions,
        test_component: Arc<Component>,
        test_logger: &mut LogWriter,
    ) -> Result<ftest::Result_, RunTestError> {
        // Exit codes used by Rust's libtest runner.
        const TR_OK: i64 = 50;
        const TR_FAILED: i64 = 51;

        // Rust test binaries launched with `__RUST_TEST_INVOKE` don't care if a test is disabled,
        // so we must manually return early in order to skip a test.
        let skip_disabled_tests = !run_options.include_disabled_tests.unwrap_or(false);
        if skip_disabled_tests && self.is_test_disabled(test_component.clone(), test).await? {
            return Ok(ftest::Result_ { status: Some(ftest::Status::Skipped) });
        }

        let test_invoke = Some(format!("__RUST_TEST_INVOKE={}", test));

        let mut args = vec!["--nocapture".to_owned()];
        args.extend(test_component.args.clone());

        // run test.
        // Load bearing to hold job guard.
        let (process, _job, mut stdlogger) =
            launch_component_process::<RunTestError>(&test_component, args, test_invoke).await?;

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
                    buf.push(newline);
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
                test_logger.write_str("test failed.\n").await?;
                Ok(ftest::Result_ { status: Some(ftest::Status::Failed) })
            }
            other => Err(RunTestError::UnexpectedReturnCode(other)),
        }
    }
}

/// Filter for use in `get_tests`.
enum TestFilter {
    /// List _all_ tests in the test binary.
    AllTests,
    /// List only the disabled tests in the test binary.
    DisabledTests,
}

/// Launches the Rust test binary specified by the given `Component` to retrieve a list of test
/// names.
async fn get_tests(
    test_component: Arc<Component>,
    filter: TestFilter,
) -> Result<Vec<String>, EnumerationError> {
    let mut args = vec!["-Z".to_owned(), "unstable-options".to_owned(), "--list".to_owned()];

    if let TestFilter::DisabledTests = filter {
        args.push("--ignored".to_owned());
    }

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
                tests.push(name.as_str().into());
            }
        }
    }

    Ok(tests)
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
        handle_infos: None,
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
        matches::assert_matches,
        pretty_assertions::assert_eq,
        runner::component::ComponentNamespace,
        runner::component::ComponentNamespaceError,
        std::convert::TryFrom,
        test_runners_lib::cases::TestCaseInfo,
        test_runners_test_lib::{
            assert_event_ord, collect_listener_event, names_to_invocation, ListenerEvent,
        },
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
        let server = TestServer::new();
        let expected: Vec<TestCaseInfo> = vec![
            TestCaseInfo { name: "my_tests::sample_test_one".to_string(), enabled: true },
            TestCaseInfo { name: "my_tests::ignored_failing_test".to_string(), enabled: false },
            TestCaseInfo { name: "my_tests::ignored_passing_test".to_string(), enabled: false },
            TestCaseInfo { name: "my_tests::passing_test".to_string(), enabled: true },
            TestCaseInfo { name: "my_tests::failing_test".to_string(), enabled: true },
            TestCaseInfo { name: "my_tests::sample_test_two".to_string(), enabled: true },
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
        let server = TestServer::new();

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
        let server = TestServer::new();

        let actual_tests: Vec<TestCaseInfo> = server
            .enumerate_tests(component.clone())
            .await?
            .iter()
            .sorted()
            .map(Clone::clone)
            .collect();

        let expected: Vec<TestCaseInfo> = (1..=1000)
            .map(|i| TestCaseInfo { name: format!("test_{}", i), enabled: true })
            .sorted()
            .collect();

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
        let server = TestServer::new();

        let err = server
            .enumerate_tests(component.clone())
            .await
            .expect_err("this function have error-ed out due to non-existent file.");

        assert_matches!(err, EnumerationError::LaunchTest(..));
        let is_valid_error = match &err {
            EnumerationError::LaunchTest(arc) => match **arc {
                launch::LaunchError::LoadInfo(
                    runner::component::LaunchError::LoadingExecutable(_),
                ) => true,
                _ => false,
            },
            _ => false,
        };
        assert!(is_valid_error, "Invalid error: {:?}", err);
        Ok(())
    }

    async fn run_tests(
        invocations: Vec<Invocation>,
        run_options: RunOptions,
    ) -> Result<Vec<ListenerEvent>, anyhow::Error> {
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
        fasync::Task::spawn(async move {
            server
                .serve_test_suite(test_suite, weak_component)
                .await
                .expect("Failed to run test suite")
        })
        .detach();

        suite_proxy
            .run(&mut invocations.into_iter().map(|i| i.into()), run_options, run_listener_client)
            .context("cannot call run")?;

        collect_listener_event(run_listener).await.context("Failed to collect results")
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn run_one_test() -> Result<(), Error> {
        let events =
            run_tests(names_to_invocation(vec!["my_tests::passing_test"]), RunOptions::empty())
                .await
                .unwrap();

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
    async fn run_multiple_tests_exclude_disabled_tests() -> Result<(), Error> {
        let events = run_tests(
            names_to_invocation(vec![
                "my_tests::sample_test_one",
                "my_tests::passing_test",
                "my_tests::failing_test",
                "my_tests::sample_test_two",
                "my_tests::ignored_passing_test",
                "my_tests::ignored_failing_test",
            ]),
            RunOptions { include_disabled_tests: Some(false), parallel: None },
        )
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
            ListenerEvent::start_test("my_tests::ignored_passing_test"),
            ListenerEvent::finish_test(
                "my_tests::ignored_passing_test",
                TestResult { status: Some(Status::Skipped) },
            ),
            ListenerEvent::start_test("my_tests::ignored_failing_test"),
            ListenerEvent::finish_test(
                "my_tests::ignored_failing_test",
                TestResult { status: Some(Status::Skipped) },
            ),
            ListenerEvent::finish_all_test(),
        ];

        assert_eq!(expected_events, events);
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn run_multiple_tests_parallel() -> Result<(), Error> {
        let mut events = run_tests(
            names_to_invocation(vec![
                "my_tests::sample_test_one",
                "my_tests::passing_test",
                "my_tests::failing_test",
                "my_tests::sample_test_two",
                "my_tests::ignored_passing_test",
                "my_tests::ignored_failing_test",
            ]),
            RunOptions { include_disabled_tests: Some(false), parallel: Some(4) },
        )
        .await
        .unwrap();

        let mut expected_events = vec![
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
            ListenerEvent::start_test("my_tests::ignored_passing_test"),
            ListenerEvent::finish_test(
                "my_tests::ignored_passing_test",
                TestResult { status: Some(Status::Skipped) },
            ),
            ListenerEvent::start_test("my_tests::ignored_failing_test"),
            ListenerEvent::finish_test(
                "my_tests::ignored_failing_test",
                TestResult { status: Some(Status::Skipped) },
            ),
            ListenerEvent::finish_all_test(),
        ];

        assert_event_ord(&events);
        expected_events.sort();
        events.sort();

        assert_eq!(expected_events, events);
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn run_multiple_tests_include_disabled_tests() -> Result<(), Error> {
        let events = run_tests(
            names_to_invocation(vec![
                "my_tests::sample_test_two",
                "my_tests::ignored_passing_test",
                "my_tests::ignored_failing_test",
            ]),
            RunOptions { include_disabled_tests: Some(true), parallel: None },
        )
        .await
        .unwrap();

        let expected_events = vec![
            ListenerEvent::start_test("my_tests::sample_test_two"),
            ListenerEvent::finish_test(
                "my_tests::sample_test_two",
                TestResult { status: Some(Status::Passed) },
            ),
            ListenerEvent::start_test("my_tests::ignored_passing_test"),
            ListenerEvent::finish_test(
                "my_tests::ignored_passing_test",
                TestResult { status: Some(Status::Passed) },
            ),
            ListenerEvent::start_test("my_tests::ignored_failing_test"),
            ListenerEvent::finish_test(
                "my_tests::ignored_failing_test",
                TestResult { status: Some(Status::Failed) },
            ),
            ListenerEvent::finish_all_test(),
        ];

        assert_eq!(expected_events, events);
        Ok(())
    }
}
