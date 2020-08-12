// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::SuiteServer,
    async_trait::async_trait,
    fidl::endpoints::{ServerEnd, ServiceMarker},
    fidl_fuchsia_component_runner as fcrunner,
    fidl_fuchsia_io::DirectoryMarker,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_runtime::job_default,
    fuchsia_syslog::fx_log_err,
    fuchsia_zircon::{self as zx, AsHandleRef},
    futures::future::abortable,
    futures::{future::BoxFuture, prelude::*},
    runner::component::ComponentNamespace,
    std::{
        boxed::Box,
        convert::{TryFrom, TryInto},
        mem,
        ops::Deref,
        path::Path,
        sync::{Arc, Mutex},
    },
    thiserror::Error,
    zx::{HandleBased, Task},
};

/// Error encountered running test component
#[derive(Debug, Error)]
pub enum ComponentError {
    #[error("invalid start info: {:?}", _0)]
    InvalidStartInfo(runner::StartInfoError),

    #[error("error for test {}: {:?}", _0, _1)]
    InvalidArgs(String, anyhow::Error),

    #[error("Cannot run test {}, no namespace was supplied.", _0)]
    MissingNamespace(String),

    #[error("Cannot run test {}, as no outgoing directory was supplied.", _0)]
    MissingOutDir(String),

    #[error("Cannot run suite server: {:?}", _0)]
    ServeSuite(anyhow::Error),

    #[error("{}: {:?}", _0, _1)]
    Fidl(String, fidl::Error),

    #[error("cannot create job: {:?}", _0)]
    CreateJob(zx::Status),

    #[error("cannot duplicate job: {:?}", _0)]
    DuplicateJob(zx::Status),

    #[error("invalid url")]
    InvalidUrl,
}

impl ComponentError {
    /// Convert this error into its approximate `fuchsia.component.Error` equivalent.
    pub fn as_zx_status(&self) -> zx::Status {
        match self {
            Self::InvalidStartInfo(_) => zx::Status::INVALID_ARGS,
            Self::InvalidArgs(_, _) => zx::Status::INVALID_ARGS,
            Self::MissingNamespace(_) => zx::Status::INVALID_ARGS,
            Self::MissingOutDir(_) => zx::Status::INVALID_ARGS,
            Self::ServeSuite(_) => zx::Status::INTERNAL,
            Self::Fidl(_, _) => zx::Status::INTERNAL,
            Self::CreateJob(_) => zx::Status::INTERNAL,
            Self::DuplicateJob(_) => zx::Status::INTERNAL,
            Self::InvalidUrl => zx::Status::INVALID_ARGS,
        }
    }
}

/// All information about this test ELF component.
#[derive(Debug)]
pub struct Component {
    /// Component URL
    pub url: String,

    /// Component name
    pub name: String,

    /// Binary path for this component relative to /pkg in 'ns'
    pub binary: String,

    /// Arguments for this test.
    pub args: Vec<String>,

    /// Namespace to pass to test process.
    pub ns: ComponentNamespace,

    /// Parent job in which all test processes should be executed.
    pub job: zx::Job,
}

