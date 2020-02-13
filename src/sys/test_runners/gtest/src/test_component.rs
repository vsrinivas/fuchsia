// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::errors::*,
    async_trait::async_trait,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{
        self as fio, DirectoryMarker, CLONE_FLAG_SAME_RIGHTS, OPEN_RIGHT_READABLE,
        OPEN_RIGHT_WRITABLE,
    },
    fidl_fuchsia_process as fproc, fidl_fuchsia_sys2 as fsys, fidl_fuchsia_test as ftest,
    fsys::ComponentControllerMarker,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_runtime::job_default,
    fuchsia_syslog::fx_log_err,
    fuchsia_zircon::{self as zx, HandleBased, Task},
    futures::future::abortable,
    futures::future::AbortHandle,
    futures::prelude::*,
    rand::rngs::ThreadRng,
    rand::Rng,
    runner::component::ComponentNamespace,
    serde_derive::{Deserialize, Serialize},
    std::{
        convert::TryFrom,
        fs,
        ops::Deref,
        ops::Drop,
        path::Path,
        str::from_utf8,
        sync::{Arc, Mutex, Weak},
    },
};

/// All information about this test component.
pub struct Component {
    /// Component URL
    url: String,

    /// Component name
    name: String,

    /// Binary path for this component relative to /pkg in 'ns'
    binary: String,

    /// Arguments for this test.
    args: Vec<String>,

    /// Namespace to pass to test process.
    ns: ComponentNamespace,

    /// Parent job in which all test processes should be executed.
    job: zx::Job,
}

impl Component {
    fn new(
        start_info: fsys::ComponentStartInfo,
    ) -> Result<(Self, ServerEnd<DirectoryMarker>), ComponentError> {
        let url =
            runner::get_resolved_url(&start_info).map_err(ComponentError::InvalidStartInfo)?;
        let name = Path::new(&url)
            .file_name()
            .ok_or_else(|| ComponentError::InvalidUrl)?
            .to_str()
            .ok_or_else(|| ComponentError::InvalidUrl)?
            .to_string();

        let args = runner::get_program_args(&start_info)
            .map_err(|e| ComponentError::InvalidArgs(url.clone(), e.into()))?;
        // TODO validate args

        let binary = runner::get_program_binary(&start_info)
            .map_err(|e| ComponentError::InvalidArgs(url.clone(), e.into()))?;

        let ns = start_info.ns.ok_or_else(|| ComponentError::MissingNamespace(url.clone()))?;
        let ns = ComponentNamespace::try_from(ns)
            .map_err(|e| ComponentError::InvalidArgs(url.clone(), e.into()))?;

        let outgoing_dir =
            start_info.outgoing_dir.ok_or_else(|| ComponentError::MissingOutDir(url.clone()))?;

        Ok((
            Self {
                url: url,
                name: name,
                binary: binary,
                args: args,
                ns: ns,
                job: job_default().create_child_job().map_err(ComponentError::CreateJob)?,
            },
            outgoing_dir,
        ))
    }
}

#[async_trait]
impl runner::component::Killable for ComponentRuntime {
    async fn kill(mut self) {
        self.kill_self();
    }
}

impl Drop for ComponentRuntime {
    fn drop(&mut self) {
        self.kill_self();
    }
}

/// Setup and run test component in background.
pub fn start_component(
    start_info: fsys::ComponentStartInfo,
    server_end: ServerEnd<ComponentControllerMarker>,
) -> Result<(), ComponentError> {
    let (component, outgoing_dir) = Component::new(start_info)?;
    let component = Arc::new(component);

    let job_dup = component
        .job
        .duplicate_handle(zx::Rights::SAME_RIGHTS)
        .map_err(ComponentError::DuplicateJob)?;
    let mut fs = ServiceFs::new_local();
    let mut rng = rand::thread_rng();

    let suite_server_abortable_handles = Arc::new(Mutex::new(vec![]));
    let weak_test_suite_abortable_handles = Arc::downgrade(&suite_server_abortable_handles);

    let weak_component = Arc::downgrade(&component);

    let url = component.url.clone();
    fs.dir("svc").add_fidl_service(move |stream| {
        launch_fidl_service(
            weak_test_suite_abortable_handles.clone(),
            weak_component.clone(),
            &mut rng,
            &url,
            stream,
        );
    });

    fs.serve_connection(outgoing_dir.into_channel()).map_err(ComponentError::ServeSuite)?;
    let (fut, abortable_handle) = abortable(fs.collect::<()>());

    let url = component.url.clone();
    let component_runtime =
        ComponentRuntime::new(abortable_handle, suite_server_abortable_handles, job_dup, component);

    let resolved_url = url.clone();
    fasync::spawn_local(async move {
        if let Err(e) = fut.await {
            fx_log_err!("Test {} ended with error {:?}", url, e);
        }
    });

    let controller_stream = server_end.into_stream().map_err(|e| {
        ComponentError::Fidl("failed to convert server end to controller".to_owned(), e)
    })?;
    let controller = runner::component::Controller::new(component_runtime, controller_stream);
    fasync::spawn_local(async move {
        if let Err(e) = controller.serve().await {
            fx_log_err!("test '{}' controller ended with error: {:?}", resolved_url, e);
        }
    });

    Ok(())
}

