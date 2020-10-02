// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        builtin_environment::{BuiltinEnvironment, BuiltinEnvironmentBuilder},
        config::RuntimeConfig,
        klog,
        model::{
            hooks::HooksRegistration,
            model::Model,
            moniker::{AbsoluteMoniker, PartialMoniker},
            realm::Realm,
            rights,
            testing::{
                mocks::{ControlMessage, MockResolver, MockRunner},
                test_hook::TestHook,
            },
        },
        startup::Arguments,
    },
    cm_rust::{ChildDecl, ComponentDecl, NativeIntoFidl},
    fidl::endpoints::{self, Proxy, ServerEnd},
    fidl_fidl_examples_echo as echo, fidl_fuchsia_component_runner as fcrunner,
    fidl_fuchsia_data as fdata,
    fidl_fuchsia_io::{
        DirectoryProxy, CLONE_FLAG_SAME_RIGHTS, MODE_TYPE_SERVICE, OPEN_FLAG_CREATE,
        OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE,
    },
    fidl_fuchsia_io2::{self as fio2},
    fidl_fuchsia_sys2 as fsys, files_async, fuchsia_async as fasync,
    fuchsia_zircon::{self as zx, AsHandleRef, Koid},
    futures::{channel::mpsc::Receiver, StreamExt, TryStreamExt},
    std::collections::HashSet,
    std::default::Default,
    std::path::Path,
    std::sync::Arc,
    vfs::directory::entry::DirectoryEntry,
};

pub struct ComponentInfo {
    pub realm: Arc<Realm>,
    pub channel_id: Koid,
}

impl ComponentInfo {
    /// Given a `Realm` which has been bound, look up the resolved URL
    /// and package into a `ComponentInfo` struct.
    pub async fn new(realm: Arc<Realm>) -> ComponentInfo {
        // The koid is the only unique piece of information we have about
        // a component start request. Two start requests for the same
        // component URL look identical to the Runner, the only difference
        // being the Channel passed to the Runner to use for the
        // ComponentController protocol.
        let koid = {
            let realm = realm.lock_execution().await;
            let runtime = realm.runtime.as_ref().expect("runtime is unexpectedly missing");
            let controller =
                runtime.controller.as_ref().expect("controller is unexpectedly missing");
            let basic_info = controller
                .as_channel()
                .basic_info()
                .expect("error getting basic info about controller channel");
            // should be the koid of the other side of the channel
            basic_info.related_koid
        };

        ComponentInfo { realm, channel_id: koid }
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

        let execution = self.realm.lock_execution().await;
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

        let execution = self.realm.lock_execution().await;
        assert!(execution.runtime.is_some());
        assert!(!execution.is_shut_down());
    }
}

pub async fn execution_is_shut_down(realm: &Realm) -> bool {
    let execution = realm.lock_execution().await;
    execution.runtime.is_none() && execution.is_shut_down()
}

/// Returns true if the given child realm (live or deleting) exists.
pub async fn has_child<'a>(realm: &'a Realm, moniker: &'a str) -> bool {
    realm
        .lock_state()
        .await
        .as_ref()
        .expect("not resolved")
        .all_child_realms()
        .contains_key(&moniker.into())
}

/// Return the instance id of the given live child.
pub async fn get_instance_id<'a>(realm: &'a Realm, moniker: &'a str) -> u32 {
    realm
        .lock_state()
        .await
        .as_ref()
        .expect("not resolved")
        .get_live_child_instance_id(&moniker.into())
        .unwrap()
}

/// Return all monikers of the live children of the given `realm`.
pub async fn get_live_children(realm: &Realm) -> HashSet<PartialMoniker> {
    realm
        .lock_state()
        .await
        .as_ref()
        .expect("not resolved")
        .live_child_realms()
        .map(|(m, _)| m.clone())
        .collect()
}

/// Return the child realm of the given `realm` with moniker `child`.
pub async fn get_live_child<'a>(realm: &'a Realm, child: &'a str) -> Arc<Realm> {
    realm
        .lock_state()
        .await
        .as_ref()
        .expect("not resolved")
        .get_live_child_realm(&child.into())
        .unwrap()
        .clone()
}

/// Returns an empty component decl for an executable component.
pub fn default_component_decl() -> ComponentDecl {
    ComponentDecl {
        program: Some(fdata::Dictionary { entries: Some(vec![]) }),
        ..Default::default()
    }
}

