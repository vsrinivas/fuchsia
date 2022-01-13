// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    fidl::endpoints::{
        create_request_stream, ClientEnd, DiscoverableProtocolMarker, MemberOpener, Proxy,
        ServerEnd, ServiceMarker, ServiceProxy,
    },
    fidl_fuchsia_component_runner as fcrunner, fidl_fuchsia_component_test as ftest,
    fidl_fuchsia_data as fdata, fidl_fuchsia_io as fio, fuchsia_async as fasync,
    fuchsia_component::DEFAULT_SERVICE_INSTANCE,
    fuchsia_zircon as zx,
    futures::{future::BoxFuture, lock::Mutex, select, FutureExt, TryStreamExt},
    io_util,
    runner::get_value as get_dictionary_value,
    std::{collections::HashMap, path::Path, sync::Arc},
    tracing::*,
    vfs::{
        directory::entry::DirectoryEntry, execution_scope::ExecutionScope,
        file::vmo::asynchronous::read_only_static, path::Path as VfsPath, pseudo_directory,
    },
};

struct DirectoryProtocolImpl(fio::DirectoryProxy);

impl MemberOpener for DirectoryProtocolImpl {
    fn open_member(&self, member: &str, server_end: zx::Channel) -> Result<(), fidl::Error> {
        let flags = fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE;
        self.0.open(flags, fio::MODE_TYPE_SERVICE, member, ServerEnd::new(server_end))?;
        Ok(())
    }
}

/// The handles from the framework over which the local component should interact with other
/// components.
pub struct LocalComponentHandles {
    namespace: HashMap<String, fio::DirectoryProxy>,

    /// The outgoing directory handle for a local component. This can be used to run a ServiceFs
    /// for the component.
    pub outgoing_dir: ServerEnd<fio::DirectoryMarker>,
}

impl LocalComponentHandles {
    fn new(
        fidl_namespace: Vec<fcrunner::ComponentNamespaceEntry>,
        outgoing_dir: ServerEnd<fio::DirectoryMarker>,
    ) -> Result<Self, Error> {
        let mut namespace = HashMap::new();
        for namespace_entry in fidl_namespace {
            namespace.insert(
                namespace_entry.path.ok_or(format_err!("namespace entry missing path"))?,
                namespace_entry
                    .directory
                    .ok_or(format_err!("namespace entry missing directory handle"))?
                    .into_proxy()
                    .expect("failed to convert handle to proxy"),
            );
        }
        Ok(Self { namespace, outgoing_dir })
    }

    /// Connects to a FIDL protocol and returns a proxy to that protocol.
    pub fn connect_to_protocol<P: DiscoverableProtocolMarker>(&self) -> Result<P::Proxy, Error> {
        self.connect_to_named_protocol::<P>(P::PROTOCOL_NAME)
    }

    /// Connects to a FIDL protocol with the given name and returns a proxy to that protocol.
    pub fn connect_to_named_protocol<P: DiscoverableProtocolMarker>(
        &self,
        name: &str,
    ) -> Result<P::Proxy, Error> {
        let svc_dir_proxy = self
            .namespace
            .get(&"/svc".to_string())
            .ok_or(format_err!("the component's namespace doesn't have a /svc directory"))?;
        let node_proxy = io_util::open_node(
            svc_dir_proxy,
            Path::new(name),
            fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE,
            fio::MODE_TYPE_SERVICE,
        )?;
        Ok(P::Proxy::from_channel(
            node_proxy
                .into_channel()
                .map_err(|_| format_err!("failed to convert proxy to handle"))?,
        ))
    }

    /// Opens a FIDL service as a directory, which holds instances of the service.
    pub fn open_service<S: ServiceMarker>(&self) -> Result<fio::DirectoryProxy, Error> {
        self.open_named_service(S::SERVICE_NAME)
    }

    /// Opens a FIDL service with the given name as a directory, which holds instances of the
    /// service.
    pub fn open_named_service(&self, name: &str) -> Result<fio::DirectoryProxy, Error> {
        let svc_dir_proxy = self
            .namespace
            .get(&"/svc".to_string())
            .ok_or(format_err!("the component's namespace doesn't have a /svc directory"))?;
        io_util::open_directory(
            &svc_dir_proxy,
            &Path::new(name),
            io_util::OPEN_RIGHT_READABLE | io_util::OPEN_RIGHT_WRITABLE,
        )
    }