/// Launches suite fidl service on `stream`.
fn launch_fidl_service(
    weak_test_suite_abortable_handles: Weak<Mutex<Vec<AbortHandle>>>,
    weak_component: Weak<Component>,
    rng: &mut ThreadRng,
    url: &str,
    stream: fidl_fuchsia_test::SuiteRequestStream,
) {
    let handles = weak_test_suite_abortable_handles.upgrade();
    if handles.is_none() {
        return;
    }
    let handles = handles.unwrap();

    let mut handles = handles.lock().unwrap();

    let test_data_name = format!("{}", rng.gen::<u64>());
    let test_data_path = format!("/data/test_data/{}", test_data_name);

    // TODO(anmittal): use async lib.
    fs::create_dir(&test_data_path).expect("cannot create test output directory.");
    let test_data_dir = io_util::open_directory_in_namespace(
        &test_data_path,
        OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
    )
    .expect("Cannot open data directory");

    let server = TestServer::new(test_data_dir);
    let url = url.clone().to_owned();
    let (fut, test_suite_abortable_handle) =
        abortable(server.serve_test_suite(stream, weak_component.clone()));

    handles.push(test_suite_abortable_handle);

    fasync::spawn_local(async move {
        if let Err(e) = fut.await {
            fx_log_err!("server failed for test {}: {:?}", url, e);
        }

        // Even if `serve_test_suite` failed, clean local data directory as these files are no
        // longer needed and they are consuming space.
        let test_data_dir = io_util::open_directory_in_namespace(
            "/data/test_data",
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
        )
        .expect("Cannot open data directory");
        if let Err(e) = files_async::remove_dir_recursive(&test_data_dir, &test_data_name).await {
            fx_log_err!("error deleting temp data dir '{}': {:?}", test_data_name, e);
        }
    });
}

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

/// Implements `fuchsia.test.Suite` and runs provided test.
pub struct TestServer {
    /// Cache to store enumerated test names.
    test_list: Option<Vec<String>>,

    /// Directory where test data(json) is written by gtest.
    output_dir_proxy: fio::DirectoryProxy,
}

impl TestServer {
    /// Creates new test server.
    /// Clients should call this function to create new object and then call `serve_test_suite`.
    pub fn new(output_dir_proxy: fio::DirectoryProxy) -> Self {
        Self { test_list: None, output_dir_proxy: output_dir_proxy }
    }