/// Name of the test runner.
///
/// Several functions assume the existance of a runner with this name.
pub const TEST_RUNNER_NAME: &str = "test_runner";

/// Returns an empty component decl set up to have a non-empty program and to use the "test_runner"
/// runner.
pub fn component_decl_with_test_runner() -> ComponentDecl {
    ComponentDecl {
        program: Some(fdata::Dictionary { entries: Some(vec![]) }),
        uses: vec![cm_rust::UseDecl::Runner(cm_rust::UseRunnerDecl {
            source_name: TEST_RUNNER_NAME.into(),
        })],
        ..Default::default()
    }
}

/// Builder for constructing a ComponentDecl.
#[derive(Debug, Clone)]
pub struct ComponentDeclBuilder {
    result: ComponentDecl,
}

impl ComponentDeclBuilder {
    /// An empty ComponentDeclBuilder, with no program.
    pub fn new_empty_component() -> Self {
        ComponentDeclBuilder { result: Default::default() }
    }

    /// A ComponentDeclBuilder prefilled with a program and using a runner named "test_runner",
    /// which we assume is offered to us.
    pub fn new() -> Self {
        Self::new_empty_component().use_runner(TEST_RUNNER_NAME)
    }

    /// Add a child element.
    pub fn add_child(mut self, decl: impl Into<cm_rust::ChildDecl>) -> Self {
        self.result.children.push(decl.into());
        self
    }

    // Add a collection element.
    pub fn add_collection(mut self, decl: impl Into<cm_rust::CollectionDecl>) -> Self {
        self.result.collections.push(decl.into());
        self
    }

    /// Add a lazily instantiated child with a default test URL derived from the name.
    pub fn add_lazy_child(self, name: &str) -> Self {
        self.add_child(ChildDeclBuilder::new_lazy_child(name))
    }

    /// Add an eagerly instantiated child with a default test URL derived from the name.
    pub fn add_eager_child(self, name: &str) -> Self {
        self.add_child(
            ChildDeclBuilder::new()
                .name(name)
                .url(&format!("test:///{}", name))
                .startup(fsys::StartupMode::Eager),
        )
    }

    /// Add a transient collection.
    pub fn add_transient_collection(self, name: &str) -> Self {
        self.add_collection(CollectionDeclBuilder::new_transient_collection(name))
    }

    /// Add a "use" clause, using the given runner.
    pub fn use_runner(mut self, name: &str) -> Self {
        self.result
            .uses
            .push(cm_rust::UseDecl::Runner(cm_rust::UseRunnerDecl { source_name: name.into() }));
        self
    }

    /// Add a custom offer.
    pub fn offer(mut self, offer: cm_rust::OfferDecl) -> Self {
        self.result.offers.push(offer);
        self
    }

    /// Add a custom expose.
    pub fn expose(mut self, expose: cm_rust::ExposeDecl) -> Self {
        self.result.exposes.push(expose);
        self
    }

    /// Add a custom use decl.
    pub fn use_(mut self, use_: cm_rust::UseDecl) -> Self {
        self.result.uses.push(use_);
        self
    }

    /// Add a custom protocol declaration.
    pub fn protocol(mut self, protocol: cm_rust::ProtocolDecl) -> Self {
        self.result.capabilities.push(cm_rust::CapabilityDecl::Protocol(protocol));
        self
    }

    /// Add a custom directory declaration.
    pub fn directory(mut self, directory: cm_rust::DirectoryDecl) -> Self {
        self.result.capabilities.push(cm_rust::CapabilityDecl::Directory(directory));
        self
    }

    /// Add a custom storage declaration.
    pub fn storage(mut self, storage: cm_rust::StorageDecl) -> Self {
        self.result.capabilities.push(cm_rust::CapabilityDecl::Storage(storage));
        self
    }

    /// Add a custom runner declaration.
    pub fn runner(mut self, runner: cm_rust::RunnerDecl) -> Self {
        self.result.capabilities.push(cm_rust::CapabilityDecl::Runner(runner));
        self
    }

    /// Add an environment declaration.
    pub fn add_environment(mut self, environment: impl Into<cm_rust::EnvironmentDecl>) -> Self {
        self.result.environments.push(environment.into());
        self
    }

    /// Generate the final ComponentDecl.
    pub fn build(self) -> ComponentDecl {
        self.result
    }
}

/// A convenience builder for constructing ChildDecls.
#[derive(Debug)]
pub struct ChildDeclBuilder(cm_rust::ChildDecl);

