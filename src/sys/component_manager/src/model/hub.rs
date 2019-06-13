// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::directory_broker,
    crate::model::{self, error::ModelError},
    failure::Fail,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{NodeMarker, OPEN_FLAG_DESCRIBE, OPEN_RIGHT_READABLE},
    fuchsia_async as fasync,
    fuchsia_vfs_pseudo_fs::{directory, file::simple::read_only},
    futures::{
        future::{AbortHandle, Abortable, BoxFuture},
        lock::Mutex,
    },
    std::{collections::HashMap, sync::Arc},
};

/// Hub state on an instance of a component.
struct Instance {
    pub abs_moniker: model::AbsoluteMoniker,
    pub component_url: String,
    pub execution: Option<Execution>,
    pub directory: directory::controlled::Controller<'static>,
    pub children_directory: directory::controlled::Controller<'static>,
}

/// The execution state for a component that has started running.
struct Execution {
    pub resolved_url: String,
    pub directory: directory::controlled::Controller<'static>,
}

/// Errors produced by `Hub`.
#[derive(Debug, Fail)]
pub enum HubError {
    #[fail(display = "Failed to add directory entry \"{}\" for \"{}\"", abs_moniker, entry_name)]
    AddDirectoryEntryError { abs_moniker: model::AbsoluteMoniker, entry_name: String },
}

impl HubError {
    pub fn add_directory_entry_error(
        abs_moniker: model::AbsoluteMoniker,
        entry_name: &str,
    ) -> HubError {
        HubError::AddDirectoryEntryError { abs_moniker, entry_name: entry_name.to_string() }
    }
}

pub struct Hub {
    instances: Mutex<HashMap<model::AbsoluteMoniker, Instance>>,
    /// Called when Hub is dropped to drop pseudodirectory hosting the Hub.
    abort_handle: AbortHandle,
}

impl Drop for Hub {
    fn drop(&mut self) {
        self.abort_handle.abort();
    }
}

impl Hub {
    /// Create a new Hub given a |component_url| and a controller to the root directory.
    pub fn new(
        component_url: String,
        mut root_directory: directory::simple::Simple<'static>,
    ) -> Result<Hub, ModelError> {
        let mut instances_map = HashMap::new();
        let abs_moniker = model::AbsoluteMoniker::root();

        let self_directory =
            Hub::add_instance_if_necessary(&abs_moniker, component_url, &mut instances_map)?
                .expect("Did not create directory.");
        root_directory
            .add_entry("self", self_directory)
            .map_err(|_| HubError::add_directory_entry_error(abs_moniker.clone(), "self"))?;

        // Run the hub root directory forever until the component manager is terminated.
        let (abort_handle, abort_registration) = AbortHandle::new_pair();
        let future = Abortable::new(root_directory, abort_registration);
        fasync::spawn(async move {
            let _ = await!(future);
        });

        Ok(Hub { instances: Mutex::new(instances_map), abort_handle })
    }

    fn add_instance_if_necessary(
        abs_moniker: &model::AbsoluteMoniker,
        component_url: String,
        instance_map: &mut HashMap<model::AbsoluteMoniker, Instance>,
    ) -> Result<Option<directory::controlled::Controlled<'static>>, HubError> {
        if instance_map.contains_key(&abs_moniker) {
            return Ok(None);
        }

        let (controller, mut controlled) =
            directory::controlled::controlled(directory::simple::empty());

        // Add a 'url' file.
        controlled
            .add_entry("url", {
                let url = component_url.clone();
                read_only(move || Ok(url.clone().into_bytes()))
            })
            .map_err(|_| HubError::add_directory_entry_error(abs_moniker.clone(), "url"))?;

        // Add a children directory.
        let (children_controller, children_controlled) =
            directory::controlled::controlled(directory::simple::empty());
        controlled
            .add_entry("children", children_controlled)
            .map_err(|_| HubError::add_directory_entry_error(abs_moniker.clone(), "children"))?;

