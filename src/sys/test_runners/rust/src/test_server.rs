// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::errors::*,
    fidl_fuchsia_test as ftest,
    fidl_fuchsia_test::{Invocation, RunListenerProxy},
    fuchsia_async as fasync, fuchsia_zircon as zx,
    fuchsia_zircon_sys::ZX_CHANNEL_MAX_MSG_BYTES,
    futures::future::abortable,
    futures::future::AbortHandle,
    futures::prelude::*,
    log::{debug, error, info},
    regex::Regex,
    rust_measure_tape_for_case::measure,
    std::{
        str::from_utf8,
        sync::{Arc, Weak},
    },
    test_runners_lib::{
        elf_component::{Component, SuiteServer},
        LogError, LogStreamReader, LogWriter,
    },
    thiserror::Error,
};

/// Error encountered while working fidl lib.
#[derive(Debug, Error)]
pub enum FidlError {
    #[error("cannot convert client end to proxy: {:?}", _0)]
    ClientEndToProxy(fidl::Error),

    #[error("cannot create fidl proxy: {:?}", _0)]
    CreateProxy(fidl::Error),
}

/// Error encountered while working with kernel object.
#[derive(Debug, Error)]
pub enum KernelError {
    #[error("job creation failed: {:?}", _0)]
    CreateJob(zx::Status),

    #[error("error waiting for test process to exit: {:?}", _0)]
    ProcessExit(zx::Status),

    #[error("error getting info from process: {:?}", _0)]
    ProcessInfo(zx::Status),

    #[error("error creating socket: {:?}", _0)]
    CreateSocket(zx::Status),

    #[error("cannot convert zircon socket to async socket: {:?}", _0)]
    SocketToAsync(zx::Status),
}

/// Implements `fuchsia.test.Suite` and runs provided test.
pub struct TestServer {
    /// Cache to store enumerated test names.
    test_list: Option<Vec<String>>,
}

impl SuiteServer for TestServer {
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
            test_runners_lib::launch_process(test_runners_lib::LaunchProcessArgs {
                bin_path: &test_component.binary,
                process_name: &test_component.name,
                job: Some(
                    test_component.job.create_child_job().map_err(KernelError::CreateJob).unwrap(),
                ),
                ns: test_component.ns.clone().map_err(NamespaceError::Clone)?,
                args: Some(args),
                name_infos: None,
                environs: Some(vec![test_invoke]),
            })
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

    /// Runs requested tests and sends test events to the given listener.
    // TODO(45852): Support disabled tests.
    pub async fn run_tests(
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

    /// Lauches a process that lists the tests without actually running any of them. It then parses
    /// the output of that process into a vector of strings.
    ///
    /// Example output for rust test process:
    ///
    /// tests::purposefully_failing_test: test
    /// tests::test_full_path: test
    /// tests::test_minimal_path: test
    ///
    /// 3 tests, 0 benchmarks
    ///
    async fn enumerate_tests(
        &mut self,
        test_component: Arc<Component>,
    ) -> Result<Vec<String>, EnumerationError> {
        if let Some(t) = &self.test_list {
            return Ok(t.clone());
        }

        let mut args = vec!["-Z".to_owned(), "unstable-options".to_owned(), "--list".to_owned()];
        args.extend(test_component.args.clone());

        // Load bearing to hold job guard.
        let (process, _job, stdlogger) =
            test_runners_lib::launch_process(test_runners_lib::LaunchProcessArgs {
                bin_path: &test_component.binary,
                process_name: &test_component.name,
                job: Some(
                    test_component.job.create_child_job().map_err(KernelError::CreateJob).unwrap(),
                ),
                ns: test_component.ns.clone().map_err(NamespaceError::Clone)?,
                args: Some(args),
                name_infos: None,
                environs: None,
            })
            .await?;

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

        let mut test_names = vec![];
        let regex = Regex::new(r"^(.*): test$").unwrap();

        for test in output.split("\n") {
            if let Some(capture) = regex.captures(test) {
                if let Some(name) = capture.get(1) {
                    test_names.push(String::from(name.as_str()));
                }
            }
        }

        self.test_list = Some(test_names.clone());

        return Ok(test_names);
    }

    /// Implements `fuchsia.test.Suite` service and runs test.
    pub async fn serve_test_suite(
        mut self,
        mut stream: ftest::SuiteRequestStream,
        component: Weak<Component>,
    ) -> Result<(), SuiteServerError> {
        while let Some(event) = stream.try_next().await.map_err(SuiteServerError::Stream)? {
            match event {
                ftest::SuiteRequest::GetTests { iterator, control_handle: _ } => {
                    let component = component.upgrade();
                    if component.is_none() {
                        // no component object, return, test has ended, channel would be closed shortly.
                        break;
                    }
                    let mut stream = iterator.into_stream().map_err(SuiteServerError::Stream)?;
                    let tests = self.enumerate_tests(component.unwrap()).await?;

                    fasync::spawn(
                        async move {
                            let mut iter =
                                tests.into_iter().map(|name| ftest::Case { name: Some(name) });
                            while let Some(ftest::CaseIteratorRequest::GetNext { responder }) =
                                stream.try_next().await?
                            {
                                // Paginate cases
                                let mut bytes_used: usize = 32; // Page overhead of message header + vector
                                let mut case_count = 0;
                                for case in iter.clone() {
                                    bytes_used += measure(&case).num_bytes;
                                    if bytes_used > ZX_CHANNEL_MAX_MSG_BYTES as usize {
                                        break;
                                    }
                                    case_count += 1;
                                }
                                responder
                                    .send(&mut iter.by_ref().take(case_count))
                                    .map_err(SuiteServerError::Response)?;
                            }
                            Ok(())
                        }
                        .unwrap_or_else(|e: anyhow::Error| error!("error serving tests: {:?}", e)),
                    );
                }
                ftest::SuiteRequest::Run { tests, options: _, listener, .. } => {
                    let component = component.upgrade();
                    if component.is_none() {
                        // no component object, return, test has ended, channel would be closed shortly.
                        break;
                    }

                    let listener =
                        listener.into_proxy().map_err(FidlError::ClientEndToProxy).unwrap();

                    self.run_tests(tests, component.unwrap(), &listener).await?;
                    listener.on_finished().map_err(RunTestError::SendFinishAllTests).unwrap();
                }
            }
        }
        Ok(())
    }
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
        runner::component::ComponentNamespace,
        runner::component::ComponentNamespaceError,
        std::convert::TryFrom,
        test_runners_lib::*,
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
        let mut expected = vec![
            "my_tests::sample_test_one",
            "my_tests::passing_test",
            "my_tests::failing_test",
            "my_tests::sample_test_two",
        ]
        .into_iter()
        .map(|s| s.to_owned())
        .collect::<Vec<String>>();
        let mut actual = server.enumerate_tests(component.clone()).await?;

        expected.sort();
        actual.sort();
        assert_eq!(expected, actual);

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

        assert_eq!(server.enumerate_tests(component.clone()).await?, Vec::<String>::new());

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

        let mut actual_tests = server.enumerate_tests(component.clone()).await?;

        actual_tests.sort();

        let mut expected: Vec<String> = (1..=1000).map(|i| format!("test_{}", i)).collect();
        expected.sort();
        assert_eq!(expected, actual_tests);

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
            EnumerationError::LaunchTest(LaunchError::LoadInfo(
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
