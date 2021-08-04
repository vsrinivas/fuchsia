// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::SuiteServer,
    crate::errors::ArgumentError,
    anyhow::Context,
    async_trait::async_trait,
    fidl::endpoints::{ProtocolMarker, ServerEnd},
    fidl_fuchsia_component_runner as fcrunner, fidl_fuchsia_io as fio,
    fidl_fuchsia_io::{DirectoryMarker, DirectoryProxy},
    fidl_fuchsia_ldsvc::{LoaderMarker, LoaderRequest},
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_runtime::job_default,
    fuchsia_syslog::{fx_log_err, fx_log_info},
    fuchsia_zircon::{self as zx, AsHandleRef},
    futures::future::abortable,
    futures::lock::Mutex as FutMutex,
    futures::{future::BoxFuture, prelude::*},
    log::warn,
    runner::component::ComponentNamespace,
    std::{
        boxed::Box,
        collections::HashMap,
        convert::{TryFrom, TryInto},
        mem,
        ops::Deref,
        path::Path,
        sync::{Arc, Mutex, Weak},
    },
    thiserror::Error,
    zx::{HandleBased, Task},
};

static PKG_PATH: &'static str = "/pkg";

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

    #[error("Cannot run test {}, as no /pkg directory was supplied.", _0)]
    MissingPkg(String),

    #[error("Cannot load library for {}: {}.", _0, _1)]
    LibraryLoadError(String, anyhow::Error),

    #[error("Cannot load executable binary '{}': {}", _0, _1)]
    LoadingExecutable(String, anyhow::Error),

    #[error("Cannot create vmo child for test {}: {}", _0, _1)]
    VmoChild(String, anyhow::Error),

    #[error("Cannot run suite server: {:?}", _0)]
    ServeSuite(anyhow::Error),

    #[error("{}: {:?}", _0, _1)]
    Fidl(String, fidl::Error),

    #[error("cannot create job: {:?}", _0)]
    CreateJob(zx::Status),

    #[error("cannot create channel: {:?}", _0)]
    CreateChannel(zx::Status),

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
            Self::MissingPkg(_) => zx::Status::INVALID_ARGS,
            Self::LibraryLoadError(_, _) => zx::Status::INVALID_ARGS,
            Self::LoadingExecutable(_, _) => zx::Status::INVALID_ARGS,
            Self::VmoChild(_, _) => zx::Status::INTERNAL,
            Self::ServeSuite(_) => zx::Status::INTERNAL,
            Self::Fidl(_, _) => zx::Status::INTERNAL,
            Self::CreateJob(_) => zx::Status::INTERNAL,
            Self::CreateChannel(_) => zx::Status::INTERNAL,
            Self::DuplicateJob(_) => zx::Status::INTERNAL,
            Self::InvalidUrl => zx::Status::INVALID_ARGS,
        }
    }
}

/// maps vmo key with vmo result.
type VmoKeyMap = HashMap<String, (i32, Option<zx::Vmo>)>;

#[derive(Debug)]
struct LibraryLoaderCache {
    /// Proxy to /pkg/lib
    lib_proxy: Arc<DirectoryProxy>,

    /// Mapping of config key with loaded VMOs map.
    load_response_map: FutMutex<HashMap<String, Arc<FutMutex<VmoKeyMap>>>>,
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

    /// Environment variables for this test.
    pub environ: Option<Vec<String>>,

    /// Namespace to pass to test process.
    pub ns: ComponentNamespace,

    /// Parent job in which all test processes should be executed.
    pub job: zx::Job,

    /// Handle to library loader cache.
    lib_loader_cache: Arc<LibraryLoaderCache>,

    /// cached executable vmo.
    executable_vmo: zx::Vmo,
}

pub struct BuilderArgs {
    /// Component URL
    pub url: String,

    /// Component name
    pub name: String,

    /// Binary path for this component relative to /pkg in 'ns'
    pub binary: String,

    /// Arguments for this test.
    pub args: Vec<String>,

    /// Environment variables for this test.
    pub environ: Option<Vec<String>>,

