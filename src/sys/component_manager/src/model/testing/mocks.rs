// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        builtin::runner::BuiltinRunnerFactory,
        config::ScopedPolicyChecker,
        model::{
            binding::Binder,
            environment::{Environment, RunnerRegistry},
            error::ModelError,
            moniker::AbsoluteMoniker,
            realm::{BindReason, Realm, WeakRealm},
            resolver::{Resolver, ResolverError, ResolverFut, ResolverRegistry},
            runner::{Runner, RunnerError},
        },
    },
    anyhow::format_err,
    async_trait::async_trait,
    cm_rust::{ComponentDecl, ExposeDecl, UseDecl},
    directory_broker::RoutingFn,
    fidl::{endpoints::ServerEnd, epitaph::ChannelEpitaphExt},
    fidl_fidl_examples_echo::{EchoMarker, EchoRequest, EchoRequestStream},
    fidl_fuchsia_component_runner as fcrunner,
    fidl_fuchsia_io::{DirectoryMarker, NodeMarker},
    fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
    fuchsia_zircon::{self as zx, AsHandleRef, Koid},
    futures::{
        future::{AbortHandle, Abortable},
        lock::Mutex,
        prelude::*,
    },
    std::{
        boxed::Box,
        collections::{HashMap, HashSet},
        convert::TryFrom,
        sync::{Arc, Mutex as SyncMutex},
    },
    vfs::{
        directory::entry::DirectoryEntry, execution_scope::ExecutionScope,
        file::pcb::asynchronous::read_only_static, path::Path, pseudo_directory,
    },
};

/// Creates a routing function factory for `UseDecl` that does the following:
/// - Redirects all directory capabilities to a directory with the file "hello".
/// - Redirects all service capabilities to the echo service.
pub fn proxy_use_routing_factory() -> impl Fn(WeakRealm, UseDecl) -> RoutingFn {
    move |_realm: WeakRealm, use_decl: UseDecl| new_proxy_routing_fn(use_decl.into())
}

/// Creates a routing function factory for `ExposeDecl` that does the following:
/// - Redirects all directory capabilities to a directory with the file "hello".
/// - Redirects all service capabilities to the echo service.
pub fn proxy_expose_routing_factory() -> impl Fn(WeakRealm, ExposeDecl) -> RoutingFn {
    move |_realm: WeakRealm, expose_decl: ExposeDecl| new_proxy_routing_fn(expose_decl.into())
}

enum CapabilityType {
    Service,
    Protocol,
    Directory,
    Storage,
    Runner,
    Resolver,
    Event,
    EventStream,
}

impl From<UseDecl> for CapabilityType {
    fn from(use_: UseDecl) -> Self {
        match use_ {
            UseDecl::Service(_) => CapabilityType::Service,
            UseDecl::Protocol(_) => CapabilityType::Protocol,
            UseDecl::Directory(_) => CapabilityType::Directory,
            UseDecl::Storage(_) => CapabilityType::Storage,
            UseDecl::Runner(_) => CapabilityType::Runner,
            UseDecl::Event(_) => CapabilityType::Event,
            UseDecl::EventStream(_) => CapabilityType::EventStream,
        }
    }
}

impl From<ExposeDecl> for CapabilityType {
    fn from(expose: ExposeDecl) -> Self {
        match expose {
            ExposeDecl::Service(_) => CapabilityType::Service,
            ExposeDecl::Protocol(_) => CapabilityType::Protocol,
            ExposeDecl::Directory(_) => CapabilityType::Directory,
            ExposeDecl::Runner(_) => CapabilityType::Runner,
            ExposeDecl::Resolver(_) => CapabilityType::Resolver,
        }
    }
}