impl ChildDeclBuilder {
    /// Creates a new builder.
    pub fn new() -> Self {
        ChildDeclBuilder(cm_rust::ChildDecl {
            name: String::new(),
            url: String::new(),
            startup: fsys::StartupMode::Lazy,
            environment: None,
        })
    }

    /// Creates a new builder initialized with a lazy child.
    pub fn new_lazy_child(name: &str) -> Self {
        Self::new().name(name).url(&format!("test:///{}", name)).startup(fsys::StartupMode::Lazy)
    }

    /// Sets the ChildDecl's name.
    pub fn name(mut self, name: &str) -> Self {
        self.0.name = name.to_string();
        self
    }

    /// Sets the ChildDecl's url.
    pub fn url(mut self, url: &str) -> Self {
        self.0.url = url.to_string();
        self
    }

    /// Sets the ChildDecl's startup mode.
    pub fn startup(mut self, startup: fsys::StartupMode) -> Self {
        self.0.startup = startup;
        self
    }

    /// Sets the ChildDecl's environment name.
    pub fn environment(mut self, environment: &str) -> Self {
        self.0.environment = Some(environment.to_string());
        self
    }

    /// Consumes the builder and returns a ChildDecl.
    pub fn build(self) -> cm_rust::ChildDecl {
        self.0
    }
}

impl From<ChildDeclBuilder> for cm_rust::ChildDecl {
    fn from(builder: ChildDeclBuilder) -> Self {
        builder.build()
    }
}

/// A convenience builder for constructing CollectionDecls.
#[derive(Debug)]
pub struct CollectionDeclBuilder(cm_rust::CollectionDecl);

impl CollectionDeclBuilder {
    /// Creates a new builder.
    pub fn new() -> Self {
        CollectionDeclBuilder(cm_rust::CollectionDecl {
            name: String::new(),
            durability: fsys::Durability::Transient,
            environment: None,
        })
    }

    /// Creates a new builder initialized with a transient collection.
    pub fn new_transient_collection(name: &str) -> Self {
        Self::new().name(name).durability(fsys::Durability::Transient)
    }

    /// Sets the CollectionDecl's name.
    pub fn name(mut self, name: &str) -> Self {
        self.0.name = name.to_string();
        self
    }

    /// Sets the CollectionDecl's durability
    pub fn durability(mut self, durability: fsys::Durability) -> Self {
        self.0.durability = durability;
        self
    }

    /// Sets the CollectionDecl's environment name.
    pub fn environment(mut self, environment: &str) -> Self {
        self.0.environment = Some(environment.to_string());
        self
    }

    /// Consumes the builder and returns a CollectionDecl.
    pub fn build(self) -> cm_rust::CollectionDecl {
        self.0
    }
}

impl From<CollectionDeclBuilder> for cm_rust::CollectionDecl {
    fn from(builder: CollectionDeclBuilder) -> Self {
        builder.build()
    }
}

/// A convenience builder for constructing EnvironmentDecls.
#[derive(Debug)]
pub struct EnvironmentDeclBuilder(cm_rust::EnvironmentDecl);

impl EnvironmentDeclBuilder {
    /// Creates a new builder.
    pub fn new() -> Self {
        EnvironmentDeclBuilder(cm_rust::EnvironmentDecl {
            name: String::new(),
            extends: fsys::EnvironmentExtends::None,
            runners: vec![],
            resolvers: vec![],
            stop_timeout_ms: None,
        })
    }

    /// Sets the EnvironmentDecl's name.
    pub fn name(mut self, name: &str) -> Self {
        self.0.name = name.to_string();
        self
    }

    /// Sets whether the environment extends from its realm.
    pub fn extends(mut self, extends: fsys::EnvironmentExtends) -> Self {
        self.0.extends = extends;
        self
    }

    /// Registers a runner with the environment.
    pub fn add_runner(mut self, runner: cm_rust::RunnerRegistration) -> Self {
        self.0.runners.push(runner);
        self
    }

    /// Registers a resolver with the environment.
    pub fn add_resolver(mut self, resolver: cm_rust::ResolverRegistration) -> Self {
        self.0.resolvers.push(resolver);
        self
    }

    pub fn stop_timeout(mut self, timeout_ms: u32) -> Self {
        self.0.stop_timeout_ms = Some(timeout_ms);
        self
    }

    /// Consumes the builder and returns an EnvironmentDecl.
    pub fn build(self) -> cm_rust::EnvironmentDecl {
        self.0
    }
}

