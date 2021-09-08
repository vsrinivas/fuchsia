// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        builtin_environment::{BuiltinEnvironment, BuiltinEnvironmentBuilder},
        config::RuntimeConfig,
        model::{
            binding::Binder,
            component::{BindReason, ComponentInstance, InstanceState, WeakComponentInstance},
            events::{registry::EventSubscription, source::EventSource, stream::EventStream},
            hooks::HooksRegistration,
            model::Model,
            testing::{
                mocks::{ControlMessage, MockResolver, MockRunner},
                test_hook::TestHook,
            },
        },
    },
    anyhow::{Context, Error},
    cm_rust::{CapabilityName, ChildDecl, ComponentDecl, EventMode, NativeIntoFidl},
    cm_types::Url,
    diagnostics_message::{Message, MonikerWithUrl},
    fidl::endpoints::{self, ProtocolMarker, Proxy},
    fidl_fidl_examples_echo as echo, fidl_fuchsia_component_runner as fcrunner,
    fidl_fuchsia_io::{
        DirectoryMarker, DirectoryProxy, CLONE_FLAG_SAME_RIGHTS, MODE_TYPE_SERVICE,
        OPEN_FLAG_CREATE, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE,
    },
    fidl_fuchsia_logger::{LogSinkMarker, LogSinkRequestStream},
    fidl_fuchsia_sys2 as fsys, files_async, fuchsia_async as fasync,
    fuchsia_component::server::{ServiceFs, ServiceObjLocal},
    fuchsia_zircon::{self as zx, AsHandleRef, Koid},
    futures::{channel::mpsc::Receiver, lock::Mutex, StreamExt, TryStreamExt},
    moniker::{PartialAbsoluteMoniker, PartialChildMoniker},
    std::collections::HashSet,
    std::default::Default,
    std::path::Path,
    std::sync::Arc,
    vfs::{directory::entry::DirectoryEntry, service},
};

pub const TEST_RUNNER_NAME: &str = cm_rust_testing::TEST_RUNNER_NAME;

// TODO(https://fxbug.dev/61861): remove function wrappers once the routing_test_helpers
// lib has a stable API.
pub fn default_component_decl() -> ComponentDecl {
    ::routing_test_helpers::default_component_decl()
}

pub fn component_decl_with_test_runner() -> ComponentDecl {
    ::routing_test_helpers::component_decl_with_test_runner()
}

pub type MockServiceFs<'a> = ServiceFs<ServiceObjLocal<'a, MockServiceRequest>>;

pub enum MockServiceRequest {
    LogSink(LogSinkRequestStream),
}

pub struct ComponentInfo {
    pub component: Arc<ComponentInstance>,
    pub channel_id: Koid,
}

impl ComponentInfo {
    /// Given a `ComponentInstance` which has been bound, look up the resolved URL
    /// and package into a `ComponentInfo` struct.
    pub async fn new(component: Arc<ComponentInstance>) -> ComponentInfo {
        // The koid is the only unique piece of information we have about
        // a component start request. Two start requests for the same
        // component URL look identical to the Runner, the only difference
        // being the Channel passed to the Runner to use for the
        // ComponentController protocol.
        let koid = {
            let component = component.lock_execution().await;
            let runtime = component.runtime.as_ref().expect("runtime is unexpectedly missing");
            let controller =
                runtime.controller.as_ref().expect("controller is unexpectedly missing");
            let basic_info = controller
                .as_channel()
                .basic_info()
                .expect("error getting basic info about controller channel");
            // should be the koid of the other side of the channel
            basic_info.related_koid
        };

        ComponentInfo { component, channel_id: koid }
    }

    /// Checks that the component is shut down, panics if this is not true.
    pub async fn check_is_shut_down(&self, runner: &MockRunner) {
        // Check the list of requests for this component
        let request_map = runner.get_request_map();
        let unlocked_map = request_map.lock().await;
        let request_vec = unlocked_map
            .get(&self.channel_id)
            .expect("request map didn't have channel id, perhaps the controller wasn't started?");
        assert_eq!(*request_vec, vec![ControlMessage::Stop]);

        let execution = self.component.lock_execution().await;
        assert!(execution.runtime.is_none());
        assert!(execution.is_shut_down());
    }

