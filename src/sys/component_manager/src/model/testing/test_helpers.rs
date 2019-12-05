// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        builtin_environment::BuiltinEnvironment,
        klog,
        model::{
            hooks::HooksRegistration,
            model::{ComponentManagerConfig, Model, ModelParams},
            moniker::{AbsoluteMoniker, PartialMoniker},
            realm::Realm,
            resolver::ResolverRegistry,
            testing::{
                mocks::{ControlMessage, MockResolver, MockRunner},
                test_hook::TestHook,
            },
        },
        startup::Arguments,
    },
    cm_rust::{ChildDecl, ComponentDecl, NativeIntoFidl},
    fidl::endpoints::{self, ServerEnd},
    fidl_fidl_examples_echo as echo, fidl_fuchsia_data as fdata,
    fidl_fuchsia_io::{
        DirectoryProxy, CLONE_FLAG_SAME_RIGHTS, MODE_TYPE_SERVICE, OPEN_FLAG_CREATE,
        OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE,
    },
    fidl_fuchsia_sys2 as fsys, files_async, fuchsia_async as fasync,
    fuchsia_zircon::{self as zx, AsHandleRef, Koid},
    futures::TryStreamExt,
    std::collections::HashSet,
    std::path::Path,
    std::sync::Arc,
};

pub struct ComponentInfo {
    pub realm: Arc<Realm>,
    channel_id: Koid,
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
            let controller = realm
                .runtime
                .as_ref()
                .expect("runtime is unexpectedly missing")
                .controller
                .as_ref()
                .expect("controller is unexpectedly missing");
            let basic_info = controller
                .as_handle_ref()
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
        program: Some(fdata::Dictionary { entries: vec![] }),
        uses: Vec::new(),
        exposes: Vec::new(),
        offers: Vec::new(),
        children: Vec::new(),
        collections: Vec::new(),
        facets: None,
        storage: Vec::new(),
        runners: Vec::new(),
    }
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
    let entries = files_async::readdir_recursive(&dir).await.expect("readdir failed");
    let mut items = entries.iter().map(|entry| entry.name.clone()).collect::<Vec<String>>();
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
    let (s, _) = file_proxy
        .write(&mut contents.as_bytes().to_vec().drain(..))
        .await
        .expect("Unable to write file.");
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
pub fn create_service_directory_entry<S>() -> (
    impl fuchsia_vfs_pseudo_fs::directory::entry::DirectoryEntry + 'static,
    futures::channel::mpsc::Receiver<fidl::endpoints::Request<S>>,
)
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
            fasync::spawn(async move {
                // Convert the stream into a channel of the correct service.
                let server_end: ServerEnd<S> = ServerEnd::new(server_end.into_channel());
                let mut stream: S::RequestStream = server_end.into_stream().unwrap();

                // Keep handling requests until the stream closes or the handler returns an error.
                while let Ok(Some(request)) = stream.try_next().await {
                    sender.send(request).await.unwrap();
                }
            });
        },
    ));
    (entry, receiver)
}

/// A test harness for tests that wish to register or verify actions.
pub struct ActionsTest {
    pub model: Arc<Model>,
    pub builtin_environment: Arc<BuiltinEnvironment>,
    pub test_hook: TestHook,
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

        let mut resolver = ResolverRegistry::new();
        let runner = Arc::new(MockRunner::new());

        let mut mock_resolver = MockResolver::new();
        for (name, decl) in &components {
            mock_resolver.add_component(name, decl.clone());
        }
        resolver.register("test".to_string(), Box::new(mock_resolver));

        let args = Arguments { use_builtin_process_launcher: false, ..Default::default() };
        let model = Arc::new(Model::new(ModelParams {
            root_component_url: format!("test:///{}", root_component),
            root_resolver_registry: resolver,
            elf_runner: runner.clone(),
        }));
        // TODO(fsamuel): Don't install the Hub's hooks because the Hub expects components
        // to start and stop in a certain lifecycle ordering. In particular, some unit
        // tests will destroy component instances before binding to their parents.
        let builtin_environment = Arc::new(
            BuiltinEnvironment::new(&args, &model, ComponentManagerConfig::default())
                .await
                .expect("failed to set up builtin environment"),
        );
        let builtin_environment_inner = builtin_environment.clone();
        let test_hook = TestHook::new();
        model.root_realm.hooks.install(test_hook.hooks()).await;
        model.root_realm.hooks.install(extra_hooks).await;

        // Host framework service for root realm, if requested.
        let realm_proxy = if let Some(realm_moniker) = realm_moniker {
            let (realm_proxy, stream) =
                endpoints::create_proxy_and_stream::<fsys::RealmMarker>().unwrap();
            let realm = model
                .look_up_realm(&realm_moniker)
                .await
                .expect(&format!("could not look up {}", realm_moniker));
            fasync::spawn(async move {
                builtin_environment_inner
                    .realm_capability_host
                    .serve(realm, stream)
                    .await
                    .expect("failed serving realm service");
            });
            Some(realm_proxy)
        } else {
            None
        };

        Self { model, builtin_environment, test_hook, realm_proxy, runner }
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
