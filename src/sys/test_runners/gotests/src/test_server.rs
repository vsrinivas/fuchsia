// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    async_trait::async_trait,
    fdio::fdio_sys,
    fidl_fuchsia_process as fproc, fidl_fuchsia_test as ftest,
    ftest::{Invocation, RunListenerProxy},
    fuchsia_async as fasync,
    fuchsia_runtime::{HandleInfo, HandleType},
    fuchsia_zircon as zx,
    futures::{
        future::{abortable, AbortHandle, FutureExt as _},
        lock::Mutex,
        prelude::*,
    },
    log::{debug, error},
    std::{
        str::from_utf8,
        sync::{Arc, Weak},
    },
    test_runners_lib::{
        cases::TestCaseInfo,
        elf::{
            Component, EnumeratedTestCases, KernelError, MemoizedFutureContainer, PinnedFuture,
            SuiteServer,
        },
        errors::*,
        launch,
        logs::{LogStreamReader, LoggerStream},
    },
    zx::HandleBased,
};

/// Implements `fuchsia.test.Suite` and runs provided test.
pub struct TestServer {
    /// Cache to store enumerated tests.
    tests_future_container: MemoizedFutureContainer<EnumeratedTestCases, EnumerationError>,
}

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
        _invocations: Vec<Invocation>,
        _run_options: ftest::RunOptions,
        _test_component: Arc<Component>,
        _run_listener: &RunListenerProxy,
    ) -> Result<(), RunTestError> {
        panic!("Not yet implemented!!!");
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
        Self { tests_future_container: Arc::new(Mutex::new(None)) }
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
}

/// Launches the golang test binary specified by the given `Component` to retrieve a list of test
/// names.
async fn get_tests(test_component: Arc<Component>) -> Result<Vec<String>, EnumerationError> {
    let mut args = vec!["-test.list".to_owned(), ".*".to_owned()];
    args.extend(test_component.args.clone());

    // Load bearing to hold job guard.
    let (process, _job, stdlogger, _stdin_socket) =
        launch_component_process::<EnumerationError>(&test_component, args, None).await?;

    // collect stdout in background before waiting for process termination.
    let std_reader = LogStreamReader::new(stdlogger);

    fasync::OnSignals::new(&process, zx::Signals::PROCESS_TERMINATED)
        .await
        .map_err(KernelError::ProcessExit)?;

    let logs = std_reader.get_logs().await?;
    // TODO(4610): logs might not be utf8, fix the code.
    let mut output = from_utf8(&logs)?;
    let process_info = process.info().map_err(KernelError::ProcessInfo)?;
    if process_info.return_code != 0 {
        // TODO(45858): Add a error logger to API so that we can display test stdout logs.
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
    test_invoke: Option<String>,
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
        environs: test_invoke.map(|test_invoke| vec![test_invoke]),
        handle_infos: Some(handle_infos),
    })
    .await?;
    Ok((p, j, l, client))
}

#[cfg(test)]
mod tests {
    use {
        super::*, anyhow::Error, fidl::endpoints::ClientEnd,
        fidl_fuchsia_component_runner as fcrunner, fidl_fuchsia_io::OPEN_RIGHT_READABLE,
        fuchsia_runtime::job_default, itertools::Itertools, matches::assert_matches,
        pretty_assertions::assert_eq, runner::component::ComponentNamespace,
        runner::component::ComponentNamespaceError, std::convert::TryFrom,
        test_runners_lib::cases::TestCaseInfo,
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
            args: vec![],
            ns: ns,
            job: current_job!(),
        }))
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn enumerate_sample_test() -> Result<(), Error> {
        let component = sample_test_component().unwrap();
        let server = TestServer::new();
        let expected: Vec<TestCaseInfo> = vec![
            TestCaseInfo { name: "TestCrashing".to_string(), enabled: true },
            TestCaseInfo { name: "TestFailing".to_string(), enabled: true },
            TestCaseInfo { name: "TestPassing".to_string(), enabled: true },
            TestCaseInfo { name: "TestSubtests".to_string(), enabled: true },
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
}