    /// Checks that the component has not been shut down, panics if it has.
    pub async fn check_not_shut_down(&self, runner: &MockRunner) {
        // If the MockController has started, check that no stop requests have
        // been received.
        let request_map = runner.get_request_map();
        let unlocked_map = request_map.lock().await;
        if let Some(request_vec) = unlocked_map.get(&self.channel_id) {
            assert_eq!(*request_vec, vec![]);
        }

        let execution = self.component.lock_execution().await;
        assert!(execution.runtime.is_some());
        assert!(!execution.is_shut_down());
    }
}

pub async fn execution_is_shut_down(component: &ComponentInstance) -> bool {
    let execution = component.lock_execution().await;
    execution.runtime.is_none() && execution.is_shut_down()
}

/// Returns true if the given child (live or deleting) exists.
pub async fn has_child<'a>(component: &'a ComponentInstance, moniker: &'a str) -> bool {
    match *component.lock_state().await {
        InstanceState::Resolved(ref s) => s.all_children().contains_key(&moniker.into()),
        InstanceState::Purged => false,
        _ => panic!("not resolved"),
    }
}

/// Return the instance id of the given live child.
pub async fn get_instance_id<'a>(component: &'a ComponentInstance, moniker: &'a str) -> u32 {
    match *component.lock_state().await {
        InstanceState::Resolved(ref s) => s.get_live_child_instance_id(&moniker.into()).unwrap(),
        _ => panic!("not resolved"),
    }
}

/// Return all monikers of the live children of the given `component`.
pub async fn get_live_children(component: &ComponentInstance) -> HashSet<PartialChildMoniker> {
    match *component.lock_state().await {
        InstanceState::Resolved(ref s) => s.live_children().map(|(m, _)| m.clone()).collect(),
        InstanceState::Purged => HashSet::new(),
        _ => panic!("not resolved"),
    }
}

/// Return the child of the given `component` with moniker `child`.
pub async fn get_live_child<'a>(
    component: &'a ComponentInstance,
    child: &'a str,
) -> Arc<ComponentInstance> {
    match *component.lock_state().await {
        InstanceState::Resolved(ref s) => s.get_live_child(&child.into()).unwrap().clone(),
        _ => panic!("not resolved"),
    }
}

/// Create a fsys::OfferRunnerDecl offering the given cap from the parent to the given child
/// component.
pub fn offer_runner_cap_to_child(runner_cap: &str, child: &str) -> cm_rust::OfferDecl {
    cm_rust::OfferDecl::Runner(cm_rust::OfferRunnerDecl {
        source: cm_rust::OfferSource::Parent,
        source_name: runner_cap.into(),
        target: cm_rust::OfferTarget::Child(child.to_string()),
        target_name: runner_cap.into(),
    })
}

/// Create a fsys::OfferRunnerDecl offering the given cap from the parent to the given child
/// collection.
pub fn offer_runner_cap_to_collection(runner_cap: &str, child: &str) -> cm_rust::OfferDecl {
    cm_rust::OfferDecl::Runner(cm_rust::OfferRunnerDecl {
        source: cm_rust::OfferSource::Parent,
        source_name: runner_cap.into(),
        target: cm_rust::OfferTarget::Collection(child.to_string()),
        target_name: runner_cap.into(),
    })
}

pub async fn dir_contains<'a>(
    root_proxy: &'a DirectoryProxy,
    path: &'a str,
    entry_name: &'a str,
) -> bool {
    let dir = io_util::open_directory(&root_proxy, &Path::new(path), OPEN_RIGHT_READABLE)
        .expect("Failed to open directory");
    let entries = files_async::readdir(&dir).await.expect("readdir failed");
    let listing = entries.iter().map(|entry| entry.name.clone()).collect::<Vec<String>>();
    listing.contains(&String::from(entry_name))
}