    /// Connect to the "default" instance of a FIDL service in the `/svc` directory of
    /// the local component's root namespace.
    pub fn connect_to_service<S: ServiceMarker>(&self) -> Result<S::Proxy, Error> {
        self.connect_to_service_instance::<S>(DEFAULT_SERVICE_INSTANCE)
    }

    /// Connect to an instance of a FIDL service in the `/svc` directory of
    /// the local components's root namespace.
    /// `instance` is a path of one or more components.
    pub fn connect_to_service_instance<S: ServiceMarker>(
        &self,
        instance_name: &str,
    ) -> Result<S::Proxy, Error> {
        self.connect_to_named_service_instance::<S>(S::SERVICE_NAME, instance_name)
    }

    /// Connect to an instance of a FIDL service with the given name in the `/svc` directory of
    /// the local components's root namespace.
    /// `instance` is a path of one or more components.
    pub fn connect_to_named_service_instance<S: ServiceMarker>(
        &self,
        service_name: &str,
        instance_name: &str,
    ) -> Result<S::Proxy, Error> {
        let service_dir = self.open_named_service(service_name)?;
        let directory_proxy = io_util::open_directory(
            &service_dir,
            &Path::new(instance_name),
            io_util::OPEN_RIGHT_READABLE | io_util::OPEN_RIGHT_WRITABLE,
        )?;
        Ok(S::Proxy::from_member_opener(Box::new(DirectoryProtocolImpl(directory_proxy))))
    }

    /// Clones a directory from the local component's namespace.
    ///
    /// Note that this function only works on exact matches from the namespace. For example if the
    /// namespace had a `data` entry in it, and the caller wished to open the subdirectory at
    /// `data/assets`, then this function should be called with the argument `data` and the
    /// returned `DirectoryProxy` would then be used to open the subdirectory `assets`. In this
    /// scenario, passing `data/assets` in its entirety to this function would fail.
    ///
    /// ```
    /// let data_dir = handles.clone_from_namespace("data")?;
    /// let assets_dir = io_util::open_directory(&data_dir, Path::new("assets"), ...)?;
    /// ```
    pub fn clone_from_namespace(&self, directory_name: &str) -> Result<fio::DirectoryProxy, Error> {
        let dir_proxy = self.namespace.get(&format!("/{}", directory_name)).ok_or(format_err!(
            "the local component's namespace doesn't have a /{} directory",
            directory_name
        ))?;
        io_util::clone_directory(dir_proxy, fio::CLONE_FLAG_SAME_RIGHTS)
    }
}

type LocalComponentImplementations = HashMap<
    String,
    Arc<
        dyn Fn(LocalComponentHandles) -> BoxFuture<'static, Result<(), Error>>
            + Sync
            + Send
            + 'static,
    >,
>;

#[derive(Clone, Debug)]
pub struct LocalComponentRunnerBuilder {
    local_component_implementations: Arc<Mutex<Option<LocalComponentImplementations>>>,
}

impl LocalComponentRunnerBuilder {
    pub fn new() -> Self {
        Self { local_component_implementations: Arc::new(Mutex::new(Some(HashMap::new()))) }
    }

    pub(crate) async fn register_local_component<I>(
        &self,
        name: String,
        implementation: I,
    ) -> Result<(), ftest::RealmBuilderError2>
    where
        I: Fn(LocalComponentHandles) -> BoxFuture<'static, Result<(), Error>>
            + Sync
            + Send
            + 'static,
    {
        self.local_component_implementations
            .lock()
            .await
            .as_mut()
            .ok_or(ftest::RealmBuilderError2::BuildAlreadyCalled)?
            .insert(name, Arc::new(implementation));
        Ok(())
    }

    pub(crate) async fn build(
        self,
    ) -> Result<
        (ClientEnd<fcrunner::ComponentRunnerMarker>, fasync::Task<()>),
        ftest::RealmBuilderError2,
    > {
        let local_component_implementations = self
            .local_component_implementations
            .lock()
            .await
            .take()
            .ok_or(ftest::RealmBuilderError2::BuildAlreadyCalled)?;
        let (runner_client_end, runner_request_stream) =
            create_request_stream::<fcrunner::ComponentRunnerMarker>()
                .expect("failed to create channel pair");
        let runner = LocalComponentRunner::new(local_component_implementations);
        let runner_task = fasync::Task::spawn(async move {
            if let Err(e) = runner.handle_stream(runner_request_stream).await {
                error!("failed to run local component runner: {:?}", e);
            }
        });

        Ok((runner_client_end, runner_task))
    }
}