impl From<EnvironmentDeclBuilder> for cm_rust::EnvironmentDecl {
    fn from(builder: EnvironmentDeclBuilder) -> Self {
        builder.build()
    }
}

// A convenience builder for constructing ProtocolDecls.
#[derive(Debug)]
pub struct ProtocolDeclBuilder(cm_rust::ProtocolDecl);

impl ProtocolDeclBuilder {
    /// Creates a new builder.
    pub fn new(name: &str) -> Self {
        Self(cm_rust::ProtocolDecl {
            name: name.into(),
            source_path: format!("/svc/foo").parse().unwrap(),
        })
    }

    /// Sets the source path.
    pub fn path(mut self, path: &str) -> Self {
        self.0.source_path = path.parse().unwrap();
        self
    }

    /// Consumes the builder and returns a ProtocolDecl.
    pub fn build(self) -> cm_rust::ProtocolDecl {
        self.0
    }
}

impl From<ProtocolDeclBuilder> for cm_rust::ProtocolDecl {
    fn from(builder: ProtocolDeclBuilder) -> Self {
        builder.build()
    }
}

// A convenience builder for constructing DirectoryDecls.
#[derive(Debug)]
pub struct DirectoryDeclBuilder(cm_rust::DirectoryDecl);

impl DirectoryDeclBuilder {
    /// Creates a new builder.
    pub fn new(name: &str) -> Self {
        Self(cm_rust::DirectoryDecl {
            name: name.into(),
            source_path: format!("/data/foo").parse().unwrap(),
            rights: *rights::READ_RIGHTS,
        })
    }

    /// Sets the source path.
    pub fn path(mut self, path: &str) -> Self {
        self.0.source_path = path.parse().unwrap();
        self
    }

    /// Sets the rights.
    pub fn rights(mut self, rights: fio2::Operations) -> Self {
        self.0.rights = rights;
        self
    }

    /// Consumes the builder and returns a DirectoryDecl.
    pub fn build(self) -> cm_rust::DirectoryDecl {
        self.0
    }
}

impl From<DirectoryDeclBuilder> for cm_rust::DirectoryDecl {
    fn from(builder: DirectoryDeclBuilder) -> Self {
        builder.build()
    }
}

/// Create a fsys::OfferRunnerDecl offering the given cap from the parent to the given child
/// component.
pub fn offer_runner_cap_to_child(runner_cap: &str, child: &str) -> cm_rust::OfferDecl {
    cm_rust::OfferDecl::Runner(cm_rust::OfferRunnerDecl {
        source: cm_rust::OfferRunnerSource::Parent,
        source_name: runner_cap.into(),
        target: cm_rust::OfferTarget::Child(child.to_string()),
        target_name: runner_cap.into(),
    })
}