fn new_proxy_routing_fn(ty: CapabilityType) -> RoutingFn {
    Box::new(
        move |flags: u32, mode: u32, relative_path: String, server_end: ServerEnd<NodeMarker>| {
            match ty {
                CapabilityType::Protocol => {
                    fasync::Task::spawn(async move {
                        let server_end: ServerEnd<EchoMarker> =
                            ServerEnd::new(server_end.into_channel());
                        let mut stream: EchoRequestStream = server_end.into_stream().unwrap();
                        while let Some(EchoRequest::EchoString { value, responder }) =
                            stream.try_next().await.unwrap()
                        {
                            responder.send(value.as_ref().map(|s| &**s)).unwrap();
                        }
                    })
                    .detach();
                }
                CapabilityType::Directory | CapabilityType::Storage => {
                    let sub_dir = pseudo_directory!(
                        "hello" => read_only_static(b"friend"),
                    );
                    let path =
                        Path::validate_and_split(relative_path).expect("Failed to split path");
                    sub_dir.open(ExecutionScope::new(), flags, mode, path, server_end);
                }
                CapabilityType::Service => panic!("service capability unsupported"),
                CapabilityType::Runner => panic!("runner capability unsupported"),
                CapabilityType::Resolver => panic!("resolver capability unsupported"),
                CapabilityType::Event => panic!("event capability unsupported"),
                CapabilityType::EventStream => panic!("event stream capability unsupported"),
            }
        },
    )
}

pub struct MockResolver {
    components: HashMap<String, ComponentDecl>,
}

impl MockResolver {
    pub fn new() -> Self {
        MockResolver { components: HashMap::new() }
    }

    async fn resolve_async(&self, component_url: String) -> Result<fsys::Component, ResolverError> {
        const NAME_PREFIX: &str = "test:///";
        debug_assert!(component_url.starts_with(NAME_PREFIX), "invalid component url");
        let (_, name) = component_url.split_at(NAME_PREFIX.len());
        let decl = self.components.get(name).ok_or(ResolverError::component_not_available(
            name.to_string(),
            format_err!("not in the hashmap"),
        ))?;
        let fsys_decl =
            fsys::ComponentDecl::try_from(decl.clone()).expect("decl failed conversion");
        Ok(fsys::Component {
            resolved_url: Some(format!("test:///{}_resolved", name)),
            decl: Some(fsys_decl),
            package: None,
        })
    }

    pub fn add_component(&mut self, name: &str, component: ComponentDecl) {
        self.components.insert(name.to_string(), component);
    }
}

impl Resolver for MockResolver {
    fn resolve<'a>(&'a self, component_url: &'a str) -> ResolverFut<'a> {
        Box::pin(self.resolve_async(component_url.to_string()))
    }
}

pub type HostFn = Box<dyn Fn(ServerEnd<DirectoryMarker>) + Send + Sync>;

pub type ManagedNamespace = Mutex<Vec<fcrunner::ComponentNamespaceEntry>>;

struct MockRunnerInner {
    /// List of URLs started by this runner instance.
    urls_run: Vec<String>,

    /// Map of URL to Vec of waiters that get notified when their associated URL is
    /// "run" by the MockRunner.
    url_waiters: HashMap<String, Vec<futures::channel::oneshot::Sender<()>>>,

    /// Namespace for each component, mapping resolved URL to the component's namespace.
    namespaces: HashMap<String, Arc<Mutex<Vec<fcrunner::ComponentNamespaceEntry>>>>,

    /// Functions for serving the `outgoing` and `runtime` directories
    /// of a given compoment. When a component is started, these
    /// functions will be called with the server end of the directories.
    outgoing_host_fns: HashMap<String, Arc<HostFn>>,
    runtime_host_fns: HashMap<String, Arc<HostFn>>,

    /// Set of URLs that the MockRunner will fail the `start` call for.
    failing_urls: HashSet<String>,

    /// Map from the `Koid` of `Channel` owned by a `ComponentController` to
    /// the messages received by that controller.
    runner_requests: Arc<Mutex<HashMap<Koid, Vec<ControlMessage>>>>,

    controllers: HashMap<Koid, AbortHandle>,

    last_checker: Option<ScopedPolicyChecker>,
}

