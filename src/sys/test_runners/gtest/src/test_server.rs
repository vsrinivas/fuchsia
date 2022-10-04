// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    async_trait::async_trait,
    fidl::endpoints::Proxy,
    fidl_fuchsia_io as fio, fidl_fuchsia_process as fproc,
    fidl_fuchsia_test::{
        self as ftest, Invocation, Result_ as TestResult, RunListenerProxy, Status,
    },
    fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::{
        future::{abortable, join, AbortHandle, Future, FutureExt as _},
        lock::Mutex,
        prelude::*,
        TryStreamExt,
    },
    lazy_static::lazy_static,
    serde::{Deserialize, Serialize},
    std::{
        num::NonZeroUsize,
        path::Path,
        str::from_utf8,
        sync::{Arc, Weak},
    },
    test_runners_lib::{
        cases::TestCaseInfo,
        elf::{
            Component, ComponentError, EnumeratedTestCases, FidlError, KernelError,
            MemoizedFutureContainer, PinnedFuture, SuiteServer,
        },
        errors::*,
        launch,
        logs::{LogError, LogStreamReader, LoggerStream, SocketLogWriter},
    },
    tracing::{debug, error, info, warn},
};

/// In `gtest_list_test` output, provides info about individual test cases.
/// Example: For test FOO.Bar, this contains info about Bar.
/// Please refer to documentation of `ListTestResult` for details.
#[derive(Serialize, Deserialize, Debug)]
struct IndividualTestInfo {
    pub name: String,
    pub file: String,
    pub line: u64,
}

/// In `gtest_list_test` output, provides info about individual test suites.
/// Example: For test FOO.Bar, this contains info about FOO.
/// Please refer to documentation of `ListTestResult` for details.
#[derive(Serialize, Deserialize, Debug)]
struct TestSuiteResult {
    pub tests: usize,
    pub name: String,
    pub testsuite: Vec<IndividualTestInfo>,
}

/// Structure of the output of `<test binary> --gtest_list_test`.
///
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
///          "file": "../../src/sys/test_runners/gtest/test_data/sample_tests.cc",
///          "line": 7
///        },
///        {
///          "name": "Test2",
///          "file": "../../src/sys/test_runners/gtest/test_data/sample_tests.cc",
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

/// Provides info about test case failure if any.
#[derive(Serialize, Deserialize, Debug)]
struct Failure {
    pub failure: String,
}

/// Provides info about individual test executions.
/// Example: For test FOO.Bar, this contains info about Bar.
/// Please refer to documentation of `TestOutput` for details.
#[derive(Serialize, Deserialize, Debug)]
struct IndividualTestOutput {
    pub name: String,
    pub status: IndividualTestOutputStatus,
    pub time: String,
    pub failures: Option<Vec<Failure>>,
    /// This field is not documented, so using String. We can use serde_enum_str to convert it to
    /// enum, but that is not in our third party crates.
    /// Most common values seen in the output are COMPLETED, SKIPPED, SUPPRESSED
    pub result: String,
}

/// Describes whether a test was run or skipped.
///
/// Refer to [`TestSuiteOutput`] documentation for schema details.
#[derive(Serialize, Deserialize, Debug)]
#[serde(rename_all = "UPPERCASE")]
enum IndividualTestOutputStatus {
    Run,
    NotRun,
}

/// Provides info about individual test suites.
/// Refer to [gtest documentation] for output structure.
/// [gtest documentation]: https://github.com/google/googletest/blob/2002f267f05be6f41a3d458954414ba2bfa3ff1d/googletest/docs/advanced.md#generating-a-json-report
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
/// Please refer to documentation of `TestSuiteOutput` for details.
#[derive(Serialize, Deserialize, Debug)]
struct TestOutput {
    pub testsuites: Vec<TestSuiteOutput>,
}

const DYNAMIC_SKIP_RESULT: &str = "SKIPPED";

/// Opens and reads file defined by `path` in `dir`.
async fn read_file(dir: &fio::DirectoryProxy, path: &Path) -> Result<String, anyhow::Error> {
    // Open the file in read-only mode.
    let result_file_proxy = fuchsia_fs::open_file(dir, path, fio::OpenFlags::RIGHT_READABLE)?;
    return fuchsia_fs::read_file(&result_file_proxy).await;
}

/// Implements `fuchsia.test.Suite` and runs provided test.
pub struct TestServer {
    /// Directory where test data(json) is written by gtest.
    ///
    /// Note: Although `DirectoryProxy` is `Clone`able, it is not `Sync + Send`, so it has to be
    /// wrapped in an `Arc`.
    output_dir_proxy: Arc<fio::DirectoryProxy>,

    /// Output directory name.
    output_dir_name: String,

    /// Output directory's parent path.
    output_dir_parent_path: String,

    /// Cache to store enumerated tests.
    tests_future_container: MemoizedFutureContainer<EnumeratedTestCases, EnumerationError>,
}