/// Create a fsys::OfferRunnerDecl offering the given cap from the parent to the given child
/// collection.
pub fn offer_runner_cap_to_collection(runner_cap: &str, child: &str) -> cm_rust::OfferDecl {
    cm_rust::OfferDecl::Runner(cm_rust::OfferRunnerDecl {
        source: cm_rust::OfferRunnerSource::Parent,
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
    let node_proxy =
        io_util::open_node(&root_proxy, &Path::new(path), OPEN_RIGHT_READABLE, MODE_TYPE_SERVICE)
            .expect("failed to open echo service");
    let echo_proxy = echo::EchoProxy::new(node_proxy.into_channel().unwrap());
    let res = echo_proxy.echo_string(Some("hippos")).await;
    res.expect("failed to use echo service").expect("no result from echo")
}

/// Create a `DirectoryEntry` and `Channel` pair. The created `DirectoryEntry`
/// provides the service `S`, sending all requests to the returned channel.
pub fn create_service_directory_entry<S>(
) -> (Arc<dyn DirectoryEntry>, futures::channel::mpsc::Receiver<fidl::endpoints::Request<S>>)
where
    S: fidl::endpoints::ServiceMarker,
    fidl::endpoints::Request<S>: Send,
{
    use futures::sink::SinkExt;
    let (sender, receiver) = futures::channel::mpsc::channel(0);
    let entry = directory_broker::DirectoryBroker::new(Box::new(
        move |_flags: u32,
              _mode: u32,
              _relative_path: String,
              server_end: ServerEnd<fidl_fuchsia_io::NodeMarker>| {
            let mut sender = sender.clone();
            fasync::Task::spawn(async move {
                // Convert the stream into a channel of the correct service.
                let server_end: ServerEnd<S> = ServerEnd::new(server_end.into_channel());
                let mut stream: S::RequestStream = server_end.into_stream().unwrap();

                // Keep handling requests until the stream closes or the handler returns an error.
                while let Ok(Some(request)) = stream.try_next().await {
                    sender.send(request).await.unwrap();
                }
            })
            .detach();
        },
    ));
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
    pub builtin_environment: Arc<BuiltinEnvironment>,
    pub mock_runner: Arc<MockRunner>,
}

/// Returns a `Model` and `BuiltinEnvironment` suitable for most tests.
// TODO: Consider turning this into a builder.
pub async fn new_test_model(
    root_component: &str,
    components: Vec<(&str, ComponentDecl)>,
    runtime_config: RuntimeConfig,
) -> TestModelResult {
    let mock_runner = Arc::new(MockRunner::new());

    let mut mock_resolver = MockResolver::new();
    for (name, decl) in &components {
        mock_resolver.add_component(name, decl.clone());
    }

    let args = Arguments {
        root_component_url: format!("test:///{}", root_component),
        ..Default::default()
    };

    let builtin_environment = Arc::new(
        BuiltinEnvironmentBuilder::new()
            .set_args(args)
            .add_resolver("test".to_string(), Box::new(mock_resolver))
            .unwrap()
            .add_runner(TEST_RUNNER_NAME.into(), mock_runner.clone())
            .set_runtime_config(runtime_config)
            .build()
            .await
            .expect("builtin environment setup failed"),
    );
    TestModelResult { model: builtin_environment.model.clone(), builtin_environment, mock_runner }
}

/// A test harness for tests that wish to register or verify actions.
pub struct ActionsTest {
    pub model: Arc<Model>,
    pub builtin_environment: Arc<BuiltinEnvironment>,
    pub test_hook: Arc<TestHook>,
    pub realm_proxy: Option<fsys::RealmProxy>,
    pub runner: Arc<MockRunner>,
}

impl ActionsTest {
    pub async fn new(
        root_component: &'static str,
        components: Vec<(&'static str, ComponentDecl)>,
        realm_moniker: Option<AbsoluteMoniker>,
    ) -> Self {
        Self::new_with_hooks(root_component, components, realm_moniker, vec![]).await
    }

    pub async fn new_with_hooks(
        root_component: &'static str,
        components: Vec<(&'static str, ComponentDecl)>,
        realm_moniker: Option<AbsoluteMoniker>,
        extra_hooks: Vec<HooksRegistration>,
    ) -> Self {
        // Ensure that kernel logging has been set up
        let _ = klog::KernelLogger::init();

        let TestModelResult { model, builtin_environment, mock_runner } =
            new_test_model(root_component, components, RuntimeConfig::default()).await;

        // TODO(fsamuel): Don't install the Hub's hooks because the Hub expects components
        // to start and stop in a certain lifecycle ordering. In particular, some unit
        // tests will destroy component instances before binding to their parents.
        let test_hook = Arc::new(TestHook::new());
        model.root_realm.hooks.install(test_hook.hooks()).await;
        model.root_realm.hooks.install(extra_hooks).await;

        // Host framework service for root realm, if requested.
        let builtin_environment_inner = builtin_environment.clone();
        let realm_proxy = if let Some(realm_moniker) = realm_moniker {
            let (realm_proxy, stream) =
                endpoints::create_proxy_and_stream::<fsys::RealmMarker>().unwrap();
            fasync::Task::spawn(async move {
                builtin_environment_inner
                    .realm_capability_host
                    .serve(realm_moniker, stream)
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

    pub async fn look_up(&self, moniker: AbsoluteMoniker) -> Arc<Realm> {
        self.model.look_up_realm(&moniker).await.expect(&format!("could not look up {}", moniker))
    }

    /// Add a dynamic child to the given collection, with the given name to the
    /// realm that our proxy member variable corresponds to.
    pub async fn create_dynamic_child(&self, coll: &str, name: &str) {
        let mut collection_ref = fsys::CollectionRef { name: coll.to_string() };
        let child_decl = ChildDecl {
            name: name.to_string(),
            url: format!("test:///{}", name),
            startup: fsys::StartupMode::Lazy,
            environment: None,
        }
        .native_into_fidl();
        let res = self
            .realm_proxy
            .as_ref()
            .expect("realm service not started")
            .create_child(&mut collection_ref, child_decl)
            .await;
        res.expect("failed to create child").expect("failed to create child");
    }
}