pub struct MockRunner {
    // The internal runner state.
    //
    // Inner state is guarded by a std::sync::Mutex to avoid helper
    // functions needing "async" (and propagating to callers).
    // std::sync::MutexGuard doesn't have the "Send" trait, so the
    // compiler will prevent us calling ".await" while holding the lock.
    inner: SyncMutex<MockRunnerInner>,
}

impl MockRunner {
    pub fn new() -> Self {
        MockRunner {
            inner: SyncMutex::new(MockRunnerInner {
                urls_run: vec![],
                url_waiters: HashMap::new(),
                namespaces: HashMap::new(),
                outgoing_host_fns: HashMap::new(),
                runtime_host_fns: HashMap::new(),
                failing_urls: HashSet::new(),
                runner_requests: Arc::new(Mutex::new(HashMap::new())),
                controllers: HashMap::new(),
                last_checker: None,
            }),
        }
    }

    /// Cause the URL `url` to return an error when started.
    pub fn add_failing_url(&self, url: &str) {
        self.inner.lock().unwrap().failing_urls.insert(url.to_string());
    }

    /// Cause the component `name` to return an error when started.
    pub fn cause_failure(&self, name: &str) {
        self.add_failing_url(&format!("test:///{}_resolved", name))
    }

    /// Return a list of URLs that have been run by this runner.
    pub fn urls_run(&self) -> Vec<String> {
        self.inner.lock().unwrap().urls_run.clone()
    }

    /// Register `function` to serve the outgoing directory of component `name`
    pub fn add_host_fn(&self, name: &str, function: HostFn) {
        self.inner.lock().unwrap().outgoing_host_fns.insert(name.to_string(), Arc::new(function));
    }

    /// Register `function` to serve the runtime directory of component `name`
    pub fn add_runtime_host_fn(&self, name: &str, function: HostFn) {
        self.inner.lock().unwrap().runtime_host_fns.insert(name.to_string(), Arc::new(function));
    }

    /// Get the input namespace for component `name`.
    pub fn get_namespace(&self, name: &str) -> Option<Arc<ManagedNamespace>> {
        self.inner.lock().unwrap().namespaces.get(name).map(Arc::clone)
    }

    pub fn get_request_map(&self) -> Arc<Mutex<HashMap<Koid, Vec<ControlMessage>>>> {
        self.inner.lock().unwrap().runner_requests.clone()
    }

    /// Returns a future that completes when `url` was launched by the runner. If `url` was
    /// already launched, returns a future that completes immediately.
    pub fn wait_for_url(&self, url: &str) -> impl Future<Output = ()> {
        let (sender, receiver) = futures::channel::oneshot::channel();
        let mut inner = self.inner.lock().unwrap();
        if let Some(_) = inner.urls_run.iter().find(|&u| u == url) {
            sender.send(()).expect("failed to send url notice");
        } else {
            let waiters = inner.url_waiters.entry(url.to_string()).or_insert_with(|| vec![]);
            waiters.push(sender);
        }
        async move { receiver.await.expect("failed to receive url notice") }
    }

    pub fn abort_controller(&self, koid: &Koid) {
        let state = self.inner.lock().unwrap();
        let controller = state.controllers.get(koid).expect("koid was not available");
        controller.abort();
    }

    pub fn last_checker(&self) -> Option<ScopedPolicyChecker> {
        self.inner.lock().unwrap().last_checker.take()
    }
}

impl BuiltinRunnerFactory for MockRunner {
    fn get_scoped_runner(self: Arc<Self>, checker: ScopedPolicyChecker) -> Arc<dyn Runner> {
        {
            let mut state = self.inner.lock().unwrap();
            state.last_checker = Some(checker);
        }
        self
    }
}

