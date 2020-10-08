// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    async_trait::async_trait,
    fdio::fdio_sys,
    fidl_fuchsia_process as fproc,
    fidl_fuchsia_test::{
        self as ftest, Invocation, Result_ as TestResult, RunListenerProxy, Status,
    },
    fuchsia_async as fasync,
    fuchsia_runtime::{HandleInfo, HandleType},
    fuchsia_zircon as zx,
    futures::{
        future::{abortable, AbortHandle, FutureExt as _},
        lock::Mutex,
        prelude::*,
        TryStreamExt,
    },
    lazy_static::lazy_static,
    log::{debug, error},
    regex::bytes::Regex,
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
    zx::HandleBased,
};

/// Implements `fuchsia.test.Suite` and runs provided test.
pub struct TestServer {
    /// Cache to store enumerated tests.
    tests_future_container: MemoizedFutureContainer<EnumeratedTestCases, EnumerationError>,
}

/// Default concurrency for running test cases in parallel.
static PARALLEL_DEFAULT: u16 = 10;

#[async_trait]
impl SuiteServer for TestServer {
    /// Launches a process that lists the tests without actually running any of them. It then parses
    /// the output of that process into a vector of strings.
    ///
    /// Example output for go test process:
    ///
    /// ```text
    /// TestPassing
    /// TestFailing
    /// TestCrashing
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
        let num_parallel =
            Self::get_parallel_count(run_options.parallel.unwrap_or(PARALLEL_DEFAULT));