    /// Launches test process and gets test list out. Returns list of tests names in the format
    /// defined by gtests, i.e FOO.Bar.
    /// It only runs enumeration logic once, caches and returns the same result back on subsequent
    /// calls.
    async fn enumerate_tests(
        &mut self,
        component: Arc<Component>,
    ) -> Result<Vec<String>, EnumerationError> {
        if let Some(t) = &self.test_list {
            return Ok(t.clone());
        }

        let (client_endpoint, server_endpoint) =
            fidl::endpoints::create_endpoints::<fio::DirectoryMarker>()
                .map_err(EnumerationError::CreateEndpoints)?;

        self.output_dir_proxy
            .clone(
                CLONE_FLAG_SAME_RIGHTS,
                fidl::endpoints::ServerEnd::<fio::NodeMarker>::new(server_endpoint.into_channel()),
            )
            .map_err(EnumerationError::CloneProxy)?;

        let names =
            vec![fproc::NameInfo { path: "/test_data".to_owned(), directory: client_endpoint }];

        let test_list_file = "test_list.json";
        let test_list_path = format!("/test_data/{}", test_list_file);

        let mut args = vec![
            "--gtest_list_tests".to_owned(),
            format!("--gtest_output=json:{}", test_list_path),
        ];

        args.extend(component.args.clone());

        let (process, _job, logger) = test_runners_lib::launch_process(
            &component.binary,
            &component.name,
            Some(component.job.create_child_job().map_err(EnumerationError::CreateJob)?),
            component.ns.clone().map_err(EnumerationError::CloneNamespace)?,
            Some(args),
            Some(names),
            None,
        )
        .await?;

        fasync::OnSignals::new(&process, zx::Signals::PROCESS_TERMINATED)
            .await
            .map_err(EnumerationError::ProcessExit)?;

        let process_info = process.info().map_err(EnumerationError::ProcessInfo)?;

        if process_info.return_code != 0 {
            let logs = logger.try_concat().await.map_err(EnumerationError::LogError)?;
            let output = from_utf8(&logs)?;
            // TODO(anmittal): Add a error logger to API so that we can display test stdout logs.
            fx_log_err!("Failed getting list of tests:\n{}", output);
            return Err(EnumerationError::ListTest);
        }

        // Open the file in read-only mode.

        let result_file_proxy = io_util::open_file(
            &self.output_dir_proxy,
            Path::new(test_list_file),
            OPEN_RIGHT_READABLE,
        )
        .map_err(EnumerationError::OpenFile)?;

        let result_str = match io_util::read_file(&result_file_proxy).await {
            Ok(b) => b,
            Err(e) => {
                let logs = logger.try_concat().await.map_err(EnumerationError::LogError)?;
                let output = from_utf8(&logs)?;
                fx_log_err!("Failed getting list of tests from {}:\n{}", test_list_file, output);
                return Err(EnumerationError::ReadFile(e));
            }
        };

        let test_list: ListTestResult =
            serde_json::from_str(&result_str).map_err(EnumerationError::JsonParse)?;

        let mut tests = Vec::<String>::with_capacity(test_list.tests);

        for suite in &test_list.testsuites {
            for test in &suite.testsuite {
                tests.push(format!("{}.{}", suite.name, test.name))
            }
        }

        self.test_list = Some(tests.clone());

        return Ok(tests);
    }

    /// Implements `fuchsia.test.Suite` service and runs test.
    pub async fn serve_test_suite(
        mut self,
        mut stream: ftest::SuiteRequestStream,
        component: Weak<Component>,
    ) -> Result<(), SuiteServerError> {
        while let Some(event) = stream.try_next().await.map_err(SuiteServerError::Stream)? {
            match event {
                ftest::SuiteRequest::GetTests { responder } => {
                    let component = component.upgrade();
                    if component.is_none() {
                        // no component object, return, test has ended.
                        break;
                    }

                    let tests = self.enumerate_tests(component.unwrap()).await?;
                    responder
                        .send(&mut tests.into_iter().map(|name| ftest::Case { name: Some(name) }))
                        .map_err(SuiteServerError::Response)?;
                }
                ftest::SuiteRequest::Run { tests: _, options: _, listener: _, .. } => {
                    let component = component.upgrade();
                    if component.is_none() {
                        // no component object, return, test has ended.
                        break;
                    }
                    panic!("not implemented yet!");
                }
            }
        }
        Ok(())
    }
}

/// Information about all the test instances running for this component.
struct ComponentRuntime {
    /// handle to abort component's outgoing services.
    outgoing_abortable_handle: Option<futures::future::AbortHandle>,

    /// handle to abort running test suite servers.
    suite_service_abortable_handles: Option<Arc<Mutex<Vec<futures::future::AbortHandle>>>>,

    /// job containing all processes in this component.
    job: Option<zx::Job>,

    /// component object which is stored here for safe keeping. It would be dropped when test is
    /// stopped/killed.
    component: Option<Arc<Component>>,
}

impl ComponentRuntime {
    fn new(
        outgoing_abortable_handle: futures::future::AbortHandle,
        suite_service_abortable_handles: Arc<Mutex<Vec<futures::future::AbortHandle>>>,
        job: zx::Job,
        component: Arc<Component>,
    ) -> Self {
        Self {
            outgoing_abortable_handle: Some(outgoing_abortable_handle),
            suite_service_abortable_handles: Some(suite_service_abortable_handles),
            job: Some(job),
            component: Some(component),
        }
    }