    /// Namespace to pass to test process.
    pub ns: ComponentNamespace,

    /// Parent job in which all test processes should be executed.
    pub job: zx::Job,
}

impl Component {
    /// Create new object using `ComponentStartInfo`.
    /// On success returns self and outgoing_dir from `ComponentStartInfo`.
    pub async fn new<F>(
        start_info: fcrunner::ComponentStartInfo,
        validate_args: F,
    ) -> Result<(Self, ServerEnd<DirectoryMarker>), ComponentError>
    where
        F: 'static + Fn(&Vec<String>) -> Result<(), ArgumentError>,
    {
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
        validate_args(&args).map_err(|e| ComponentError::InvalidArgs(url.clone(), e.into()))?;

        let binary = runner::get_program_binary(&start_info)
            .map_err(|e| ComponentError::InvalidArgs(url.clone(), e.into()))?;

        // It's safe to unwrap `start_info.program` below because if the field
        // were empty, this func would have a returned an error by now.
        let environ = runner::get_environ(&start_info.program.as_ref().unwrap())
            .map_err(|e| ComponentError::InvalidArgs(url.clone(), e.into()))?;

        let ns = start_info.ns.ok_or_else(|| ComponentError::MissingNamespace(url.clone()))?;
        let ns = ComponentNamespace::try_from(ns)
            .map_err(|e| ComponentError::InvalidArgs(url.clone(), e.into()))?;

        let outgoing_dir =
            start_info.outgoing_dir.ok_or_else(|| ComponentError::MissingOutDir(url.clone()))?;

        let (pkg_proxy, lib_proxy) = get_pkg_and_lib_proxy(&ns, &url)?;

        let executable_vmo = library_loader::load_vmo(pkg_proxy, &binary)
            .await
            .map_err(|e| ComponentError::LoadingExecutable(binary.clone(), e))?;

        Ok((
            Self {
                url: url,
                name: name,
                binary: binary,
                args: args,
                environ,
                ns: ns,
                job: job_default().create_child_job().map_err(ComponentError::CreateJob)?,
                lib_loader_cache: Arc::new(LibraryLoaderCache {
                    lib_proxy: lib_proxy.into(),
                    load_response_map: FutMutex::new(HashMap::new()),
                }),
                executable_vmo,
            },
            outgoing_dir,
        ))
    }

    pub fn executable_vmo(&self) -> Result<zx::Vmo, ComponentError> {
        vmo_create_child(&self.executable_vmo)
            .map_err(|e| ComponentError::VmoChild(self.url.clone(), e))
    }

    pub fn loader_service(&self, loader: ServerEnd<LoaderMarker>) {
        Component::serve_lib_loader(loader, Arc::downgrade(&self.lib_loader_cache))
    }

    pub async fn create_for_tests(args: BuilderArgs) -> Result<Self, ComponentError> {
        let (pkg_proxy, lib_proxy) = get_pkg_and_lib_proxy(&args.ns, &args.url)?;
        let executable_vmo = library_loader::load_vmo(pkg_proxy, &args.binary)
            .await
            .map_err(|e| ComponentError::LoadingExecutable(args.url.clone(), e))?;

        Ok(Self {
            url: args.url,
            name: args.name,
            binary: args.binary,
            args: args.args,
            environ: args.environ,
            ns: args.ns,
            job: args.job,
            lib_loader_cache: Arc::new(LibraryLoaderCache {
                lib_proxy: lib_proxy.into(),
                load_response_map: FutMutex::new(HashMap::new()),
            }),
            executable_vmo,
        })
    }