pub struct LocalComponentRunner {
    execution_scope: ExecutionScope,
    local_component_implementations: HashMap<
        String,
        Arc<
            dyn Fn(LocalComponentHandles) -> BoxFuture<'static, Result<(), Error>>
                + Sync
                + Send
                + 'static,
        >,
    >,
}

impl Drop for LocalComponentRunner {
    fn drop(&mut self) {
        self.execution_scope.shutdown();
    }
}

impl LocalComponentRunner {
    fn new(
        local_component_implementations: HashMap<
            String,
            Arc<
                dyn Fn(LocalComponentHandles) -> BoxFuture<'static, Result<(), Error>>
                    + Sync
                    + Send
                    + 'static,
            >,
        >,
    ) -> Self {
        Self { local_component_implementations, execution_scope: ExecutionScope::new() }
    }

    async fn handle_stream(
        &self,
        mut runner_request_stream: fcrunner::ComponentRunnerRequestStream,
    ) -> Result<(), Error> {
        while let Some(req) = runner_request_stream.try_next().await? {
            match req {
                fcrunner::ComponentRunnerRequest::Start { start_info, controller, .. } => {
                    if let Some(numbered_handles) = start_info.numbered_handles.as_ref() {
                        if !numbered_handles.is_empty() {
                            return Err(format_err!(
                                "realm builder runner does not support numbered handles"
                            ));
                        }
                    }
                    let program = start_info
                        .program
                        .ok_or(format_err!("program is missing from start_info"))?;
                    let namespace =
                        start_info.ns.ok_or(format_err!("program is missing from start_info"))?;
                    let outgoing_dir = start_info
                        .outgoing_dir
                        .ok_or(format_err!("program is missing from start_info"))?;
                    let runtime_dir_server_end = start_info
                        .runtime_dir
                        .ok_or(format_err!("program is missing from start_info"))?;

                    let local_component_name = extract_local_component_name(program)?;
                    let local_component_implementation = self
                        .local_component_implementations
                        .get(&local_component_name)
                        .ok_or(format_err!("no such local component: {:?}", local_component_name))?
                        .clone();
                    let component_handles = LocalComponentHandles::new(namespace, outgoing_dir)?;

                    let runtime_dir = pseudo_directory!(
                        "local_component_name" =>
                            read_only_static(local_component_name.clone().into_bytes()),
                    );
                    runtime_dir.open(
                        self.execution_scope.clone(),
                        fio::OPEN_RIGHT_READABLE,
                        fio::MODE_TYPE_DIRECTORY,
                        VfsPath::dot(),
                        runtime_dir_server_end.into_channel().into(),
                    );

                    let mut controller_request_stream = controller.into_stream()?;
                    self.execution_scope.spawn(async move {
                        let mut local_component_implementation_fut =
                            (*local_component_implementation)(component_handles).fuse();
                        loop {
                            let mut controller_request_fut =
                                controller_request_stream.try_next().fuse();
                            select! {
                                res = local_component_implementation_fut => {
                                    if let Err(e) = res {
                                        error!(
                                            "the local component {:?} returned an error: {:?}",
                                            local_component_name,
                                            e,
                                        );
                                    }
                                    break;
                                }
                                req_res = controller_request_fut => {
                                    match req_res.expect("invalid controller request") {
                                        Some(fcrunner::ComponentControllerRequest::Stop { .. })
                                        | Some(fcrunner::ComponentControllerRequest::Kill { .. })
                                        | None => {
                                            // TODO: support notifying the component implementation
                                            // on stop
                                            break;
                                        }
                                    }
                                }
                            };
                        }
                    });
                }
            }
        }
        Ok(())
    }
}