static PARALLEL_DEFAULT: u16 = 1;

#[async_trait]
impl SuiteServer for TestServer {
    /// Launches test process and gets test list out. Returns list of tests names in the format
    /// defined by gtests, i.e FOO.Bar.
    /// It only runs enumeration logic once, caches and returns the same result back on subsequent
    /// calls.
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
        component: Arc<Component>,
        run_listener: &RunListenerProxy,
    ) -> Result<(), RunTestError> {
        let num_parallel =
            Self::get_parallel_count(run_options.parallel.unwrap_or(PARALLEL_DEFAULT));

        let invocations = stream::iter(invocations);
        invocations
            .map(Ok)
            .try_for_each_concurrent(num_parallel, |invocation| {
                self.run_test(invocation, &run_options, component.clone(), run_listener)
            })
            .await
    }

    /// Run this server.
    fn run(
        self,
        weak_component: Weak<Component>,
        test_url: &str,
        stream: ftest::SuiteRequestStream,
    ) -> AbortHandle {
        let test_data_name = self.output_dir_name.clone();
        let test_data_parent = self.output_dir_parent_path.clone();
        let test_url = test_url.to_owned();
        let (fut, test_suite_abortable_handle) =
            abortable(self.serve_test_suite(stream, weak_component.clone()));

        fasync::Task::local(async move {
            match fut.await {
                Ok(result) => {
                    if let Err(e) = result {
                        error!("server failed for test {}: {:?}", test_url, e);
                    }
                }
                Err(e) => debug!("server aborted for test {}: {:?}", test_url, e),
            }
            debug!("Done running server for {}.", test_url);

            // Even if `serve_test_suite` failed, clean local data directory as these files are no
            // longer needed and they are consuming space.
            let test_data_dir = fuchsia_fs::directory::open_in_namespace(
                &test_data_parent,
                fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
            )
            .expect("Cannot open data directory");
            if let Err(e) =
                fuchsia_fs::directory::remove_dir_recursive(&test_data_dir, &test_data_name).await
            {
                debug!(
                    "cannot delete temp data dir '{}/{}': {:?}",
                    test_data_parent, test_data_name, e
                );
            }
        })
        .detach();
        test_suite_abortable_handle
    }
}

const NEWLINE: u8 = b'\n';
const PREFIXES_TO_EXCLUDE: [&[u8]; 10] = [
    "Note: Google Test filter".as_bytes(),
    " 1 FAILED TEST".as_bytes(),
    "  YOU HAVE 1 DISABLED TEST".as_bytes(),
    "[==========]".as_bytes(),
    "[----------]".as_bytes(),
    "[ RUN      ]".as_bytes(),
    "[  PASSED  ]".as_bytes(),
    "[  FAILED  ]".as_bytes(),
    "[  SKIPPED ]".as_bytes(),
    "[       OK ]".as_bytes(),
];
const SOCKET_BUFFER_SIZE: usize = 4096;

#[cfg(feature = "gtest")]
macro_rules! test_flag {
    ($f: expr) => {
        concat!("--gtest_", $f)
    };
}

#[cfg(feature = "gunit")]
macro_rules! test_flag {
    ($f: expr) => {
        concat!("--gunit_", $f)
    };
}

lazy_static! {
    static ref RESTRICTED_FLAGS: Vec<&'static str> = vec![
        test_flag!("filter"),
        test_flag!("output"),
        test_flag!("also_run_disabled_tests"),
        test_flag!("list_tests"),
        test_flag!("repeat")
    ];
}