    fn kill_self(&mut self) {
        // drop component.
        self.component.take();

        // kill outgoing server.
        if let Some(h) = self.outgoing_abortable_handle.take() {
            h.abort();
        }

        // kill all suite servers.
        if let Some(handles) = self.suite_service_abortable_handles.take() {
            let handles = handles.lock().unwrap();
            for h in handles.deref() {
                h.abort();
            }
        }

        // kill all test processes if running.
        if let Some(job) = self.job.take() {
            let _ = job.kill();
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        anyhow::{Context as _, Error},
        fidl::endpoints::ClientEnd,
        fidl_fuchsia_sys2 as fsys,
        fio::OPEN_RIGHT_WRITABLE,
        fuchsia_runtime::job_default,
        runner::component::ComponentNamespaceError,
        std::convert::TryFrom,
        std::fs,
        uuid::Uuid,
    };

    struct TestDataDir {
        dir_name: String,
    }

    impl TestDataDir {
        fn new() -> Result<Self, Error> {
            let dir = format!("/tmp/{}", Uuid::new_v4().to_simple());
            fs::create_dir(&dir).context("cannot create test output directory")?;
            Ok(Self { dir_name: dir })
        }

        fn proxy(&self) -> Result<fio::DirectoryProxy, Error> {
            io_util::open_directory_in_namespace(
                &self.dir_name,
                OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            )
            .context("Cannot open test data directory")
        }
    }

    impl Drop for TestDataDir {
        fn drop(&mut self) {
            fs::remove_dir_all(&self.dir_name).expect("can't delete temp dir");
        }
    }

    fn create_ns_from_current_ns(
        dir_paths: Vec<(&str, u32)>,
    ) -> Result<ComponentNamespace, ComponentNamespaceError> {
        let mut paths = vec![];
        let mut directories = vec![];
        for (path, permission) in dir_paths {
            let chan = io_util::open_directory_in_namespace(path, permission)
                .unwrap()
                .into_channel()
                .unwrap()
                .into_zx_channel();
            let handle = ClientEnd::new(chan);

            paths.push(path.to_string());
            directories.push(handle);
        }

        let ns = fsys::ComponentNamespace { paths: paths, directories: directories };

        ComponentNamespace::try_from(ns)
    }

    macro_rules! current_job {
        () => {
            job_default().duplicate(zx::Rights::SAME_RIGHTS)?
        };
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn can_enumerate_sample_test() -> Result<(), Error> {
        let test_data = TestDataDir::new()?;

        let ns = create_ns_from_current_ns(vec![("/pkg", OPEN_RIGHT_READABLE)])?;

        let component = Arc::new(Component {
            url: "fuchsia-pkg://fuchsia.com/sample_test#test.cm".to_owned(),
            name: "test.cm".to_owned(),
            binary: "bin/sample_tests".to_owned(),
            args: vec![],
            ns: ns,
            job: current_job!(),
        });
        let mut server = TestServer::new(test_data.proxy()?);

        assert_eq!(
            server.enumerate_tests(component.clone()).await?,
            vec![
                "SampleTest1.SimpleFail",
                "SampleTest1.Crashing",
                "SampleTest2.SimplePass",
                "SampleFixture.Test1",
                "SampleFixture.Test2",
                "SampleDisabled.DISABLED_Test1",
                "Tests/SampleParameterizedTestFixture.Test/0",
                "Tests/SampleParameterizedTestFixture.Test/1",
                "Tests/SampleParameterizedTestFixture.Test/2",
                "Tests/SampleParameterizedTestFixture.Test/3"
            ]
            .into_iter()
            .map(|s| s.to_owned())
            .collect::<Vec<String>>()
        );

        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn can_enumerate_empty_test_file() -> Result<(), Error> {
        let test_data = TestDataDir::new()?;

        let ns = create_ns_from_current_ns(vec![("/pkg", OPEN_RIGHT_READABLE)])?;

        let component = Arc::new(Component {
            url: "fuchsia-pkg://fuchsia.com/sample_test#test.cm".to_owned(),
            name: "test.cm".to_owned(),
            binary: "bin/no_tests".to_owned(),
            args: vec![],
            ns: ns,
            job: current_job!(),
        });
        let mut server = TestServer::new(test_data.proxy()?);

        assert_eq!(server.enumerate_tests(component.clone()).await?, Vec::<String>::new());

        Ok(())
    }
}