pub async fn list_directory<'a>(root_proxy: &'a DirectoryProxy) -> Vec<String> {
    let entries = files_async::readdir(&root_proxy).await.expect("readdir failed");
    let mut items = entries.iter().map(|entry| entry.name.clone()).collect::<Vec<String>>();
    items.sort();
    items
}

pub async fn list_directory_recursive<'a>(root_proxy: &'a DirectoryProxy) -> Vec<String> {
    let dir = io_util::clone_directory(&root_proxy, CLONE_FLAG_SAME_RIGHTS)
        .expect("Failed to clone DirectoryProxy");
    let entries = files_async::readdir_recursive(&dir, /*timeout=*/ None);
    let mut items = entries
        .map(|result| result.map(|entry| entry.name.clone()))
        .try_collect::<Vec<_>>()
        .await
        .expect("readdir failed");
    items.sort();
    items
}

pub async fn read_file<'a>(root_proxy: &'a DirectoryProxy, path: &'a str) -> String {
    let file_proxy = io_util::open_file(&root_proxy, &Path::new(path), OPEN_RIGHT_READABLE)
        .expect("Failed to open file.");
    let res = io_util::read_file(&file_proxy).await;
    res.expect("Unable to read file.")
}

pub async fn write_file<'a>(root_proxy: &'a DirectoryProxy, path: &'a str, contents: &'a str) {
    let file_proxy = io_util::open_file(
        &root_proxy,
        &Path::new(path),
        OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | OPEN_FLAG_CREATE,
    )
    .expect("Failed to open file.");
    let (s, _) = file_proxy.write(contents.as_bytes()).await.expect("Unable to write file.");
    let s = zx::Status::from_raw(s);
    assert_eq!(s, zx::Status::OK, "Write failed");
}

pub async fn call_echo<'a>(root_proxy: &'a DirectoryProxy, path: &'a str) -> String {
    let node_proxy = io_util::open_node(
        &root_proxy,
        &Path::new(path),
        OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
        MODE_TYPE_SERVICE,
    )
    .expect("failed to open echo service");
    let echo_proxy = echo::EchoProxy::new(node_proxy.into_channel().unwrap());
    let res = echo_proxy.echo_string(Some("hippos")).await;
    res.expect("failed to use echo service").expect("no result from echo")
}

/// Create a `DirectoryEntry` and `Channel` pair. The created `DirectoryEntry`
/// provides the service `P`, sending all requests to the returned channel.
pub fn create_service_directory_entry<P>(
) -> (Arc<dyn DirectoryEntry>, futures::channel::mpsc::Receiver<fidl::endpoints::Request<P>>)
where
    P: fidl::endpoints::ProtocolMarker,
    fidl::endpoints::Request<P>: Send,
{
    use futures::sink::SinkExt;
    let (sender, receiver) = futures::channel::mpsc::channel(0);
    let entry = service::host(move |mut stream: P::RequestStream| {
        let mut sender = sender.clone();
        async move {
            while let Ok(Some(request)) = stream.try_next().await {
                sender.send(request).await.unwrap();
            }
        }
    });
    (entry, receiver)
}

/// Wait for a ComponentRunnerStart request, acknowledge it, and return
/// the start info.
///
/// Panics if the channel closes before we receive a request.
pub async fn wait_for_runner_request(
    recv: &mut Receiver<fcrunner::ComponentRunnerRequest>,
) -> fcrunner::ComponentStartInfo {
    let fcrunner::ComponentRunnerRequest::Start { start_info, .. } =
        recv.next().await.expect("Channel closed before request was received.");
    start_info
}

/// Contains test model and ancillary objects.
pub struct TestModelResult {
    pub model: Arc<Model>,
    pub builtin_environment: Arc<Mutex<BuiltinEnvironment>>,
    pub mock_runner: Arc<MockRunner>,
}