impl TestServer {
    /// Creates new test server.
    pub fn new(
        output_dir_proxy: fio::DirectoryProxy,
        output_dir_name: String,
        output_dir_parent_path: String,
    ) -> Self {
        Self {
            output_dir_proxy: Arc::new(output_dir_proxy),
            output_dir_name: output_dir_name,
            output_dir_parent_path: output_dir_parent_path,
            tests_future_container: Arc::new(Mutex::new(None)),
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
        async fn fetch(
            test_component: Arc<Component>,
            test_data_namespace: Result<fproc::NameInfo, IoError>,
            output_dir_proxy: Arc<fio::DirectoryProxy>,
        ) -> Result<EnumeratedTestCases, EnumerationError> {
            Ok(Arc::new(get_tests(test_component, test_data_namespace?, output_dir_proxy).await?))
        }

        /// Populates the given `tests_future_container` with a future, or returns a copy of that
        /// future if already present.
        async fn get_or_insert_tests_future(
            test_component: Arc<Component>,
            tests_future_container: MemoizedFutureContainer<EnumeratedTestCases, EnumerationError>,
            test_data_namespace: Result<fproc::NameInfo, IoError>,
            output_dir_proxy: Arc<fio::DirectoryProxy>,
        ) -> Result<EnumeratedTestCases, EnumerationError> {
            tests_future_container
                .lock()
                .await
                .get_or_insert_with(|| {
                    let fetched: PinnedFuture<EnumeratedTestCases, EnumerationError> =
                        Box::pin(fetch(test_component, test_data_namespace, output_dir_proxy));
                    fetched.shared()
                })
                .clone()
                .await
        }

        let tests_future_container = self.tests_future_container.clone();
        let test_data_namespace = self.test_data_namespace();
        let output_dir_proxy = self.output_dir_proxy.clone();

        get_or_insert_tests_future(
            test_component,
            tests_future_container,
            test_data_namespace,
            output_dir_proxy,
        )
    }

    fn test_data_namespace(&self) -> Result<fproc::NameInfo, IoError> {
        let client_channnel =
            fuchsia_fs::clone_directory(&self.output_dir_proxy, fio::OpenFlags::CLONE_SAME_RIGHTS)
                .map_err(IoError::CloneProxy)?
                .into_channel()
                .map_err(|_| FidlError::ProxyToChannel)
                .unwrap()
                .into_zx_channel();

        Ok(fproc::NameInfo { path: "/test_data".to_owned(), directory: client_channnel.into() })
    }

    async fn run_test<'a>(
        &'a self,
        invocation: Invocation,
        run_options: &ftest::RunOptions,
        component: Arc<Component>,
        run_listener: &RunListenerProxy,
    ) -> Result<(), RunTestError> {
        let test = invocation.name.as_ref().ok_or(RunTestError::TestCaseName)?.to_string();
        info!("Running test {}", test);

        let names = vec![self.test_data_namespace()?];
        let my_uuid = uuid::Uuid::new_v4();
        let file_name = format!("test_result-{}.json", my_uuid);
        let test_list_file = Path::new(&file_name);
        let test_list_path = Path::new("/test_data").join(test_list_file);

        let mut args = vec![
            format!(test_flag!("filter={}"), test),
            format!(test_flag!("output=json:{}"), test_list_path.display()),
        ];

        if run_options.include_disabled_tests.unwrap_or(false) {
            args.push(test_flag!("also_run_disabled_tests").to_owned());
        }

        let (test_stdout, stdout_client) =
            zx::Socket::create(zx::SocketOpts::STREAM).map_err(KernelError::CreateSocket).unwrap();
        let (test_stderr, stderr_client) =
            zx::Socket::create(zx::SocketOpts::STREAM).map_err(KernelError::CreateSocket).unwrap();

        let (case_listener_proxy, listener) =
            fidl::endpoints::create_proxy::<fidl_fuchsia_test::CaseListenerMarker>()
                .map_err(FidlError::CreateProxy)
                .unwrap();

        run_listener
            .on_test_case_started(
                invocation,
                ftest::StdHandles {
                    out: Some(stdout_client),
                    err: Some(stderr_client),
                    ..ftest::StdHandles::EMPTY
                },
                listener,
            )
            .map_err(RunTestError::SendStart)?;
        let test_stdout =
            fasync::Socket::from_socket(test_stdout).map_err(KernelError::SocketToAsync).unwrap();
        let mut test_stdout = SocketLogWriter::new(test_stdout);
        let test_stderr =
            fasync::Socket::from_socket(test_stderr).map_err(KernelError::SocketToAsync).unwrap();
        let mut test_stderr = SocketLogWriter::new(test_stderr);

        args.extend(component.args.clone());
        if let Some(user_args) = &run_options.arguments {
            if let Err(e) = TestServer::validate_args(user_args) {
                test_stderr.write_str(&format!("{}", e)).await?;
                case_listener_proxy
                    .finished(TestResult { status: Some(Status::Failed), ..TestResult::EMPTY })
                    .map_err(RunTestError::SendFinish)?;
                return Ok(());
            }
            args.extend(user_args.clone());
        }
        // run test.
        // Load bearing to hold job guard.
        let (process, _job, stdout_logger, stderr_logger) =
            match launch_component_process_separate_std_handles::<RunTestError>(
                &component, names, args,
            )
            .await
            {
                Ok(s) => s,
                Err(e) => {
                    warn!("failed to launch component process for {}: {}", component.url, e);
                    test_stderr
                        .write_str(&format!("failed to launch component process: {}", e))
                        .await?;
                    case_listener_proxy
                        .finished(TestResult { status: Some(Status::Failed), ..TestResult::EMPTY })
                        .map_err(RunTestError::SendFinish)?;
                    return Ok(());
                }
            };

        let stderr_logger_task = stderr_logger.buffer_and_drain(&mut test_stderr);
        let stdout_logger_task = async {
            let mut last_line_excluded = false;
            let mut socket_buf = vec![0u8; SOCKET_BUFFER_SIZE];
            let mut socket = stdout_logger.take_socket();
            while let Some(bytes_read) =
                NonZeroUsize::new(socket.read(&mut socket_buf[..]).await.map_err(LogError::Read)?)
            {
                let mut bytes = &socket_buf[..bytes_read.get()];

                // Avoid printing trailing empty line
                if *bytes.last().unwrap() == NEWLINE {
                    bytes = &bytes[..bytes.len() - 1];
                }

                let mut iter = bytes.split(|&x| x == NEWLINE);

                while let Some(line) = iter.next() {
                    if line.len() == 0 && last_line_excluded {
                        // sometimes excluded lines print two newlines, we don't want to print blank
                        // output to user's screen.
                        continue;
                    }
                    last_line_excluded = PREFIXES_TO_EXCLUDE.iter().any(|p| line.starts_with(p));

                    if !last_line_excluded {
                        let line = [line, &[NEWLINE]].concat();
                        test_stdout.write(&line).await?;
                    }
                }
            }
            Ok::<(), LogError>(())
        };

        let (out_result, err_result) = join(stdout_logger_task, stderr_logger_task).await;
        out_result?;
        err_result?;

        debug!("Waiting for test to finish: {}", test);

        // wait for test to end.
        fasync::OnSignals::new(&process, zx::Signals::PROCESS_TERMINATED)
            .await
            .map_err(KernelError::ProcessExit)
            .unwrap();

        let process_info = process.info().map_err(RunTestError::ProcessInfo)?;

        // gtest returns 0 is test succeeds and 1 if test fails. This will test if test ended abnormally.
        if process_info.return_code != 0 && process_info.return_code != 1 {
            test_stderr.write_str("Test exited abnormally\n").await?;

            case_listener_proxy
                .finished(TestResult { status: Some(Status::Failed), ..TestResult::EMPTY })
                .map_err(RunTestError::SendFinish)?;
            return Ok(());
        }

        debug!("Opening output file for {}", test);
        // read test result file.
        let result_str = match read_file(&self.output_dir_proxy, &test_list_file).await {
            Ok(b) => b,
            Err(e) => {
                // TODO(fxbug.dev/45857): Introduce Status::InternalError.
                test_stderr
                    .write_str(&format!("Error reading test result:{:?}\n", IoError::File(e)))
                    .await?;

                case_listener_proxy
                    .finished(TestResult { status: Some(Status::Failed), ..TestResult::EMPTY })
                    .map_err(RunTestError::SendFinish)?;
                return Ok(());
            }
        };

        debug!("parse output file for {}", test);
        let test_list: TestOutput =
            serde_json::from_str(&result_str).map_err(RunTestError::JsonParse)?;
        debug!("parsed output file for {}", test);

        // parse test results.
        if test_list.testsuites.len() != 1 || test_list.testsuites[0].testsuite.len() != 1 {
            // TODO(fxbug.dev/45857): Introduce Status::InternalError.
            test_stderr
                .write_str("unexpected output, should have received exactly one test result.\n")
                .await?;

            case_listener_proxy
                .finished(TestResult { status: Some(Status::Failed), ..TestResult::EMPTY })
                .map_err(RunTestError::SendFinish)?;
            return Ok(());
        }

        // as we only run one test per iteration result would be always at 0 index in the arrays.
        let test_suite = &test_list.testsuites[0].testsuite[0];
        let test_status = match &test_suite.status {
            IndividualTestOutputStatus::NotRun => Status::Skipped,
            IndividualTestOutputStatus::Run => {
                match test_suite.result.as_str() {
                    DYNAMIC_SKIP_RESULT => Status::Skipped,
                    _ => match &test_suite.failures {
                        Some(_failures) => {
                            // TODO(fxbug.dev/53955): re-enable. currently we are getting these logs from test's
                            // stdout which we are printing above.
                            //for f in failures {
                            //   test_stderr.write_str(format!("failure: {}\n", f.failure)).await?;
                            // }

                            Status::Failed
                        }
                        None => Status::Passed,
                    },
                }
            }
        };
        case_listener_proxy
            .finished(TestResult { status: Some(test_status), ..TestResult::EMPTY })
            .map_err(RunTestError::SendFinish)?;
        debug!("test finish {}", test);
        Ok(())
    }