    /// Serve a custom lib loader which caches request to load VMOs.
    fn serve_lib_loader(
        loader: ServerEnd<LoaderMarker>,
        lib_loader_cache: Weak<LibraryLoaderCache>,
    ) {
        fasync::Task::spawn(
            async move {
                let mut stream = loader.into_stream()?;
                let (mut search_dirs, mut current_response_map) = match lib_loader_cache.upgrade() {
                    Some(obj) => (
                        vec![obj.lib_proxy.clone()],
                        obj.load_response_map
                            .lock()
                            .await
                            .entry("".to_string())
                            .or_default()
                            .clone(),
                    ),
                    None => return Ok(()),
                };

                while let Some(req) = stream.try_next().await? {
                    let lib_loader_cache = match lib_loader_cache.upgrade() {
                        Some(obj) => obj,
                        None => break,
                    };
                    match req {
                        LoaderRequest::Done { control_handle } => {
                            control_handle.shutdown();
                        }
                        LoaderRequest::LoadObject { object_name, responder } => {
                            if let Some((rv, vmo)) =
                                current_response_map.lock().await.get(&object_name)
                            {
                                responder.send(rv.clone(), duplicate_vmo(vmo)?)?;
                                continue;
                            }

                            let (vmo, rv) =
                                match library_loader::load_object(&search_dirs, &object_name).await
                                {
                                    Ok(b) => (b.into(), zx::sys::ZX_OK),
                                    Err(e) => {
                                        warn!("failed to load object: {:?}", e);
                                        (None, zx::sys::ZX_ERR_NOT_FOUND)
                                    }
                                };

                            let vmo_clone = duplicate_vmo(&vmo)?;
                            current_response_map.lock().await.insert(object_name, (rv, vmo));
                            responder.send(rv, vmo_clone)?;
                        }
                        LoaderRequest::Config { config, responder } => {
                            match library_loader::parse_config_string(
                                &lib_loader_cache.lib_proxy,
                                &config,
                            ) {
                                Ok(new_search_path) => {
                                    search_dirs = new_search_path;
                                    current_response_map = lib_loader_cache
                                        .load_response_map
                                        .lock()
                                        .await
                                        .entry(config)
                                        .or_default()
                                        .clone();
                                    responder.send(zx::sys::ZX_OK)?;
                                }
                                Err(e) => {
                                    warn!("failed to parse config: {}", e);
                                    responder.send(zx::sys::ZX_ERR_INVALID_ARGS)?;
                                }
                            }
                        }
                        LoaderRequest::Clone { loader, responder } => {
                            Component::serve_lib_loader(loader, Arc::downgrade(&lib_loader_cache));
                            responder.send(zx::sys::ZX_OK)?;
                        }
                    }
                }
                Ok(())
            }
            .unwrap_or_else(|e: anyhow::Error| {
                warn!("couldn't run library loader service: {:?}", e)
            }),
        )
        .detach();
    }
}

fn duplicate_vmo(vmo: &Option<zx::Vmo>) -> Result<Option<zx::Vmo>, anyhow::Error> {
    Ok(match &vmo {
        // create child instead of duplicating so that our debugger tools don't break.
        // Also vmo created using create child is non-writable, but debugger is able to write to it
        // as it has special permissions.
        Some(vmo) => vmo_create_child(&vmo)?.into(),
        None => None,
    })
}

fn vmo_create_child(vmo: &zx::Vmo) -> Result<zx::Vmo, anyhow::Error> {
    let size = vmo.get_size().context("Cannot get vmo size.")?;
    vmo.create_child(
        zx::VmoChildOptions::SNAPSHOT_AT_LEAST_ON_WRITE | zx::VmoChildOptions::NO_WRITE,
        0,
        size,
    )
    .context("cannot create child vmo")
}

// returns (pkg_proxy, lib_proxy)
fn get_pkg_and_lib_proxy<'a>(
    ns: &'a ComponentNamespace,
    url: &String,
) -> Result<(&'a DirectoryProxy, DirectoryProxy), ComponentError> {
    // Locate the '/pkg' directory proxy previously added to the new component's namespace.
    let (_, pkg_proxy) = ns
        .items()
        .iter()
        .find(|(p, _)| p.as_str() == PKG_PATH)
        .ok_or_else(|| ComponentError::MissingPkg(url.clone()))?;

    let lib_proxy = io_util::open_directory(
        pkg_proxy,
        &Path::new("lib"),
        fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE,
    )
    .map_err(|e| ComponentError::LibraryLoadError(url.clone(), e))?;
    Ok((pkg_proxy, lib_proxy))
}