        instance_map.insert(
            abs_moniker.clone(),
            Instance {
                abs_moniker: abs_moniker.clone(),
                component_url,
                execution: None,
                directory: controller,
                children_directory: children_controller,
            },
        );

        Ok(Some(controlled))
    }

    async fn add_instance_to_parent_if_necessary<'a>(
        abs_moniker: &'a model::AbsoluteMoniker,
        component_url: String,
        mut instances_map: &'a mut HashMap<model::AbsoluteMoniker, Instance>,
    ) -> Result<(), HubError> {
        if let Some(controlled) =
            Hub::add_instance_if_necessary(&abs_moniker, component_url, &mut instances_map)?
        {
            if let (Some(name), Some(parent_moniker)) = (abs_moniker.name(), abs_moniker.parent()) {
                await!(instances_map[&parent_moniker]
                    .children_directory
                    .add_entry_res(name.clone(), controlled))
                .map_err(|_| {
                    HubError::add_directory_entry_error(abs_moniker.clone(), &name.clone())
                })?;
            }
        }
        Ok(())
    }

    pub async fn on_bind_instance_async<'a>(
        &'a self,
        realm: Arc<model::Realm>,
        realm_state: &'a model::RealmState,
    ) -> Result<(), ModelError> {
        let component_url = realm.component_url.clone();
        let abs_moniker = realm.abs_moniker.clone();
        let mut instances_map = await!(self.instances.lock());

        await!(Self::add_instance_to_parent_if_necessary(
            &abs_moniker,
            component_url,
            &mut instances_map
        ))?;

        let instance = instances_map
            .get_mut(&abs_moniker)
            .expect(&format!("Unable to find instance {} in map.", abs_moniker));

        // If we haven't already created an execution directory, create one now.
        if instance.execution.is_none() {
            let (controller, mut controlled) =
                directory::controlled::controlled(directory::simple::empty());

            if let Some(execution) = realm_state.execution.as_ref() {
                let exec = Execution {
                    resolved_url: execution.resolved_url.clone(),
                    directory: controller,
                };
                instance.execution = Some(exec);

                // Add a 'resolved_url' file.
                controlled
                    .add_entry("resolved_url", {
                        let resolved_url = execution.resolved_url.clone();
                        read_only(move || Ok(resolved_url.clone().into_bytes()))
                    })
                    .map_err(|_| {
                        HubError::add_directory_entry_error(abs_moniker.clone(), "resolved_url")
                    })?;

                // Install the out directory if we can successfully clone it.
                // TODO(fsamuel): We should probably preserve the original error messages
                // instead of dropping them.
                if let Ok(out_dir) = io_util::clone_directory(&execution.outgoing_dir) {
                    // `route_open_fn` will cause any call on the hub's `out` directory to be
                    // redirected to the component's outgoing directory. All directory
                    // operations other than `Open` will be received by the outgoing
                    // directory, because those calls are preceded by an `Open` on a path
                    // in the hub's `out`.
                    let route_open_fn = Box::new(
                        move |flags: u32,
                              mode: u32,
                              relative_path: String,
                              server_end: ServerEnd<NodeMarker>| {
                            // If we want to open the out directory directly, then call clone.
                            // Otherwise, pass long the remaining 'relative_path' to the component
                            // hosting the out directory to resolve.
                            if !relative_path.is_empty() {
                                // TODO(fsamuel): Currently DirectoryEntry::open does not return
                                // a Result so we cannot propagate this error up. We probably
                                // want to change that.
                                let _ = out_dir.open(flags, mode, &relative_path, server_end);
                            } else {
                                let _ = out_dir
                                    .clone(OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE, server_end);
                            }
                        },
                    );
                    controlled
                        .add_entry("out", directory_broker::DirectoryBroker::new(route_open_fn))
                        .map_err(|_| {
                            HubError::add_directory_entry_error(abs_moniker.clone(), "out")
                        })?;
                }

                // Mount the runtime directory if we can successfully clone it.
                if let Ok(runtime_dir) = io_util::clone_directory(&execution.runtime_dir) {
                    let route_open_fn = Box::new(
                        move |flags: u32,
                              mode: u32,
                              relative_path: String,
                              server_end: ServerEnd<NodeMarker>| {
                            if !relative_path.is_empty() {
                                let _ = runtime_dir.open(flags, mode, &relative_path, server_end);
                            } else {
                                let _ = runtime_dir
                                    .clone(OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE, server_end);
                            }
                        },
                    );
                    controlled
                        .add_entry("runtime", directory_broker::DirectoryBroker::new(route_open_fn))
                        .map_err(|_| {
                            HubError::add_directory_entry_error(abs_moniker.clone(), "runtime")
                        })?;
                }

                await!(instance.directory.add_entry_res("exec", controlled)).map_err(|_| {
                    HubError::add_directory_entry_error(abs_moniker.clone(), "exec")
                })?;
            }
        }
        for child_realm in
            realm_state.child_realms.as_ref().expect("Unable to access child realms.").values()
        {
            await!(Self::add_instance_to_parent_if_necessary(
                &child_realm.abs_moniker,
                child_realm.component_url.clone(),
                &mut instances_map
            ))?;
        }

        Ok(())
    }
}