    pub fn validate_args(args: &Vec<String>) -> Result<(), ArgumentError> {
        let restricted_flags = args
            .iter()
            .filter(|arg| {
                for r_flag in RESTRICTED_FLAGS.iter() {
                    if arg.starts_with(r_flag) {
                        return true;
                    }
                }
                return false;
            })
            .map(|s| s.clone())
            .collect::<Vec<_>>()
            .join(", ");
        if restricted_flags.len() > 0 {
            return Err(ArgumentError::RestrictedArg(restricted_flags));
        }
        Ok(())
    }
}

/// Internal, uncached implementation of `enumerate_tests`.
async fn get_tests(
    component: Arc<Component>,
    test_data_namespace: fproc::NameInfo,
    output_dir_proxy: Arc<fio::DirectoryProxy>,
) -> Result<Vec<TestCaseInfo>, EnumerationError> {
    let names = vec![test_data_namespace];

    let test_list_file = Path::new("test_list.json");
    let test_list_path = Path::new("/test_data").join(test_list_file);

    let args = vec![
        test_flag!("list_tests").to_owned(),
        format!(test_flag!("output=json:{}"), test_list_path.display()),
    ];

    // Load bearing to hold job guard.
    let (process, _job, stdlogger) =
        launch_component_process::<EnumerationError>(&component, names, args).await?;

    // collect stdout in background before waiting for process termination.
    let std_reader = LogStreamReader::new(stdlogger);

    fasync::OnSignals::new(&process, zx::Signals::PROCESS_TERMINATED)
        .await
        .map_err(KernelError::ProcessExit)
        .unwrap();

    let process_info = process.info().map_err(KernelError::ProcessInfo).unwrap();
    if process_info.return_code != 0 {
        let logs = std_reader.get_logs().await?;
        // TODO(fxbug.dev/4610): logs might not be utf8, fix the code.
        let output = from_utf8(&logs)?;
        // TODO(fxbug.dev/45858): Add a error logger to API so that we can display test stdout logs.
        error!("Failed getting list of tests:\n{}", output);
        return Err(EnumerationError::ListTest);
    }
    let result_str = match read_file(&output_dir_proxy, &test_list_file).await {
        Ok(b) => b,
        Err(e) => {
            let logs = std_reader.get_logs().await?;

            // TODO(fxbug.dev/4610): logs might not be utf8, fix the code.
            let output = from_utf8(&logs)?;
            error!("Failed getting list of tests from {}:\n{}", test_list_file.display(), output);
            return Err(IoError::File(e).into());
        }
    };

    let test_list: ListTestResult =
        serde_json::from_str(&result_str).map_err(EnumerationError::from)?;

    let mut tests = Vec::<TestCaseInfo>::with_capacity(test_list.tests);

    for suite in &test_list.testsuites {
        for test in &suite.testsuite {
            let name = format!("{}.{}", suite.name, test.name);
            let enabled = is_test_case_enabled(&name);
            tests.push(TestCaseInfo { name, enabled })
        }
    }

    Ok(tests)
}