#[async_trait]
impl Runner for MockRunner {
    async fn start(
        &self,
        start_info: fcrunner::ComponentStartInfo,
        server_end: ServerEnd<fcrunner::ComponentControllerMarker>,
    ) {
        let outgoing_host_fn;
        let runtime_host_fn;
        let runner_requests;
        // The koid is the only unique piece of information we have about a
        // component start request. Two start requests for the same component
        // URL look identical to the Runner, the only difference being the
        // Channel passed to the Runner to use for the ComponentController
        // protocol.
        let channel_koid = server_end.as_handle_ref().basic_info().expect("basic info failed").koid;
        {
            let mut state = self.inner.lock().unwrap();

            // Resolve the URL, and trigger a failure if previously requested.
            let resolved_url = start_info.resolved_url.unwrap();
            if state.failing_urls.contains(&resolved_url) {
                let status = RunnerError::component_launch_error(
                    resolved_url.clone(),
                    format_err!("launch error"),
                )
                .as_zx_status();
                server_end.into_channel().close_with_epitaph(status).unwrap();
                return;
            }

            // Record that this URL has been started.
            state.urls_run.push(resolved_url.clone());
            if let Some(waiters) = state.url_waiters.remove(&resolved_url) {
                for waiter in waiters {
                    waiter.send(()).expect("failed to send url notice");
                }
            }

            // Create a namespace for the component.
            state
                .namespaces
                .insert(resolved_url.clone(), Arc::new(Mutex::new(start_info.ns.unwrap())));

            // Fetch host functions, which will provide the outgoing and runtime directories
            // for the component.
            //
            // If functions were not provided, then start_info.outgoing_dir will be
            // automatically closed once it goes out of scope at the end of this
            // function.
            outgoing_host_fn = state.outgoing_host_fns.get(&resolved_url).map(Arc::clone);
            runtime_host_fn = state.runtime_host_fns.get(&resolved_url).map(Arc::clone);
            runner_requests = state.runner_requests.clone();
        }

        // Start serving the outgoing/runtime directories.
        if let Some(outgoing_host_fn) = outgoing_host_fn {
            outgoing_host_fn(start_info.outgoing_dir.unwrap());
        }
        if let Some(runtime_host_fn) = runtime_host_fn {
            runtime_host_fn(start_info.runtime_dir.unwrap());
        }
        let abort_handle = MockController::new(server_end, runner_requests, channel_koid).serve();
        let mut state = self.inner.lock().unwrap();
        state.controllers.insert(channel_koid, abort_handle);
    }
}

/// A fake `Binder` implementation that always returns `Ok(())` in a `BoxFuture`.
pub struct FakeBinder;

impl FakeBinder {
    pub fn new() -> Arc<dyn Binder> {
        Arc::new(Self {})
    }
}

#[async_trait]
impl Binder for FakeBinder {
    async fn bind<'a>(
        &'a self,
        _abs_moniker: &'a AbsoluteMoniker,
        _reason: &'a BindReason,
    ) -> Result<Arc<Realm>, ModelError> {
        let resolver = ResolverRegistry::new();
        let root_component_url = "test:///root".to_string();
        Ok(Arc::new(Realm::new_root_realm(
            Environment::new_root(RunnerRegistry::default(), resolver),
            root_component_url,
        )))
    }
}

#[derive(Debug, PartialEq, Clone)]
pub enum ControlMessage {
    Stop,
    Kill,
}

#[derive(Clone)]
/// What the MockController should do when it receives a message.
pub struct ControllerActionResponse {
    pub close_channel: bool,
    pub delay: Option<zx::Duration>,
}

pub struct MockController {
    pub messages: Arc<Mutex<HashMap<Koid, Vec<ControlMessage>>>>,
    request_stream: fcrunner::ComponentControllerRequestStream,
    koid: Koid,
    stop_resp: ControllerActionResponse,
    kill_resp: ControllerActionResponse,
}