pub struct TestEnvironmentBuilder {
    root_component: String,
    components: Vec<(&'static str, ComponentDecl)>,
    runtime_config: RuntimeConfig,
    enable_hub: bool,
}

impl TestEnvironmentBuilder {
    pub fn new() -> Self {
        Self {
            root_component: "root".to_owned(),
            components: vec![],
            runtime_config: Default::default(),
            enable_hub: true,
        }
    }

    pub fn set_root_component(mut self, root_component: &str) -> Self {
        self.root_component = root_component.to_owned();
        self
    }

    pub fn set_components(mut self, components: Vec<(&'static str, ComponentDecl)>) -> Self {
        self.components = components;
        self
    }

    pub fn set_runtime_config(mut self, runtime_config: RuntimeConfig) -> Self {
        self.runtime_config = runtime_config;
        self
    }

    pub fn enable_hub(mut self, val: bool) -> Self {
        self.enable_hub = val;
        self
    }

    pub fn enable_reboot_on_terminate(mut self, val: bool) -> Self {
        self.runtime_config.reboot_on_terminate_enabled = val;
        self
    }

    /// Returns a `Model` and `BuiltinEnvironment` suitable for most tests.
    pub async fn build(mut self) -> TestModelResult {
        let mock_runner = Arc::new(MockRunner::new());

        let mut mock_resolver = MockResolver::new();
        for (name, decl) in &self.components {
            mock_resolver.add_component(name, decl.clone());
        }

        self.runtime_config.root_component_url =
            Some(Url::new(format!("test:///{}", self.root_component)).unwrap());

        let builtin_environment = Arc::new(Mutex::new(
            BuiltinEnvironmentBuilder::new()
                .add_resolver("test".to_string(), Box::new(mock_resolver))
                .add_runner(TEST_RUNNER_NAME.into(), mock_runner.clone())
                .set_runtime_config(self.runtime_config)
                .enable_hub(self.enable_hub)
                .build()
                .await
                .expect("builtin environment setup failed"),
        ));
        let model = builtin_environment.lock().await.model.clone();
        TestModelResult { model, builtin_environment, mock_runner }
    }
}

/// A test harness for tests that wish to register or verify actions.
pub struct ActionsTest {
    pub model: Arc<Model>,
    pub builtin_environment: Arc<Mutex<BuiltinEnvironment>>,
    pub test_hook: Arc<TestHook>,
    pub realm_proxy: Option<fsys::RealmProxy>,
    pub runner: Arc<MockRunner>,
}

impl ActionsTest {
    pub async fn new(
        root_component: &'static str,
        components: Vec<(&'static str, ComponentDecl)>,
        moniker: Option<PartialAbsoluteMoniker>,
    ) -> Self {
        Self::new_with_hooks(root_component, components, moniker, vec![]).await
    }

    pub async fn new_with_hooks(
        root_component: &'static str,
        components: Vec<(&'static str, ComponentDecl)>,
        moniker: Option<PartialAbsoluteMoniker>,
        extra_hooks: Vec<HooksRegistration>,
    ) -> Self {
        let TestModelResult { model, builtin_environment, mock_runner } =
            TestEnvironmentBuilder::new()
                .set_root_component(root_component)
                .set_components(components)
                // Don't install the Hub's hooks because the Hub expects components
                // to start and stop in a certain lifecycle ordering. In particular, some unit
                // tests register individual actions in a way that confuses the hub's expectations.
                .enable_hub(false)
                .build()
                .await;

        let test_hook = Arc::new(TestHook::new());
        model.root().hooks.install(test_hook.hooks()).await;
        model.root().hooks.install(extra_hooks).await;

        // Host framework service for root, if requested.
        let builtin_environment_inner = builtin_environment.clone();
        let realm_proxy = if let Some(moniker) = moniker {
            let (realm_proxy, stream) =
                endpoints::create_proxy_and_stream::<fsys::RealmMarker>().unwrap();
            let component = WeakComponentInstance::from(
                &model.look_up(&moniker).await.expect(&format!("could not look up {}", moniker)),
            );
            fasync::Task::spawn(async move {
                builtin_environment_inner
                    .lock()
                    .await
                    .realm_capability_host
                    .serve(component, stream)
                    .await
                    .expect("failed serving realm service");
            })
            .detach();
            Some(realm_proxy)
        } else {
            None
        };

        Self { model, builtin_environment, test_hook, realm_proxy, runner: mock_runner }
    }

    pub async fn look_up(&self, moniker: PartialAbsoluteMoniker) -> Arc<ComponentInstance> {
        self.model.look_up(&moniker).await.expect(&format!("could not look up {}", moniker))
    }

    pub async fn bind(&self, moniker: PartialAbsoluteMoniker) -> Arc<ComponentInstance> {
        self.model
            .bind(&moniker, &BindReason::Eager)
            .await
            .expect(&format!("could not bind to {}", moniker))
    }

    /// Add a dynamic child to the given collection, with the given name to the
    /// component that our proxy member variable corresponds to.
    pub async fn create_dynamic_child(&self, coll: &str, name: &str) {
        let mut collection_ref = fsys::CollectionRef { name: coll.to_string() };
        let child_decl = ChildDecl {
            name: name.to_string(),
            url: format!("test:///{}", name),
            startup: fsys::StartupMode::Lazy,
            environment: None,
            on_terminate: None,
        }
        .native_into_fidl();
        let res = self
            .realm_proxy
            .as_ref()
            .expect("realm service not started")
            .create_child(&mut collection_ref, child_decl, fsys::CreateChildArgs::EMPTY)
            .await;
        res.expect("failed to create child").expect("failed to create child");
    }
}

/// Create a new local fs and install a mock LogSink service into.
/// Returns the created directory and corresponding namespace entries.
pub fn create_fs_with_mock_logsink(
) -> Result<(MockServiceFs<'static>, Vec<fcrunner::ComponentNamespaceEntry>), Error> {
    let (client, server) = endpoints::create_endpoints::<DirectoryMarker>()
        .context("Failed to create VFS endpoints.")?;
    let mut dir = ServiceFs::new_local();
    dir.add_fidl_service_at(LogSinkMarker::NAME, MockServiceRequest::LogSink);
    dir.serve_connection(server.into_channel()).context("Failed to add serving channel.")?;
    let entries = vec![fcrunner::ComponentNamespaceEntry {
        path: Some("/svc".to_string()),
        directory: Some(client),
        ..fcrunner::ComponentNamespaceEntry::EMPTY
    }];

    Ok((dir, entries))
}

/// Retrieve message logged to socket. The wire format is expected to
/// match with the LogSink protocol format.
pub fn get_message_logged_to_socket(socket: zx::Socket) -> Option<String> {
    let mut buffer: [u8; 1024] = [0; 1024];
    match socket.read(&mut buffer) {
        Ok(read_len) => {
            let msg = Message::from_logger(
                MonikerWithUrl {
                    moniker: "test-pkg/test-component.cmx".to_string(),
                    url: "fuchsia-pkg://fuchsia.com/test-pkg#meta/test-component.cm".to_string(),
                },
                &buffer[..read_len],
            )
            .expect("Couldn't decode message from buffer.");

            (*msg).msg().map(String::from)
        }
        Err(_) => None,
    }
}

/// Create a new event stream for the provided environment.
pub async fn new_event_stream(
    builtin_environment: Arc<Mutex<BuiltinEnvironment>>,
    events: Vec<CapabilityName>,
    mode: EventMode,
) -> (EventSource, EventStream) {
    let mut event_source = builtin_environment
        .as_ref()
        .lock()
        .await
        .event_source_factory
        .create_for_debug()
        .await
        .expect("created event source");
    let event_stream = event_source
        .subscribe(
            events.into_iter().map(|event| EventSubscription::new(event, mode.clone())).collect(),
        )
        .await
        .expect("subscribe to event stream");
    event_source.start_component_tree().await;
    (event_source, event_stream)
}