#[async_trait]
impl runner::component::Controllable for ComponentRuntime {
    async fn kill(mut self) {
        if let Some(component) = &self.component {
            fx_log_info!("kill request component: {}", component.url);
        }
        self.kill_self();
    }

    fn stop<'a>(&mut self) -> BoxFuture<'a, ()> {
        if let Some(component) = &self.component {
            fx_log_info!("stop request component: {}", component.url);
        }
        self.kill_self();
        async move {}.boxed()
    }
}

impl Drop for ComponentRuntime {
    fn drop(&mut self) {
        if let Some(component) = &self.component {
            fx_log_info!("drop component: {}", component.url);
        }
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
        if let Some(component) = self.component.take() {
            fx_log_info!("killing component: {}", component.url);
        }

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
pub async fn start_component<F, U, S>(
    start_info: fcrunner::ComponentStartInfo,
    mut server_end: ServerEnd<fcrunner::ComponentControllerMarker>,
    get_test_server: F,
    validate_args: U,
) -> Result<(), ComponentError>
where
    F: 'static + Fn() -> S + Send,
    U: 'static + Fn(&Vec<String>) -> Result<(), ArgumentError>,
    S: SuiteServer,
{
    let resolved_url = runner::get_resolved_url(&start_info).unwrap_or(String::new());
    if let Err(e) =
        start_component_inner(start_info, &mut server_end, get_test_server, validate_args).await
    {
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

async fn start_component_inner<F, U, S>(
    start_info: fcrunner::ComponentStartInfo,
    server_end: &mut ServerEnd<fcrunner::ComponentControllerMarker>,
    get_test_server: F,
    validate_args: U,
) -> Result<(), ComponentError>
where
    F: 'static + Fn() -> S + Send,
    U: 'static + Fn(&Vec<String>) -> Result<(), ArgumentError>,
    S: SuiteServer,
{
    let (component, outgoing_dir) = Component::new(start_info, validate_args).await?;
    let component = Arc::new(component);

    let job_runtime_dup = component
        .job
        .duplicate_handle(zx::Rights::SAME_RIGHTS)
        .map_err(ComponentError::DuplicateJob)?;

    let job_watch_dup = component
        .job
        .duplicate_handle(zx::Rights::SAME_RIGHTS)
        .map_err(ComponentError::DuplicateJob)?;
    let mut fs = ServiceFs::new();

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
    fasync::Task::spawn(async move {
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

    fasync::Task::spawn(async move {
        if let Err(e) = controller.serve(epitaph_fut).await {
            fx_log_err!("test '{}' controller ended with error: {:?}", resolved_url, e);
        }
    })
    .detach();

    Ok(())
}

fn take_server_end<P: ProtocolMarker>(end: &mut ServerEnd<P>) -> ServerEnd<P> {
    let invalid_end: ServerEnd<P> = zx::Handle::invalid().into();
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
        fidl::endpoints::{self, ClientEnd, Proxy},
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
                ..fcrunner::ComponentNamespaceEntry::EMPTY
            });
        }
        ComponentNamespace::try_from(ns)
    }

    macro_rules! child_job {
        () => {
            job_default().create_child_job().unwrap()
        };
    }

    async fn sample_test_component() -> Result<Arc<Component>, Error> {
        let ns = create_ns_from_current_ns(vec![("/pkg", OPEN_RIGHT_READABLE)])?;

        Ok(Arc::new(
            Component::create_for_tests(BuilderArgs {
                url: "fuchsia-pkg://fuchsia.com/sample_test#test.cm".to_owned(),
                name: "test.cm".to_owned(),
                binary: "bin/test_runners_lib_lib_test".to_owned(), //reference self binary
                args: vec![],
                environ: None,
                ns: ns,
                job: child_job!(),
            })
            .await?,
        ))
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
            ..fcrunner::ComponentStartInfo::EMPTY
        };
        let (client_controller, server_controller) = endpoints::create_proxy().unwrap();
        let get_test_server = || DummyServer {};
        let err = start_component(start_info, server_controller, get_test_server, |_| Ok(())).await;
        assert_matches!(err, Err(ComponentError::InvalidStartInfo(_)));
        assert_matches!(
            client_controller.take_event_stream().next().await,
            Some(Err(fidl::Error::ClientChannelClosed { status: zx::Status::INVALID_ARGS, .. }))
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn start_component_works() {
        let _ = sample_test_component().await.unwrap();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn component_runtime_kill_job_works() {
        let component = sample_test_component().await.unwrap();

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

    async fn list_directory<'a>(root_proxy: &'a DirectoryProxy) -> Vec<String> {
        let dir = io_util::clone_directory(&root_proxy, fio::CLONE_FLAG_SAME_RIGHTS)
            .expect("Failed to clone DirectoryProxy");
        let entries = files_async::readdir(&dir).await.expect("readdir failed");
        entries.iter().map(|entry| entry.name.clone()).collect::<Vec<String>>()
    }

    #[fasync::run_singlethreaded(test)]
    async fn load_objects_test() -> Result<(), Error> {
        // Open this test's real /pkg/lib directory to use for this test, and then check to see
        // whether an asan subdirectory is present, and use it instead if so.
        // TODO(fxbug.dev/37534): Use a synthetic /pkg/lib in this test so it doesn't depend on the
        // package layout (like whether sanitizers are in use) once Rust vfs supports
        // OPEN_RIGHT_EXECUTABLE
        let rights = fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE;
        let mut pkg_lib = io_util::open_directory_in_namespace("/pkg/lib", rights)?;
        let entries = list_directory(&pkg_lib).await;
        if entries.iter().any(|f| &f as &str == "asan-ubsan") {
            pkg_lib = io_util::open_directory(&pkg_lib, &Path::new("asan-ubsan"), rights)?;
        } else if entries.iter().any(|f| &f as &str == "asan") {
            pkg_lib = io_util::open_directory(&pkg_lib, &Path::new("asan"), rights)?;
        } else if entries.iter().any(|f| &f as &str == "coverage") {
            pkg_lib = io_util::open_directory(&pkg_lib, &Path::new("coverage"), rights)?;
        } else if entries.iter().any(|f| &f as &str == "coverage-rust") {
            pkg_lib = io_util::open_directory(&pkg_lib, &Path::new("coverage-rust"), rights)?;
        } else if entries.iter().any(|f| &f as &str == "coverage-sdk") {
            pkg_lib = io_util::open_directory(&pkg_lib, &Path::new("coverage-sdk"), rights)?;
        } else if entries.iter().any(|f| &f as &str == "profile") {
            pkg_lib = io_util::open_directory(&pkg_lib, &Path::new("profile"), rights)?;
        }

        let (loader_proxy, loader_service) = fidl::endpoints::create_proxy::<LoaderMarker>()?;
        let cache = Arc::new(LibraryLoaderCache {
            lib_proxy: pkg_lib.into(),
            load_response_map: FutMutex::new(HashMap::new()),
        });
        Component::serve_lib_loader(loader_service, Arc::downgrade(&cache));
        let tests = vec![
            // Should be able to access lib/ld.so.1
            ("ld.so.1", true),
            // Should be able to access lib/libfdio.so
            ("libfdio.so", true),
            // Should not be able to access lib/lib/ld.so.1
            ("lib/ld.so.1", false),
            // Should not be able to access lib/../lib/ld.so.1
            ("../lib/ld.so.1", false),
            // Should not be able to access lib/bin/test-runner-unit-tests
            ("bin/test-runner-unit-tests", false),
            // Should not be able to access bin/test-runner-unit-tests
            ("../bin/test-runner-unit-tests", false),
            // Should not be able to access meta/test-runner-unit-tests.cm
            ("../meta/test-runner-unit-tests.cm", false),
        ];
        for &(obj_name, should_succeed) in &tests {
            let (res, o_vmo) = loader_proxy.load_object(obj_name).await?;
            let map = cache.load_response_map.lock().await.get("").unwrap().clone();
            assert!(map.lock().await.contains_key(obj_name));
            if should_succeed {
                assert_eq!(zx::sys::ZX_OK, res, "loading {} did not succeed", obj_name);
                assert!(o_vmo.is_some());
                assert_matches!(map.lock().await.get(obj_name).unwrap().1, Some(_));
            } else {
                assert_ne!(zx::sys::ZX_OK, res, "loading {} did not fail", obj_name);
                assert!(o_vmo.is_none());
                assert_eq!(map.lock().await.get(obj_name).unwrap().1, None);
            }
        }

        // also test clone
        let (loader_proxy2, loader_service) = fidl::endpoints::create_proxy::<LoaderMarker>()?;
        assert_eq!(zx::sys::ZX_OK, loader_proxy.clone(loader_service).await?);
        for (obj_name, should_succeed) in tests {
            let (res, o_vmo) = loader_proxy2.load_object(obj_name).await?;
            if should_succeed {
                assert_eq!(zx::sys::ZX_OK, res, "loading {} did not succeed", obj_name);
                assert!(o_vmo.is_some());
            } else {
                assert_ne!(zx::sys::ZX_OK, res, "loading {} did not fail", obj_name);
                assert!(o_vmo.is_none());
            }
        }
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn config_test() -> Result<(), Error> {
        // This /pkg/lib/config_test/ directory is added by the build rules for this test package,
        // since we need a directory that supports OPEN_RIGHT_EXECUTABLE. It contains a file 'foo'
        // which contains 'hippos' and a file 'bar/baz' (that is, baz in a subdirectory bar) which
        // contains 'rule'.
        // TODO(fxbug.dev/37534): Use a synthetic /pkg/lib in this test so it doesn't depend on the
        // package layout once Rust vfs supports OPEN_RIGHT_EXECUTABLE
        let pkg_lib = io_util::open_directory_in_namespace(
            "/pkg/lib/config_test/",
            fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE,
        )?;
        let (loader_proxy, loader_service) = fidl::endpoints::create_proxy::<LoaderMarker>()?;
        let cache = Arc::new(LibraryLoaderCache {
            lib_proxy: pkg_lib.into(),
            load_response_map: FutMutex::new(HashMap::new()),
        });
        Component::serve_lib_loader(loader_service, Arc::downgrade(&cache));

        // Attempt to access things with different configurations
        for (obj_name, config, expected_result) in vec![
            // Should be able to load foo
            ("foo", None, Some("hippos")),
            // Should not be able to load bar (it's a directory)
            ("bar", None, None),
            // Should not be able to load baz (it's in a sub directory)
            ("baz", None, None),
            // Should be able to load baz with config "bar!" (only look in sub directory bar)
            ("baz", Some("bar!"), Some("rule")),
            // Should not be able to load foo with config "bar!" (only look in sub directory bar)
            ("foo", Some("bar!"), None),
            // Should be able to load foo with config "bar" (also look in sub directory bar)
            ("foo", Some("bar"), Some("hippos")),
            // Should be able to load baz with config "bar" (also look in sub directory bar)
            ("baz", Some("bar"), Some("rule")),
        ] {
            if let Some(config) = config {
                assert_eq!(zx::sys::ZX_OK, loader_proxy.config(config).await?);
            }

            let (res, o_vmo) = loader_proxy.load_object(obj_name).await?;
            let map = cache
                .load_response_map
                .lock()
                .await
                .get(config.unwrap_or_default())
                .unwrap()
                .clone();
            if let Some(expected_result) = expected_result {
                assert_eq!(zx::sys::ZX_OK, res);
                let mut buf = vec![0; expected_result.len()];
                o_vmo.expect("missing vmo").read(&mut buf, 0)?;
                assert_eq!(expected_result.as_bytes(), buf.as_slice());
                assert_matches!(map.lock().await.get(obj_name).unwrap().1, Some(_));
            } else {
                assert_ne!(zx::sys::ZX_OK, res);
                assert!(o_vmo.is_none());
                assert_eq!(map.lock().await.get(obj_name).unwrap().1, None);
            }
        }
        Ok(())
    }
}