/// Returns `true` if the test case is disabled, based on its name. (This is apparently the only
/// way that gtest tests can be disabled.)
/// See
/// https://github.com/google/googletest/blob/HEAD/googletest/docs/advanced.md#temporarily-disabling-tests
fn is_test_case_enabled(case_name: &str) -> bool {
    !case_name.contains("DISABLED_")
}

/// Convenience wrapper around [`launch::launch_process`].
async fn launch_component_process_separate_std_handles<E>(
    component: &Component,
    names: Vec<fproc::NameInfo>,
    args: Vec<String>,
) -> Result<(zx::Process, launch::ScopedJob, LoggerStream, LoggerStream), E>
where
    E: From<NamespaceError> + From<launch::LaunchError> + From<ComponentError>,
{
    let (client, loader) =
        fidl::endpoints::create_endpoints().map_err(launch::LaunchError::Fidl)?;
    component.loader_service(loader);
    let executable_vmo = Some(component.executable_vmo()?);

    Ok(launch::launch_process_with_separate_std_handles(launch::LaunchProcessArgs {
        bin_path: &component.binary,
        process_name: &component.name,
        job: Some(component.job.create_child_job().map_err(KernelError::CreateJob).unwrap()),
        ns: component.ns.clone(),
        args: Some(args),
        name_infos: Some(names),
        environs: component.environ.clone(),
        handle_infos: None,
        loader_proxy_chan: Some(client.into_channel()),
        executable_vmo,
        options: component.options,
    })
    .await?)
}