fn extract_local_component_name(dict: fdata::Dictionary) -> Result<String, Error> {
    let entry_value = get_dictionary_value(&dict, ftest::LOCAL_COMPONENT_NAME_KEY)
        .ok_or(format_err!("program section is missing component name"))?;
    if let fdata::DictionaryValue::Str(s) = entry_value {
        return Ok(s.clone());
    } else {
        return Err(format_err!("malformed program section"));
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl::endpoints::create_proxy,
        files_async::{readdir, DirEntry, DirentKind},
        fuchsia_zircon::AsHandleRef,
        futures::{channel::oneshot, future::pending, lock::Mutex},
    };

    #[fuchsia::test]
    async fn runner_builder_correctly_stores_a_function() {
        let runner_builder = LocalComponentRunnerBuilder::new();
        let (sender, receiver) = oneshot::channel();
        let sender = Arc::new(Mutex::new(Some(sender)));

        let component_name = "test".to_string();

        runner_builder
            .register_local_component(component_name.clone(), move |_handles| {
                let sender = sender.clone();
                async move {
                    let sender = sender.lock().await.take().expect("local component invoked twice");
                    sender.send(()).expect("failed to send");
                    Ok(())
                }
                .boxed()
            })
            .await
            .unwrap();

        let (_, outgoing_dir) = create_proxy().unwrap();
        let handles = LocalComponentHandles { namespace: HashMap::new(), outgoing_dir };
        let local_component_implementation = runner_builder
            .local_component_implementations
            .lock()
            .await
            .as_ref()
            .unwrap()
            .get(&component_name)
            .expect("local component missing from runner builder")
            .clone();

        (*local_component_implementation)(handles)
            .await
            .expect("local component implementation failed");
        let () = receiver.await.expect("failed to receive");
    }

    struct RunnerAndHandles {
        _runner_task: fasync::Task<()>,
        _component_runner_proxy: fcrunner::ComponentRunnerProxy,
        runtime_dir_proxy: fio::DirectoryProxy,
        outgoing_dir_proxy: fio::DirectoryProxy,
        controller_proxy: fcrunner::ComponentControllerProxy,
    }

    async fn build_and_start(
        runner_builder: LocalComponentRunnerBuilder,
        component_to_start: String,
    ) -> RunnerAndHandles {
        let (component_runner_client_end, runner_task) = runner_builder.build().await.unwrap();
        let component_runner_proxy = component_runner_client_end.into_proxy().unwrap();

        let (runtime_dir_proxy, runtime_dir_server_end) = create_proxy().unwrap();
        let (outgoing_dir_proxy, outgoing_dir_server_end) = create_proxy().unwrap();
        let (controller_proxy, controller_server_end) = create_proxy().unwrap();
        component_runner_proxy
            .start(
                fcrunner::ComponentStartInfo {
                    resolved_url: Some("test://test".to_string()),
                    program: Some(fdata::Dictionary {
                        entries: Some(vec![fdata::DictionaryEntry {
                            key: ftest::LOCAL_COMPONENT_NAME_KEY.to_string(),
                            value: Some(Box::new(fdata::DictionaryValue::Str(component_to_start))),
                        }]),
                        ..fdata::Dictionary::EMPTY
                    }),
                    ns: Some(vec![]),
                    outgoing_dir: Some(outgoing_dir_server_end),
                    runtime_dir: Some(runtime_dir_server_end),
                    numbered_handles: Some(vec![]),
                    ..fcrunner::ComponentStartInfo::EMPTY
                },
                controller_server_end,
            )
            .expect("failed to send start");

        RunnerAndHandles {
            _runner_task: runner_task,
            _component_runner_proxy: component_runner_proxy,
            runtime_dir_proxy,
            outgoing_dir_proxy,
            controller_proxy,
        }
    }

    #[fuchsia::test]
    async fn the_runner_runs_a_component() {
        let runner_builder = LocalComponentRunnerBuilder::new();
        let (sender, receiver) = oneshot::channel();
        let sender = Arc::new(Mutex::new(Some(sender)));

        let component_name = "test".to_string();

        runner_builder
            .register_local_component(component_name.clone(), move |_handles| {
                let sender = sender.clone();
                async move {
                    let sender = sender.lock().await.take().expect("local component invoked twice");
                    sender.send(()).expect("failed to send");
                    Ok(())
                }
                .boxed()
            })
            .await
            .unwrap();

        let _runner_and_handles = build_and_start(runner_builder, component_name).await;

        let () = receiver.await.expect("failed to receive");
    }

    #[fuchsia::test]
    async fn the_runner_services_the_runtime_dir() {
        let runner_builder = LocalComponentRunnerBuilder::new();

        let component_name = "test".to_string();

        runner_builder
            .register_local_component(component_name.clone(), move |_handles| pending().boxed())
            .await
            .unwrap();

        let runner_and_handles = build_and_start(runner_builder, component_name.clone()).await;

        let dir_entries =
            readdir(&runner_and_handles.runtime_dir_proxy).await.expect("failed to readdir");
        let local_component_name_filename = "local_component_name".to_string();
        assert_eq!(
            vec![DirEntry { name: local_component_name_filename.clone(), kind: DirentKind::File }],
            dir_entries
        );
        let local_component_name_file = io_util::directory::open_file(
            &runner_and_handles.runtime_dir_proxy,
            &local_component_name_filename,
            fio::OPEN_RIGHT_READABLE,
        )
        .await
        .expect("failed to open file");
        assert_eq!(
            component_name,
            io_util::read_file(&local_component_name_file).await.expect("failed to read file"),
        );
    }

    #[fuchsia::test]
    async fn the_runner_gives_the_component_its_outgoing_dir() {
        let runner_builder = LocalComponentRunnerBuilder::new();
        let (sender, receiver) = oneshot::channel::<ServerEnd<fio::DirectoryMarker>>();
        let sender = Arc::new(Mutex::new(Some(sender)));

        let component_name = "test".to_string();

        runner_builder
            .register_local_component(component_name.clone(), move |handles| {
                let sender = sender.clone();
                async move {
                    sender
                        .lock()
                        .await
                        .take()
                        .expect("local component invoked twice")
                        .send(handles.outgoing_dir)
                        .expect("failed to send");
                    Ok(())
                }
                .boxed()
            })
            .await
            .unwrap();

        let runner_and_handles = build_and_start(runner_builder, component_name.clone()).await;

        let outgoing_dir_server_end = receiver.await.expect("failed to receive");

        assert_eq!(
            outgoing_dir_server_end
                .into_channel()
                .basic_info()
                .expect("failed to get basic info")
                .koid,
            runner_and_handles
                .outgoing_dir_proxy
                .into_channel()
                .expect("failed to convert to channel")
                .basic_info()
                .expect("failed to get basic info")
                .related_koid,
        );
    }

    #[fuchsia::test]
    async fn controller_stop_will_stop_a_component() {
        let runner_builder = LocalComponentRunnerBuilder::new();
        let (sender, receiver) = oneshot::channel::<()>();
        let sender = Arc::new(Mutex::new(Some(sender)));

        let component_name = "test".to_string();

        runner_builder
            .register_local_component(component_name.clone(), move |_handles| {
                let sender = sender.clone();
                async move {
                    let _sender =
                        sender.lock().await.take().expect("local component invoked twice");
                    // Don't use sender, we want to detect when it gets dropped, which causes an error
                    // to appear on receiver.
                    pending().await
                }
                .boxed()
            })
            .await
            .unwrap();

        let runner_and_handles = build_and_start(runner_builder, component_name).await;
        runner_and_handles.controller_proxy.stop().expect("failed to send stop");
        // TODO: once we support notifying a component implementation that it's about to be
        // stopped, test for that here

        assert_eq!(Err(oneshot::Canceled), receiver.await);
    }

    #[fuchsia::test]
    async fn controller_kill_will_kill_a_component() {
        let runner_builder = LocalComponentRunnerBuilder::new();
        let (sender, receiver) = oneshot::channel::<()>();
        let sender = Arc::new(Mutex::new(Some(sender)));

        let component_name = "test".to_string();

        runner_builder
            .register_local_component(component_name.clone(), move |_handles| {
                let sender = sender.clone();
                async move {
                    let _sender =
                        sender.lock().await.take().expect("local component invoked twice");
                    // Don't use sender, we want to detect when it gets dropped, which causes an error
                    // to appear on receiver.
                    pending().await
                }
                .boxed()
            })
            .await
            .unwrap();

        let runner_and_handles = build_and_start(runner_builder, component_name).await;
        runner_and_handles.controller_proxy.kill().expect("failed to send stop");

        assert_eq!(Err(oneshot::Canceled), receiver.await);
    }

    #[fuchsia::test]
    async fn dropping_the_runner_will_kill_a_component() {
        let runner_builder = LocalComponentRunnerBuilder::new();
        let (sender, receiver) = oneshot::channel::<()>();
        let sender = Arc::new(Mutex::new(Some(sender)));

        let component_name = "test".to_string();

        runner_builder
            .register_local_component(component_name.clone(), move |_handles| {
                let sender = sender.clone();
                async move {
                    let _sender =
                        sender.lock().await.take().expect("local component invoked twice");
                    // Don't use sender, we want to detect when it gets dropped, which causes an error
                    // to appear on receiver.
                    pending().await
                }
                .boxed()
            })
            .await
            .unwrap();

        let runner_and_handles = build_and_start(runner_builder, component_name).await;
        drop(runner_and_handles);

        assert_eq!(Err(oneshot::Canceled), receiver.await);
    }
}