        let invocations = stream::iter(invocations);
        invocations
            .map(Ok)
            .try_for_each_concurrent(num_parallel, |invocation| {
                self.run_test(
                    invocation,
                    test_component.clone(),
                    run_listener,
                    num_parallel,
                    run_options.arguments.clone(),
                )
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

lazy_static! {
    static ref RESTRICTED_FLAGS: HashSet<&'static str> =
        vec!["-test.run", "-test.v", "-test.parallel"].into_iter().collect();
}

impl TestServer {
    /// Creates new test server.
    /// Clients should call this function to create new object and then call `serve_test_suite`.
    pub fn new() -> Self {
        Self { tests_future_container: Arc::new(Mutex::new(None)) }
    }

    pub fn validate_args(args: &Vec<String>) -> Result<(), ArgumentError> {
        let restricted_flags = args
            .iter()
            .filter(|arg| {
                return RESTRICTED_FLAGS.contains(arg.as_str());
            })
            .map(|s| s.clone())
            .collect::<Vec<_>>()
            .join(", ");
        if restricted_flags.len() > 0 {
            return Err(ArgumentError::RestrictedArg(restricted_flags));
        }
        Ok(())
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
        async fn fetch(
            test_component: Arc<Component>,
        ) -> Result<EnumeratedTestCases, EnumerationError> {
            let test_names = get_tests(test_component).await?;
            let tests: Vec<TestCaseInfo> =
                test_names.into_iter().map(|name| TestCaseInfo { name, enabled: true }).collect();
            Ok(Arc::new(tests))
        }

        /// Populates the given `tests_future_container` with a future, or returns a copy of that
        /// future if already present.
        async fn get_or_insert_tests_future(
            test_component: Arc<Component>,
            tests_future_container: MemoizedFutureContainer<EnumeratedTestCases, EnumerationError>,
        ) -> Result<EnumeratedTestCases, EnumerationError> {
            tests_future_container
                .lock()
                .await
                .get_or_insert_with(|| {
                    // The type must be specified in order to compile.
                    let fetched: PinnedFuture<EnumeratedTestCases, EnumerationError> =
                        Box::pin(fetch(test_component));
                    fetched.shared()
                })
                // This clones the `SharedFuture`.
                .clone()
                .await
        }

        let tests_future_container = self.tests_future_container.clone();
        get_or_insert_tests_future(test_component, tests_future_container)
    }

    async fn run_test<'a>(
        &'a self,
        invocation: Invocation,
        component: Arc<Component>,
        run_listener: &'a RunListenerProxy,
        parallel: usize,
        test_args: Option<Vec<String>>,
    ) -> Result<(), RunTestError> {
        let test = invocation.name.as_ref().ok_or(RunTestError::TestCaseName)?.to_string();
        debug!("Running test {}", test);
        let user_passed_args = test_args.unwrap_or(vec![]);

        let (test_logger, log_client) =
            zx::Socket::create(zx::SocketOpts::STREAM).map_err(KernelError::CreateSocket).unwrap();

        let (case_listener_proxy, listener) =
            fidl::endpoints::create_proxy::<fidl_fuchsia_test::CaseListenerMarker>()
                .map_err(FidlError::CreateProxy)
                .unwrap();

        run_listener
            .on_test_case_started(invocation, log_client, listener)
            .map_err(RunTestError::SendStart)?;

        let test_logger =
            fasync::Socket::from_socket(test_logger).map_err(KernelError::SocketToAsync).unwrap();
        let mut test_logger = LogWriter::new(test_logger);

        if let Err(e) = TestServer::validate_args(&user_passed_args) {
            test_logger.write_str(&format!("{}", e)).await?;
            case_listener_proxy
                .finished(TestResult { status: Some(Status::Failed) })
                .map_err(RunTestError::SendFinish)?;
            return Ok(());
        }
        let mut args = vec![
            "-test.run".to_owned(),
            format!("^{}$", test),
            "-test.parallel".to_owned(),
            parallel.to_string(),
            "-test.v".to_owned(),
        ];
        args.extend(component.args.clone());
        args.extend(user_passed_args);

        // run test.
        // Load bearing to hold job guard.
        let (process, _job, mut stdlogger, _stdin_socket) =
            launch_component_process::<RunTestError>(&component, args).await?;

        let mut buffer = vec![];
        const NEWLINE: u8 = b'\n';
        const BUF_THRESHOLD: usize = 2048;
        let test_start_re = Regex::new(&format!(r"^=== RUN\s+{}$", test)).unwrap();
        let test_end_re = Regex::new(&format!(r"^\s*--- (\w*?): {} \(.*\)$", test)).unwrap();
        let mut skipped = false;
        while let Some(bytes) = stdlogger.try_next().await.map_err(LogError::Read)? {
            if bytes.is_empty() {
                continue;
            }

            let is_last_byte_newline = *bytes.last().unwrap() == NEWLINE;
            let mut iter = bytes.split(|&x| x == NEWLINE).peekable();
            while let Some(b) = iter.next() {
                if iter.peek() == None && b.len() == 0 {
                    continue;
                }
                buffer.extend_from_slice(b);

                if buffer.len() >= BUF_THRESHOLD {
                    if iter.peek() != None || is_last_byte_newline {
                        buffer.push(NEWLINE)
                    }
                    test_logger.write(&buffer).await?;
                    buffer.clear();
                    continue;
                } else if buffer.len() < BUF_THRESHOLD
                    && !is_last_byte_newline
                    && iter.peek() == None
                {
                    // last part of split without a newline, so skip printing or matching and store
                    // it in buffer for next iteration.
                    break;
                }
                if iter.peek() == Some(&"".as_bytes()) && (buffer == b"PASS" || buffer == b"FAIL") {
                    // end of test, do nothing, no need to print it
                } else if test_start_re.is_match(&buffer) {
                    // start of test, do nothing, no need to print it
                } else if let Some(capture) = test_end_re.captures(&buffer) {
                    if capture.get(1).unwrap().as_bytes() == b"SKIP" {
                        skipped = true;
                    }
                } else {
                    if iter.peek() != None || is_last_byte_newline {
                        buffer.push(NEWLINE)
                    }
                    test_logger.write(&buffer).await?;
                }
                buffer.clear()
            }
        }

        if buffer.len() > 0 {
            test_logger.write(&buffer).await?;
        }
        buffer.clear();
        debug!("Waiting for test to finish: {}", test);

        // wait for test to end.
        fasync::OnSignals::new(&process, zx::Signals::PROCESS_TERMINATED)
            .await
            .map_err(KernelError::ProcessExit)
            .unwrap();
        let process_info = process.info().map_err(RunTestError::ProcessInfo)?;

        // gotest returns 0 is test succeeds and 1 if test fails. This will check if test ended
        // abnormally.
        if process_info.return_code != 0 && process_info.return_code != 1 {
            test_logger.write_str("Test exited abnormally\n").await?;
            case_listener_proxy
                .finished(TestResult { status: Some(Status::Failed) })
                .map_err(RunTestError::SendFinish)?;
            return Ok(());
        }

        let status = if skipped {
            Status::Skipped
        } else if process_info.return_code != 0 {
            Status::Failed
        } else {
            Status::Passed
        };
        case_listener_proxy
            .finished(TestResult { status: Some(status) })
            .map_err(RunTestError::SendFinish)?;
        debug!("test finish {}", test);
        Ok(())
    }
}

/// Launches the golang test binary specified by the given `Component` to retrieve a list of test
/// names.
async fn get_tests(test_component: Arc<Component>) -> Result<Vec<String>, EnumerationError> {
    let mut args = vec!["-test.list".to_owned(), ".*".to_owned()];
    args.extend(test_component.args.clone());

    // Load bearing to hold job guard.
    let (process, _job, stdlogger, _stdin_socket) =
        launch_component_process::<EnumerationError>(&test_component, args).await?;

    // collect stdout in background before waiting for process termination.
    let std_reader = LogStreamReader::new(stdlogger);

    fasync::OnSignals::new(&process, zx::Signals::PROCESS_TERMINATED)
        .await
        .map_err(KernelError::ProcessExit)?;

    let logs = std_reader.get_logs().await?;
    // TODO(fxbug.dev/4610): logs might not be utf8, fix the code.
    let mut output = from_utf8(&logs)?;
    let process_info = process.info().map_err(KernelError::ProcessInfo)?;
    if process_info.return_code != 0 {
        // TODO(fxbug.dev/45858): Add a error logger to API so that we can display test stdout logs.
        error!("Failed getting list of tests:\n{}", output);
        return Err(EnumerationError::ListTest);
    }
    output = output.trim();
    let tests = if !output.is_empty() {
        output.split("\n").into_iter().map(|t| t.into()).collect()
    } else {
        vec![]
    };
    Ok(tests)
}

/// Convenience wrapper around [`launch::launch_process`].
async fn launch_component_process<E>(
    component: &Component,
    args: Vec<String>,
) -> Result<(zx::Process, launch::ScopedJob, LoggerStream, zx::Socket), E>
where
    E: From<NamespaceError> + From<launch::LaunchError>,
{
    // TODO(fxbug.dev/58076): Golang binary fails if it is not provided with a stdin.
    // Provide it till the issue is fixed.
    let (client, log) =
        zx::Socket::create(zx::SocketOpts::STREAM).map_err(launch::LaunchError::CreateSocket)?;
    let mut handle_infos = vec![];
    unsafe {
        const STDIN: u16 = 0;
        let mut stdin_fd: i32 = -1;

        let mut stdin_file_handle = zx::sys::ZX_HANDLE_INVALID;
        let status = fdio::fdio_sys::fdio_fd_create(log.into_raw(), &mut stdin_fd);
        if let Err(s) = zx::Status::ok(status) {
            return Err(launch::LaunchError::Fdio(FdioError::Create(s)).into());
        }
        let status = fdio_sys::fdio_fd_transfer(
            stdin_fd,
            &mut stdin_file_handle as *mut zx::sys::zx_handle_t,
        );
        if let Err(s) = zx::Status::ok(status) {
            return Err(launch::LaunchError::Fdio(FdioError::Clone(s)).into());
        }
        handle_infos.push(fproc::HandleInfo {
            handle: zx::Handle::from_raw(stdin_file_handle),
            id: HandleInfo::new(HandleType::FileDescriptor, STDIN).as_raw(),
        });
    }

    let (p, j, l) = launch::launch_process(launch::LaunchProcessArgs {
        bin_path: &component.binary,
        process_name: &component.name,
        job: Some(component.job.create_child_job().map_err(KernelError::CreateJob).unwrap()),
        ns: component.ns.clone().map_err(NamespaceError::Clone)?,
        args: Some(args),
        name_infos: None,
        environs: None,
        handle_infos: Some(handle_infos),
    })
    .await?;
    Ok((p, j, l, client))
}

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

    fn create_ns_from_raw_ns(
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
        let ns = create_ns_from_raw_ns(vec![("/pkg", OPEN_RIGHT_READABLE)])?;

        Ok(Arc::new(Component {
            url: "fuchsia-pkg://fuchsia.com/sample_test#test.cm".to_owned(),
            name: "test/sample_go_test".to_owned(),
            binary: "test/sample_go_test".to_owned(),
            args: vec!["-my_custom_flag".to_string()],
            ns: ns,
            job: current_job!(),
        }))
    }

    #[test]
    fn validate_args_test() {
        // choose a subset of restricted flags and run them through validation function.
        let restricted_flags = vec!["-test.v", "-test.run", "-test.parallel"];

        for flag in restricted_flags {
            let args = vec![flag.to_string()];
            let err = TestServer::validate_args(&args)
                .expect_err(&format!("should error out for flag: {}", flag));
            match err {
                ArgumentError::RestrictedArg(f) => assert_eq!(f, flag),
            }
        }

        let allowed_flags = vec!["-test.short", "-test.anyflag", "-test.timeout", "-mycustomflag"];

        for flag in allowed_flags {
            let args = vec![flag.to_string()];
            TestServer::validate_args(&args)
                .expect(&format!("should not error out for flag: {}", flag));
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn enumerate_sample_test() -> Result<(), Error> {
        let component = sample_test_component().unwrap();
        let server = TestServer::new();
        let expected: Vec<TestCaseInfo> = vec![
            TestCaseInfo { name: "TestCrashing".to_string(), enabled: true },
            TestCaseInfo { name: "TestFailing".to_string(), enabled: true },
            TestCaseInfo { name: "TestPassing".to_string(), enabled: true },
            TestCaseInfo { name: "TestPrefix".to_string(), enabled: true },
            TestCaseInfo { name: "TestPrefixExtra".to_string(), enabled: true },
            TestCaseInfo { name: "TestPrintMultiline".to_string(), enabled: true },
            TestCaseInfo { name: "TestSkipped".to_string(), enabled: true },
            TestCaseInfo { name: "TestSubtests".to_string(), enabled: true },
            TestCaseInfo { name: "TestCustomArg".to_string(), enabled: true },
            TestCaseInfo { name: "TestCustomArg2".to_string(), enabled: true },
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
        let ns = create_ns_from_raw_ns(vec![("/pkg", OPEN_RIGHT_READABLE)])?;

        let component = Arc::new(Component {
            url: "fuchsia-pkg://fuchsia.com/sample_test#test.cm".to_owned(),
            name: "test/empty_go_test".to_owned(),
            binary: "test/empty_go_test".to_owned(),
            args: vec![],
            ns: ns,
            job: current_job!(),
        });
        let server = TestServer::new();

        assert_eq!(*server.enumerate_tests(component.clone()).await?, Vec::<TestCaseInfo>::new());

        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn enumerate_invalid_file() -> Result<(), Error> {
        let ns = create_ns_from_raw_ns(vec![("/pkg", OPEN_RIGHT_READABLE)])?;

        let component = Arc::new(Component {
            url: "fuchsia-pkg://fuchsia.com/sample_test#test.cm".to_owned(),
            name: "test/invalid".to_owned(),
            binary: "test/invalid".to_owned(),
            args: vec![],
            ns: ns,
            job: current_job!(),
        });
        let server = TestServer::new();

        let err = server
            .enumerate_tests(component.clone())
            .await
            .expect_err("this function should have error-ed out due to non-existent file.");

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
    async fn run_multiple_tests() -> Result<(), Error> {
        fuchsia_syslog::init_with_tags(&["gtest_runner_test"]).expect("cannot init logger");
        let events = run_tests(
            names_to_invocation(vec![
                "TestCrashing",
                "TestPassing",
                "TestFailing",
                "TestPrefix",
                "TestSkipped",
                "TestPrefixExtra",
                "TestCustomArg",
                "TestCustomArg2",
            ]),
            RunOptions {
                include_disabled_tests: Some(false),
                parallel: Some(1),
                arguments: Some(vec!["-my_custom_flag_2".to_owned()]),
            },
        )
        .await
        .unwrap();

        let expected_events = vec![
            ListenerEvent::start_test("TestCrashing"),
            ListenerEvent::finish_test("TestCrashing", TestResult { status: Some(Status::Failed) }),
            ListenerEvent::start_test("TestPassing"),
            ListenerEvent::finish_test("TestPassing", TestResult { status: Some(Status::Passed) }),
            ListenerEvent::start_test("TestFailing"),
            ListenerEvent::finish_test("TestFailing", TestResult { status: Some(Status::Failed) }),
            ListenerEvent::start_test("TestPrefix"),
            ListenerEvent::finish_test("TestPrefix", TestResult { status: Some(Status::Passed) }),
            ListenerEvent::start_test("TestSkipped"),
            ListenerEvent::finish_test("TestSkipped", TestResult { status: Some(Status::Skipped) }),
            ListenerEvent::start_test("TestPrefixExtra"),
            ListenerEvent::finish_test(
                "TestPrefixExtra",
                TestResult { status: Some(Status::Passed) },
            ),
            ListenerEvent::start_test("TestCustomArg"),
            ListenerEvent::finish_test(
                "TestCustomArg",
                TestResult { status: Some(Status::Passed) },
            ),
            ListenerEvent::start_test("TestCustomArg2"),
            ListenerEvent::finish_test(
                "TestCustomArg2",
                TestResult { status: Some(Status::Passed) },
            ),
            ListenerEvent::finish_all_test(),
        ];

        assert_eq!(expected_events, events);
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn run_multiple_tests_parallel() -> Result<(), Error> {
        fuchsia_syslog::init_with_tags(&["gtest_runner_test"]).expect("cannot init logger");
        let mut events = run_tests(
            names_to_invocation(vec![
                "TestCrashing",
                "TestPassing",
                "TestFailing",
                "TestPrefix",
                "TestSkipped",
                "TestPrefixExtra",
            ]),
            RunOptions { include_disabled_tests: Some(false), parallel: Some(4), arguments: None },
        )
        .await
        .unwrap();

        let mut expected_events = vec![
            ListenerEvent::start_test("TestCrashing"),
            ListenerEvent::finish_test("TestCrashing", TestResult { status: Some(Status::Failed) }),
            ListenerEvent::start_test("TestPassing"),
            ListenerEvent::finish_test("TestPassing", TestResult { status: Some(Status::Passed) }),
            ListenerEvent::start_test("TestFailing"),
            ListenerEvent::finish_test("TestFailing", TestResult { status: Some(Status::Failed) }),
            ListenerEvent::start_test("TestPrefix"),
            ListenerEvent::finish_test("TestPrefix", TestResult { status: Some(Status::Passed) }),
            ListenerEvent::start_test("TestSkipped"),
            ListenerEvent::finish_test("TestSkipped", TestResult { status: Some(Status::Skipped) }),
            ListenerEvent::start_test("TestPrefixExtra"),
            ListenerEvent::finish_test(
                "TestPrefixExtra",
                TestResult { status: Some(Status::Passed) },
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
    async fn run_no_test() -> Result<(), Error> {
        let events = run_tests(
            vec![],
            RunOptions { include_disabled_tests: Some(false), parallel: Some(1), arguments: None },
        )
        .await
        .unwrap();

        let expected_events = vec![ListenerEvent::finish_all_test()];

        assert_eq!(expected_events, events);
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn run_one_test() -> Result<(), Error> {
        let events = run_tests(
            names_to_invocation(vec!["TestPassing"]),
            RunOptions { include_disabled_tests: Some(false), parallel: Some(1), arguments: None },
        )
        .await
        .unwrap();

        let expected_events = vec![
            ListenerEvent::start_test("TestPassing"),
            ListenerEvent::finish_test("TestPassing", TestResult { status: Some(Status::Passed) }),
            ListenerEvent::finish_all_test(),
        ];

        assert_eq!(expected_events, events);

        Ok(())
    }
}