impl Component {
    /// Create new object using `ComponentStartInfo`.
    /// On sucess returns self and outgoing_dir from `ComponentStartInfo`.
    pub fn new(
        start_info: fcrunner::ComponentStartInfo,
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
impl runner::component::Controllable for ComponentRuntime {
    async fn kill(mut self) {
        self.kill_self();
    }

    fn stop<'a>(&mut self) -> BoxFuture<'a, ()> {
        self.kill_self();
        async move {}.boxed()
    }
}

impl Drop for ComponentRuntime {
    fn drop(&mut self) {
        self.kill_self();
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

/// Setup and run test component in background.
///
/// * `F`: Function which returns new instance of `SuiteServer`.
pub fn start_component<F, S>(
    start_info: fcrunner::ComponentStartInfo,
    mut server_end: ServerEnd<fcrunner::ComponentControllerMarker>,
    get_test_server: F,
) -> Result<(), ComponentError>
where
    F: 'static + Fn() -> S,
    S: SuiteServer,
{
    let resolved_url = runner::get_resolved_url(&start_info).unwrap_or(String::new());
    if let Err(e) = start_component_inner(start_info, &mut server_end, get_test_server) {
        // Take ownership of `server_end`.
        let server_end = take_server_end(&mut server_end);
        runner::component::report_start_error(
            e.as_zx_status(),
            format!("{}", e),
            &resolved_url,
            server_end,
        );
        return Err(e);
    }
    Ok(())
}

fn start_component_inner<F, S>(
    start_info: fcrunner::ComponentStartInfo,
    server_end: &mut ServerEnd<fcrunner::ComponentControllerMarker>,
    get_test_server: F,
) -> Result<(), ComponentError>
where
    F: 'static + Fn() -> S,
    S: SuiteServer,
{
    let (component, outgoing_dir) = Component::new(start_info)?;
    let component = Arc::new(component);

    let job_runtime_dup = component
        .job
        .duplicate_handle(zx::Rights::SAME_RIGHTS)
        .map_err(ComponentError::DuplicateJob)?;

    let job_watch_dup = component
        .job
        .duplicate_handle(zx::Rights::SAME_RIGHTS)
        .map_err(ComponentError::DuplicateJob)?;
    let mut fs = ServiceFs::new_local();

    let suite_server_abortable_handles = Arc::new(Mutex::new(vec![]));
    let weak_test_suite_abortable_handles = Arc::downgrade(&suite_server_abortable_handles);

    let weak_component = Arc::downgrade(&component);

    let url = component.url.clone();
    fs.dir("svc").add_fidl_service(move |stream| {
        let abortable_handles = weak_test_suite_abortable_handles.upgrade();
        if abortable_handles.is_none() {
            return;
        }
        let abortable_handles = abortable_handles.unwrap();
        let mut abortable_handles = abortable_handles.lock().unwrap();
        let abortable_handle = get_test_server().run(weak_component.clone(), &url, stream);
        abortable_handles.push(abortable_handle);
    });

    fs.serve_connection(outgoing_dir.into_channel()).map_err(ComponentError::ServeSuite)?;
    let (fut, abortable_handle) = abortable(fs.collect::<()>());

    let url = component.url.clone();
    let component_runtime = ComponentRuntime::new(
        abortable_handle,
        suite_server_abortable_handles,
        job_runtime_dup,
        component,
    );

    let resolved_url = url.clone();
    fasync::Task::local(async move {
        // as error on abortable will always return Aborted,
        // no need to check that, as it is a valid usecase.
        fut.await.ok();
    })
    .detach();

    let server_end = take_server_end(server_end);
    let controller_stream = server_end.into_stream().map_err(|e| {
        ComponentError::Fidl("failed to convert server end to controller".to_owned(), e)
    })?;
    let controller = runner::component::Controller::new(component_runtime, controller_stream);

    let epitaph_fut = Box::pin(async move {
        // Just return 'OK' here. Any actual errors will be handled through
        // the test protocol.
        let _ =
            fasync::OnSignals::new(&job_watch_dup.as_handle_ref(), zx::Signals::TASK_TERMINATED)
                .await;
        zx::Status::OK.try_into().unwrap()
    });

    fasync::Task::local(async move {
        if let Err(e) = controller.serve(epitaph_fut).await {
            fx_log_err!("test '{}' controller ended with error: {:?}", resolved_url, e);
        }
    })
    .detach();

    Ok(())
}

fn take_server_end<S: ServiceMarker>(end: &mut ServerEnd<S>) -> ServerEnd<S> {
    let invalid_end: ServerEnd<S> = zx::Handle::invalid().into();
    mem::replace(end, invalid_end)
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            elf::EnumeratedTestCases,
            errors::{EnumerationError, RunTestError},
        },
        anyhow::Error,
        fidl::endpoints::{self, ClientEnd},
        fidl_fuchsia_io::OPEN_RIGHT_READABLE,
        fidl_fuchsia_test::{Invocation, RunListenerProxy},
        fuchsia_runtime::job_default,
        futures::future::{AbortHandle, Aborted},
        matches::assert_matches,
        runner::component::{ComponentNamespace, ComponentNamespaceError},
        std::sync::Weak,
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

    macro_rules! child_job {
        () => {
            job_default().create_child_job().unwrap()
        };
    }

    fn sample_test_component() -> Result<Arc<Component>, Error> {
        let ns = create_ns_from_current_ns(vec![("/pkg", OPEN_RIGHT_READABLE)])?;

        Ok(Arc::new(Component {
            url: "fuchsia-pkg://fuchsia.com/sample_test#test.cm".to_owned(),
            name: "test.cm".to_owned(),
            binary: "bin/sample_tests".to_owned(),
            args: vec![],
            ns: ns,
            job: child_job!(),
        }))
    }

    async fn dummy_func() -> u32 {
        2
    }

    struct DummyServer {}

    #[async_trait]
    impl SuiteServer for DummyServer {
        fn run(
            self,
            _component: Weak<Component>,
            _test_url: &str,
            _stream: fidl_fuchsia_test::SuiteRequestStream,
        ) -> AbortHandle {
            let (_, handle) = abortable(async {});
            handle
        }

        async fn enumerate_tests(
            &self,
            _test_component: Arc<Component>,
        ) -> Result<EnumeratedTestCases, EnumerationError> {
            Ok(Arc::new(vec![]))
        }

        async fn run_tests(
            &self,
            _invocations: Vec<Invocation>,
            _run_options: fidl_fuchsia_test::RunOptions,
            _component: Arc<Component>,
            _run_listener: &RunListenerProxy,
        ) -> Result<(), RunTestError> {
            Ok(())
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn start_component_error() {
        let start_info = fcrunner::ComponentStartInfo {
            resolved_url: None,
            program: None,
            ns: None,
            outgoing_dir: None,
            runtime_dir: None,
        };
        let (client_controller, server_controller) = endpoints::create_proxy().unwrap();
        let get_test_server = || DummyServer {};
        let err = start_component(start_info, server_controller, get_test_server);
        assert_matches!(err, Err(ComponentError::InvalidStartInfo(_)));
        assert_matches!(
            client_controller.take_event_stream().next().await,
            Some(Err(fidl::Error::ClientChannelClosed { status: zx::Status::INVALID_ARGS, .. }))
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn component_runtime_kill_job_works() {
        let component = sample_test_component().unwrap();

        let mut futs = vec![];
        let mut handles = vec![];
        for _i in 0..10 {
            let (fut, handle) = abortable(dummy_func());
            futs.push(fut);
            handles.push(handle);
        }

        let (out_fut, out_handle) = abortable(dummy_func());
        let mut runtime = ComponentRuntime::new(
            out_handle,
            Arc::new(Mutex::new(handles)),
            child_job!(),
            component.clone(),
        );

        assert_eq!(Arc::strong_count(&component), 2);
        runtime.kill_self();

        for fut in futs {
            assert_eq!(fut.await, Err(Aborted));
        }

        assert_eq!(out_fut.await, Err(Aborted));

        assert_eq!(Arc::strong_count(&component), 1);
    }
}