impl MockController {
    /// Create a `MockController` that listens to the `server_end` and inserts
    /// `ControlMessage`'s into the Vec in the HashMap keyed under the provided
    /// `Koid`. When either a request to stop or kill a component is received
    /// the `MockController` will close the control channel immediately.
    pub fn new(
        server_end: ServerEnd<fcrunner::ComponentControllerMarker>,
        messages: Arc<Mutex<HashMap<Koid, Vec<ControlMessage>>>>,
        koid: Koid,
    ) -> MockController {
        Self::new_with_responses(
            server_end,
            messages,
            koid,
            ControllerActionResponse { close_channel: true, delay: None },
            ControllerActionResponse { close_channel: true, delay: None },
        )
    }

    /// Create a MockController that listens to the `server_end` and inserts
    /// `ControlMessage`'s into the Vec in the HashMap keyed under the provided
    /// `Koid`. The `stop_response` controls the delay used before taking any
    /// action on the control channel when a request to stop is received. The
    /// `kill_respone` provides the same control when the a request to kill is
    /// received.
    pub fn new_with_responses(
        server_end: ServerEnd<fcrunner::ComponentControllerMarker>,
        messages: Arc<Mutex<HashMap<Koid, Vec<ControlMessage>>>>,
        koid: Koid,
        stop_response: ControllerActionResponse,
        kill_response: ControllerActionResponse,
    ) -> MockController {
        MockController {
            messages: messages,
            request_stream: server_end.into_stream().expect("stream conversion failed"),
            koid: koid,
            stop_resp: stop_response,
            kill_resp: kill_response,
        }
    }
    /// Spawn an async execution context which takes ownership of `server_end`
    /// and inserts `ControlMessage`s into `messages` based on events sent on
    /// the `ComponentController` channel. This simply spawns a future which
    /// awaits self.run().
    pub fn serve(mut self) -> AbortHandle {
        // Listen to the ComponentController server end and record the messages
        // that arrive. Exit after the first one, as this is the contract we
        // have implemented so far. Exiting will cause our handle to the
        // channel to drop and close the channel.

        let (handle, registration) = AbortHandle::new_pair();
        let fut = Abortable::new(
            async move {
                self.messages.lock().await.insert(self.koid, Vec::new());
                while let Ok(Some(request)) = self.request_stream.try_next().await {
                    match request {
                        fcrunner::ComponentControllerRequest::Stop { control_handle: c } => {
                            self.messages
                                .lock()
                                .await
                                .get_mut(&self.koid)
                                .expect("component channel koid key missing from mock runner map")
                                .push(ControlMessage::Stop);
                            if let Some(delay) = self.stop_resp.delay {
                                let delay_copy = delay.clone();
                                let close_channel = self.stop_resp.close_channel;
                                fasync::Task::spawn(async move {
                                    fasync::Timer::new(fasync::Time::after(delay_copy)).await;
                                    if close_channel {
                                        c.shutdown_with_epitaph(zx::Status::OK);
                                    }
                                })
                                .detach();
                            } else if self.stop_resp.close_channel {
                                c.shutdown_with_epitaph(zx::Status::OK);
                                break;
                            }
                        }
                        fcrunner::ComponentControllerRequest::Kill { control_handle: c } => {
                            self.messages
                                .lock()
                                .await
                                .get_mut(&self.koid)
                                .expect("component channel koid key missing from mock runner map")
                                .push(ControlMessage::Kill);
                            if let Some(delay) = self.kill_resp.delay {
                                let delay_copy = delay.clone();
                                let close_channel = self.kill_resp.close_channel;
                                fasync::Task::spawn(async move {
                                    fasync::Timer::new(fasync::Time::after(delay_copy)).await;
                                    if close_channel {
                                        c.shutdown_with_epitaph(zx::Status::OK);
                                    }
                                })
                                .detach();
                                if self.kill_resp.close_channel {
                                    break;
                                }
                            } else if self.kill_resp.close_channel {
                                c.shutdown_with_epitaph(zx::Status::OK);
                                break;
                            }
                        }
                    }
                }
            },
            registration,
        );
        fasync::Task::spawn(async move {
            let _ = fut.await;
        })
        .detach();
        handle
    }
}