/// Convenience wrapper around [`launch::launch_process`].
async fn launch_component_process<E>(
    component: &Component,
    names: Vec<fproc::NameInfo>,
    args: Vec<String>,
) -> Result<(zx::Process, launch::ScopedJob, LoggerStream), E>
where
    E: From<NamespaceError> + From<launch::LaunchError> + From<ComponentError>,
{
    let (client, loader) =
        fidl::endpoints::create_endpoints().map_err(launch::LaunchError::Fidl)?;
    component.loader_service(loader);
    let executable_vmo = Some(component.executable_vmo()?);

    Ok(launch::launch_process(launch::LaunchProcessArgs {
        bin_path: &component.binary,
        process_name: &component.name,
        job: Some(component.job.create_child_job().map_err(KernelError::CreateJob).unwrap()),
        ns: component.ns.clone(),
        args: Some(args),
        name_infos: Some(names),
        environs: component.environ.clone(),
        handle_infos: None,
        loader_proxy_chan: Some(client.into_channel()),
        executable_vmo,
        options: component.options,
    })
    .await?)
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        anyhow::{Context as _, Error},
        assert_matches::assert_matches,
        fidl_fuchsia_test::{RunListenerMarker, RunOptions, SuiteMarker},
        pretty_assertions::assert_eq,
        std::fs,
        test_runners_test_lib::{
            assert_event_ord, collect_listener_event, names_to_invocation, test_component,
            ListenerEvent,
        },
        uuid::Uuid,
    };

    #[cfg(feature = "gtest")]
    macro_rules! test_bin_name {
        ($f: expr) => {
            concat!("bin/", "gtest_", $f)
        };
    }

    #[cfg(feature = "gunit")]
    macro_rules! test_bin_name {
        ($f: expr) => {
            concat!("bin/", "gunit_", $f)
        };
    }

    struct TestDataDir {
        dir_name: String,
    }

    impl TestDataDir {
        fn new() -> Result<Self, Error> {
            let dir = format!("/tmp/{}", Uuid::new_v4().simple());
            fs::create_dir(&dir).context("cannot create test output directory")?;
            Ok(Self { dir_name: dir })
        }

        fn proxy(&self) -> Result<fio::DirectoryProxy, Error> {
            fuchsia_fs::directory::open_in_namespace(
                &self.dir_name,
                fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
            )
            .context("Cannot open test data directory")
        }
    }

    impl Drop for TestDataDir {
        fn drop(&mut self) {
            fs::remove_dir_all(&self.dir_name).expect("can't delete temp dir");
        }
    }

    #[test]
    fn validate_args_test() {
        let restricted_flags = vec![
            test_flag!("filter"),
            test_flag!("filter=mytest"),
            test_flag!("output"),
            test_flag!("output=json"),
        ];

        for flag in restricted_flags {
            let args = vec![flag.to_string()];
            let err = TestServer::validate_args(&args)
                .expect_err(&format!("should error out for flag: {}", flag));
            match err {
                ArgumentError::RestrictedArg(f) => assert_eq!(f, flag),
            }
        }

        let allowed_flags = vec![test_flag!("anyotherflag"), "--anyflag", "--mycustomflag"];

        for flag in allowed_flags {
            let args = vec![flag.to_string()];
            TestServer::validate_args(&args)
                .expect(&format!("should not error out for flag: {}", flag));
        }
    }

    async fn sample_test_component() -> Result<Arc<Component>, Error> {
        test_component(
            "fuchsia-pkg://fuchsia.com/sample_test#test.cm",
            "test.cm",
            test_bin_name!("runner_sample_tests"),
            vec![],
        )
        .await
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn can_enumerate_sample_test() -> Result<(), Error> {
        let test_data = TestDataDir::new()?;

        let component = sample_test_component().await?;

        let server =
            TestServer::new(test_data.proxy()?, "some_name".to_owned(), "some_path".to_owned());

        assert_eq!(
            *server.enumerate_tests(component.clone()).await?,
            vec![
                TestCaseInfo { name: "SampleTest1.SimpleFail".to_owned(), enabled: true },
                TestCaseInfo { name: "SampleTest1.Crashing".to_owned(), enabled: true },
                TestCaseInfo { name: "SampleTest2.SimplePass".to_owned(), enabled: true },
                TestCaseInfo { name: "SampleFixture.Test1".to_owned(), enabled: true },
                TestCaseInfo { name: "SampleFixture.Test2".to_owned(), enabled: true },
                TestCaseInfo {
                    name: "SampleDisabled.DISABLED_TestPass".to_owned(),
                    enabled: false
                },
                TestCaseInfo {
                    name: "SampleDisabled.DISABLED_TestFail".to_owned(),
                    enabled: false
                },
                TestCaseInfo { name: "SampleDisabled.DynamicSkip".to_owned(), enabled: true },
                TestCaseInfo { name: "WriteToStd.TestPass".to_owned(), enabled: true },
                TestCaseInfo { name: "WriteToStd.TestFail".to_owned(), enabled: true },
                TestCaseInfo {
                    name: "Tests/SampleParameterizedTestFixture.Test/0".to_owned(),
                    enabled: true,
                },
                TestCaseInfo {
                    name: "Tests/SampleParameterizedTestFixture.Test/1".to_owned(),
                    enabled: true,
                },
                TestCaseInfo {
                    name: "Tests/SampleParameterizedTestFixture.Test/2".to_owned(),
                    enabled: true,
                },
                TestCaseInfo {
                    name: "Tests/SampleParameterizedTestFixture.Test/3".to_owned(),
                    enabled: true,
                },
            ]
        );

        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn invalid_executable_file() -> Result<(), Error> {
        let err = test_component(
            "fuchsia-pkg://fuchsia.com/rust-test-runner-test#invalid-test.cm",
            "bin/invalid.cm",
            "bin/invalid",
            vec![],
        )
        .await
        .expect_err("this function should have error-ed out due to non-existent file.");

        assert_matches!(
            err.downcast::<ComponentError>().unwrap(),
            ComponentError::LoadingExecutable(..)
        );

        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn can_enumerate_test_with_custom_args() -> Result<(), Error> {
        let test_data = TestDataDir::new()?;

        let component = test_component(
            "fuchsia-pkg://fuchsia.com/test_with_custom_args#test.cm",
            "test.cm",
            test_bin_name!("runner_test_with_custom_args"),
            vec!["--my_custom_arg".to_owned()],
        )
        .await?;

        let server =
            TestServer::new(test_data.proxy()?, "some_name".to_owned(), "some_path".to_owned());

        assert_eq!(
            *server.enumerate_tests(component.clone()).await?,
            vec![TestCaseInfo { name: "TestArg.TestArg".to_owned(), enabled: true },]
        );

        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn can_enumerate_empty_test_file() -> Result<(), Error> {
        let test_data = TestDataDir::new()?;

        let component = test_component(
            "fuchsia-pkg://fuchsia.com/sample_test#test.cm",
            "test.cm",
            test_bin_name!("runner_no_tests"),
            vec![],
        )
        .await?;

        let server =
            TestServer::new(test_data.proxy()?, "some_name".to_owned(), "some_path".to_owned());

        assert_eq!(*server.enumerate_tests(component.clone()).await?, Vec::<TestCaseInfo>::new());

        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn enumerate_huge_tests() -> Result<(), Error> {
        let test_data = TestDataDir::new()?;
        let component = test_component(
            "fuchsia-pkg://fuchsia.com/huge_test#test.cm",
            "test.cm",
            test_bin_name!("huge_runner_example"),
            vec![],
        )
        .await?;

        let server =
            TestServer::new(test_data.proxy()?, "some_name".to_owned(), "some_path".to_owned());

        let mut expected = vec![];
        for i in 0..1000 {
            let name = format!("HugeStress/HugeTest.Test/{}", i);
            expected.push(TestCaseInfo { name, enabled: true });
        }
        assert_eq!(*server.enumerate_tests(component.clone()).await?, expected);

        Ok(())
    }

    async fn run_tests(
        invocations: Vec<Invocation>,
        run_options: RunOptions,
        component: Option<Arc<Component>>,
    ) -> Result<Vec<ListenerEvent>, anyhow::Error> {
        let test_data = TestDataDir::new().context("Cannot create test data")?;
        let component = component
            .unwrap_or(sample_test_component().await.context("Cannot create test component")?);
        let weak_component = Arc::downgrade(&component);
        let server = TestServer::new(
            test_data.proxy().context("Cannot create test server")?,
            "some_name".to_owned(),
            "some_path".to_owned(),
        );

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

    #[fuchsia::test]
    async fn run_multiple_tests() -> Result<(), Error> {
        let events = run_tests(
            names_to_invocation(vec![
                "SampleTest1.SimpleFail",
                "SampleTest1.Crashing",
                "SampleTest2.SimplePass",
                "Tests/SampleParameterizedTestFixture.Test/2",
            ]),
            RunOptions {
                include_disabled_tests: Some(false),
                parallel: None,
                arguments: None,
                ..RunOptions::EMPTY
            },
            None,
        )
        .await
        .unwrap();

        let expected_events = vec![
            ListenerEvent::start_test("SampleTest1.SimpleFail"),
            ListenerEvent::finish_test(
                "SampleTest1.SimpleFail",
                TestResult { status: Some(Status::Failed), ..TestResult::EMPTY },
            ),
            ListenerEvent::start_test("SampleTest1.Crashing"),
            ListenerEvent::finish_test(
                "SampleTest1.Crashing",
                TestResult { status: Some(Status::Failed), ..TestResult::EMPTY },
            ),
            ListenerEvent::start_test("SampleTest2.SimplePass"),
            ListenerEvent::finish_test(
                "SampleTest2.SimplePass",
                TestResult { status: Some(Status::Passed), ..TestResult::EMPTY },
            ),
            ListenerEvent::start_test("Tests/SampleParameterizedTestFixture.Test/2"),
            ListenerEvent::finish_test(
                "Tests/SampleParameterizedTestFixture.Test/2",
                TestResult { status: Some(Status::Passed), ..TestResult::EMPTY },
            ),
            ListenerEvent::finish_all_test(),
        ];

        assert_eq!(expected_events, events);
        Ok(())
    }

    #[fuchsia::test]
    async fn run_test_with_custom_arg() -> Result<(), Error> {
        let component = test_component(
            "fuchsia-pkg://fuchsia.com/test_with_arg#test.cm",
            "test.cm",
            test_bin_name!("runner_test_with_custom_args"),
            vec!["--my_custom_arg".to_owned()],
        )
        .await?;

        let events = run_tests(
            names_to_invocation(vec!["TestArg.TestArg"]),
            RunOptions {
                include_disabled_tests: Some(false),
                parallel: None,
                arguments: Some(vec!["--my_custom_arg2".to_owned()]),
                ..RunOptions::EMPTY
            },
            Some(component),
        )
        .await
        .unwrap();

        let expected_events = vec![
            ListenerEvent::start_test("TestArg.TestArg"),
            ListenerEvent::finish_test(
                "TestArg.TestArg",
                TestResult { status: Some(Status::Passed), ..TestResult::EMPTY },
            ),
            ListenerEvent::finish_all_test(),
        ];

        assert_eq!(expected_events, events);
        Ok(())
    }

    #[fuchsia::test]
    async fn run_multiple_tests_parallel() -> Result<(), Error> {
        let mut events = run_tests(
            names_to_invocation(vec![
                "SampleTest1.SimpleFail",
                "SampleTest1.Crashing",
                "SampleTest2.SimplePass",
                "Tests/SampleParameterizedTestFixture.Test/0",
                "Tests/SampleParameterizedTestFixture.Test/1",
                "Tests/SampleParameterizedTestFixture.Test/2",
            ]),
            RunOptions {
                include_disabled_tests: Some(false),
                parallel: Some(4),
                arguments: None,
                ..RunOptions::EMPTY
            },
            None,
        )
        .await
        .unwrap();

        let mut expected_events = vec![
            ListenerEvent::start_test("SampleTest1.SimpleFail"),
            ListenerEvent::finish_test(
                "SampleTest1.SimpleFail",
                TestResult { status: Some(Status::Failed), ..TestResult::EMPTY },
            ),
            ListenerEvent::start_test("SampleTest1.Crashing"),
            ListenerEvent::finish_test(
                "SampleTest1.Crashing",
                TestResult { status: Some(Status::Failed), ..TestResult::EMPTY },
            ),
            ListenerEvent::start_test("SampleTest2.SimplePass"),
            ListenerEvent::finish_test(
                "SampleTest2.SimplePass",
                TestResult { status: Some(Status::Passed), ..TestResult::EMPTY },
            ),
            ListenerEvent::start_test("Tests/SampleParameterizedTestFixture.Test/0"),
            ListenerEvent::finish_test(
                "Tests/SampleParameterizedTestFixture.Test/0",
                TestResult { status: Some(Status::Passed), ..TestResult::EMPTY },
            ),
            ListenerEvent::start_test("Tests/SampleParameterizedTestFixture.Test/1"),
            ListenerEvent::finish_test(
                "Tests/SampleParameterizedTestFixture.Test/1",
                TestResult { status: Some(Status::Passed), ..TestResult::EMPTY },
            ),
            ListenerEvent::start_test("Tests/SampleParameterizedTestFixture.Test/2"),
            ListenerEvent::finish_test(
                "Tests/SampleParameterizedTestFixture.Test/2",
                TestResult { status: Some(Status::Passed), ..TestResult::EMPTY },
            ),
            ListenerEvent::finish_all_test(),
        ];
        assert_event_ord(&events);

        expected_events.sort();
        events.sort();
        assert_eq!(expected_events, events);
        Ok(())
    }

    #[fuchsia::test]
    async fn run_disabled_tests_exclude() -> Result<(), Error> {
        let events = run_tests(
            names_to_invocation(vec![
                "SampleDisabled.DISABLED_TestPass",
                "SampleDisabled.DISABLED_TestFail",
            ]),
            RunOptions {
                include_disabled_tests: Some(false),
                parallel: None,
                arguments: None,
                ..RunOptions::EMPTY
            },
            None,
        )
        .await
        .unwrap();

        let expected_events = vec![
            ListenerEvent::start_test("SampleDisabled.DISABLED_TestPass"),
            ListenerEvent::finish_test(
                "SampleDisabled.DISABLED_TestPass",
                TestResult { status: Some(Status::Skipped), ..TestResult::EMPTY },
            ),
            ListenerEvent::start_test("SampleDisabled.DISABLED_TestFail"),
            ListenerEvent::finish_test(
                "SampleDisabled.DISABLED_TestFail",
                TestResult { status: Some(Status::Skipped), ..TestResult::EMPTY },
            ),
            ListenerEvent::finish_all_test(),
        ];

        assert_eq!(expected_events, events);
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn run_no_test() -> Result<(), Error> {
        let events = run_tests(
            vec![],
            RunOptions {
                include_disabled_tests: Some(false),
                parallel: None,
                arguments: None,
                ..RunOptions::EMPTY
            },
            None,
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
            names_to_invocation(vec!["SampleTest2.SimplePass"]),
            RunOptions {
                include_disabled_tests: Some(false),
                parallel: None,
                arguments: None,
                ..RunOptions::EMPTY
            },
            None,
        )
        .await
        .unwrap();

        let expected_events = vec![
            ListenerEvent::start_test("SampleTest2.SimplePass"),
            ListenerEvent::finish_test(
                "SampleTest2.SimplePass",
                TestResult { status: Some(Status::Passed), ..TestResult::EMPTY },
            ),
            ListenerEvent::finish_all_test(),
        ];

        assert_eq!(expected_events, events);

        Ok(())
    }
}