impl model::Hook for Hub {
    fn on_bind_instance<'a>(
        &'a self,
        realm: Arc<model::Realm>,
        realm_state: &'a model::RealmState,
    ) -> BoxFuture<Result<(), ModelError>> {
        Box::pin(self.on_bind_instance_async(realm, realm_state))
    }

    fn on_add_dynamic_child(&self, _realm: Arc<model::Realm>) -> BoxFuture<Result<(), ModelError>> {
        // TODO: Update the hub with the new child
        Box::pin(async { Ok(()) })
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::model::{
            self, hub::Hub, testing::mocks, testing::routing_test_helpers::default_component_decl,
        },
        cm_rust::{self, ChildDecl, ComponentDecl},
        fidl::endpoints::{ClientEnd, ServerEnd},
        fidl_fuchsia_io::{
            DirectoryMarker, DirectoryProxy, NodeMarker, MODE_TYPE_DIRECTORY, OPEN_RIGHT_READABLE,
            OPEN_RIGHT_WRITABLE,
        },
        fidl_fuchsia_sys2 as fsys, files_async,
        fuchsia_vfs_pseudo_fs::directory::entry::DirectoryEntry,
        fuchsia_zircon as zx,
        std::{iter, path::PathBuf},
    };

    /// Hosts an out directory with a 'foo' file.
    fn foo_out_dir_fn() -> Box<Fn(ServerEnd<DirectoryMarker>) + Send + Sync> {
        Box::new(move |server_end: ServerEnd<DirectoryMarker>| {
            let mut out_dir = directory::simple::empty();
            // Add a 'foo' file.
            out_dir
                .add_entry("foo", { read_only(move || Ok(b"bar".to_vec())) })
                .map_err(|(s, _)| s)
                .expect("Failed to add 'foo' entry");

            out_dir.open(
                OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
                MODE_TYPE_DIRECTORY,
                &mut iter::empty(),
                ServerEnd::new(server_end.into_channel()),
            );

            let mut test_dir = directory::simple::empty();
            test_dir
                .add_entry("aaa", { read_only(move || Ok(b"bbb".to_vec())) })
                .map_err(|(s, _)| s)
                .expect("Failed to add 'aaa' entry");
            out_dir
                .add_entry("test", test_dir)
                .map_err(|(s, _)| s)
                .expect("Failed to add 'test' directory.");

            fasync::spawn(async move {
                let _ = await!(out_dir);
            });
        })
    }

    async fn read_file<'a>(root_proxy: &'a DirectoryProxy, path: &'a str) -> String {
        let file_proxy =
            io_util::open_file(&root_proxy, &PathBuf::from(path)).expect("Failed to open file.");
        let res = await!(io_util::read_file(&file_proxy));
        res.expect("Unable to read file.")
    }

    async fn dir_contains<'a>(
        root_proxy: &'a DirectoryProxy,
        path: &'a str,
        entry_name: &'a str,
    ) -> bool {
        let dir = io_util::open_directory(&root_proxy, &PathBuf::from(path))
            .expect("Failed to open directory");
        let entries = await!(files_async::readdir(&dir)).expect("readdir failed");
        let listing = entries.iter().map(|entry| entry.name.clone()).collect::<Vec<String>>();
        listing.contains(&String::from(entry_name))
    }

    /// Hosts a runtime directory with a 'bleep' file.
    fn bleep_runtime_dir_fn() -> Box<Fn(ServerEnd<DirectoryMarker>) + Send + Sync> {
        Box::new(move |server_end: ServerEnd<DirectoryMarker>| {
            let mut pseudo_dir = directory::simple::empty();
            // Add a 'bleep' file.
            pseudo_dir
                .add_entry("bleep", { read_only(move || Ok(b"blah".to_vec())) })
                .map_err(|(s, _)| s)
                .expect("Failed to add 'bleep' entry");

            pseudo_dir.open(
                OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
                MODE_TYPE_DIRECTORY,
                &mut iter::empty(),
                ServerEnd::new(server_end.into_channel()),
            );
            fasync::spawn(async move {
                let _ = await!(pseudo_dir);
            });
        })
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn run_hub_basic() {
        let mut resolver = model::ResolverRegistry::new();
        let runner = mocks::MockRunner::new();
        let mut mock_resolver = mocks::MockResolver::new();
        mock_resolver.add_component(
            "root",
            ComponentDecl {
                children: vec![ChildDecl {
                    name: "a".to_string(),
                    url: "test:///a".to_string(),
                    startup: fsys::StartupMode::Lazy,
                }],
                ..default_component_decl()
            },
        );
        mock_resolver
            .add_component("a", ComponentDecl { children: vec![], ..default_component_decl() });
        resolver.register("test".to_string(), Box::new(mock_resolver));
        let mut hooks: model::Hooks = Vec::new();
        let mut root_directory = directory::simple::empty();

        let (client_chan, server_chan) = zx::Channel::create().unwrap();
        root_directory.open(
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            0,
            &mut iter::empty(),
            ServerEnd::<NodeMarker>::new(server_chan.into()),
        );

        let root_component_url = "test:///root".to_string();
        let hub = Arc::new(Hub::new(root_component_url.clone(), root_directory).unwrap());
        hooks.push(hub);
        let model = model::Model::new(model::ModelParams {
            ambient: Box::new(mocks::MockAmbientEnvironment::new()),
            root_component_url: root_component_url.clone(),
            root_resolver_registry: resolver,
            root_default_runner: Box::new(runner),
            hooks,
        });

        let res = await!(model.look_up_and_bind_instance(model::AbsoluteMoniker::root()));
        let expected_res: Result<(), model::ModelError> = Ok(());
        assert_eq!(format!("{:?}", res), format!("{:?}", expected_res));

        let hub_proxy = ClientEnd::<DirectoryMarker>::new(client_chan)
            .into_proxy()
            .expect("failed to create directory proxy");

        assert_eq!(root_component_url, await!(read_file(&hub_proxy, "self/url")));
        assert_eq!(
            format!("{}_resolved", root_component_url),
            await!(read_file(&hub_proxy, "self/exec/resolved_url"))
        );
        assert_eq!("test:///a", await!(read_file(&hub_proxy, "self/children/a/url")));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn run_hub_out_directory() {
        let mut resolver = model::ResolverRegistry::new();
        let mut runner = mocks::MockRunner::new();
        let root_component_url = "test:///root".to_string();
        let resolved_root_component_url = format!("{}_resolved", root_component_url);
        runner.host_fns.insert(resolved_root_component_url, foo_out_dir_fn());
        let mut mock_resolver = mocks::MockResolver::new();
        mock_resolver.add_component("root", default_component_decl());
        resolver.register("test".to_string(), Box::new(mock_resolver));
        let mut hooks: model::Hooks = Vec::new();
        let mut root_directory = directory::simple::empty();

        let (client_chan, server_chan) = zx::Channel::create().unwrap();
        root_directory.open(
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            0,
            &mut iter::empty(),
            ServerEnd::<NodeMarker>::new(server_chan.into()),
        );

        let hub = Arc::new(Hub::new(root_component_url.clone(), root_directory).unwrap());
        hooks.push(hub);
        let model = model::Model::new(model::ModelParams {
            ambient: Box::new(mocks::MockAmbientEnvironment::new()),
            root_component_url: root_component_url.clone(),
            root_resolver_registry: resolver,
            root_default_runner: Box::new(runner),
            hooks,
        });

        // Bind to the top component, and check that it and the eager components were started.
        let res = await!(model.look_up_and_bind_instance(model::AbsoluteMoniker::root()));
        let expected_res: Result<(), model::ModelError> = Ok(());
        assert_eq!(format!("{:?}", res), format!("{:?}", expected_res));

        let hub_proxy = ClientEnd::<DirectoryMarker>::new(client_chan)
            .into_proxy()
            .expect("failed to create directory proxy");

        assert!(await!(dir_contains(&hub_proxy, "self/exec", "out")));
        assert!(await!(dir_contains(&hub_proxy, "self/exec/out", "foo")));
        assert!(await!(dir_contains(&hub_proxy, "self/exec/out/test", "aaa")));
        assert_eq!("bar", await!(read_file(&hub_proxy, "self/exec/out/foo")));
        assert_eq!("bbb", await!(read_file(&hub_proxy, "self/exec/out/test/aaa")));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn run_hub_runtime_directory() {
        let mut resolver = model::ResolverRegistry::new();
        let mut runner = mocks::MockRunner::new();
        let root_component_url = "test:///root".to_string();
        let resolved_root_component_url = format!("{}_resolved", root_component_url);
        runner.runtime_host_fns.insert(resolved_root_component_url, bleep_runtime_dir_fn());
        let mut mock_resolver = mocks::MockResolver::new();
        mock_resolver.add_component("root", default_component_decl());
        resolver.register("test".to_string(), Box::new(mock_resolver));
        let mut hooks: model::Hooks = Vec::new();
        let mut root_directory = directory::simple::empty();

        let (client_chan, server_chan) = zx::Channel::create().unwrap();
        root_directory.open(
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            0,
            &mut iter::empty(),
            ServerEnd::<NodeMarker>::new(server_chan.into()),
        );

        let hub = Arc::new(Hub::new(root_component_url.clone(), root_directory).unwrap());
        hooks.push(hub);
        let model = model::Model::new(model::ModelParams {
            ambient: Box::new(mocks::MockAmbientEnvironment::new()),
            root_component_url: root_component_url.clone(),
            root_resolver_registry: resolver,
            root_default_runner: Box::new(runner),
            hooks,
        });

        let res = await!(model.look_up_and_bind_instance(model::AbsoluteMoniker::root()));
        let expected_res: Result<(), model::ModelError> = Ok(());
        assert_eq!(format!("{:?}", res), format!("{:?}", expected_res));

        let hub_proxy = ClientEnd::<DirectoryMarker>::new(client_chan)
            .into_proxy()
            .expect("failed to create directory proxy");
        assert_eq!("blah", await!(read_file(&hub_proxy, "self/exec/runtime/bleep")));
    }
}
