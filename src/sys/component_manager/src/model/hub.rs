// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{self, error::ModelError},
    failure::Fail,
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
    pub runtime: Runtime,
    pub directory: directory::controlled::Controller<'static>,
}

/// The runner state for a component instance that has started running.
struct Runtime {
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

    pub async fn on_bind_instance_async<'a>(
        &'a self,
        realm: Arc<model::Realm>,
        instance_state: &'a model::InstanceState,
    ) -> Result<(), ModelError> {
        let component_url = realm.instance.component_url.clone();
        let abs_moniker = realm.abs_moniker.clone();
        let mut instances_map = await!(self.instances.lock());

        if let Some(controlled) =
            Hub::add_instance_if_necessary(&abs_moniker, component_url, &mut instances_map)?
        {
            if let (Some(name), Some(parent_moniker)) = (abs_moniker.name(), abs_moniker.parent()) {
                await!(instances_map[&parent_moniker]
                    .children_directory
                    .add_entry(name.clone(), controlled))
                .map_err(|_| {
                    HubError::add_directory_entry_error(abs_moniker.clone(), &name.clone())
                })?;
            }
        }

        let instance = instances_map
            .get_mut(&abs_moniker)
            .expect(&format!("Unable to find instance {} in map.", abs_moniker));

        // If we haven't already created an execution directory, create one now.
        if instance.execution.is_none() {
            let (controller, mut controlled) =
                directory::controlled::controlled(directory::simple::empty());

            // Add the runtime subdirectory.
            let (runtime_controller, runtime_controlled) =
                directory::controlled::controlled(directory::simple::empty());
            controlled
                .add_entry("runtime", runtime_controlled)
                .map_err(|_| HubError::add_directory_entry_error(abs_moniker.clone(), "runtime"))?;

            if let Some(execution) = instance_state.execution.as_ref() {
                let exec = Execution {
                    resolved_url: execution.resolved_url.clone(),
                    directory: controller,
                    runtime: Runtime { directory: runtime_controller },
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

                await!(instance.directory.add_entry("exec", controlled)).map_err(|_| {
                    HubError::add_directory_entry_error(abs_moniker.clone(), "exec")
                })?;
            }
        }
        Ok(())
    }

    pub async fn on_resolve_realm_async(&self, realm: Arc<model::Realm>) -> Result<(), ModelError> {
        let abs_moniker = &realm.abs_moniker;
        let component_url = realm.instance.component_url.clone();
        let mut instances_map = await!(self.instances.lock());

        if let Some(controlled) =
            Hub::add_instance_if_necessary(&abs_moniker, component_url, &mut instances_map)?
        {
            if let (Some(name), Some(parent_moniker)) = (abs_moniker.name(), abs_moniker.parent()) {
                await!(instances_map[&parent_moniker]
                    .children_directory
                    .add_entry(name.clone(), controlled))
                .map_err(|_| {
                    HubError::add_directory_entry_error(abs_moniker.clone(), &name.clone())
                })?;
            }
        }
        Ok(())
    }
}

impl model::Hook for Hub {
    fn on_bind_instance<'a>(
        &'a self,
        realm: Arc<model::Realm>,
        instance_state: &'a model::InstanceState,
    ) -> BoxFuture<Result<(), ModelError>> {
        Box::pin(self.on_bind_instance_async(realm, instance_state))
    }

    fn on_resolve_realm<'a>(
        &'a self,
        realm: Arc<model::Realm>,
    ) -> BoxFuture<'a, Result<(), ModelError>> {
        Box::pin(self.on_resolve_realm_async(realm))
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::model::{
            self, hub::Hub, tests::mocks, tests::routing_test_helpers::default_component_decl,
        },
        cm_rust::{self, ChildDecl, ComponentDecl},
        fidl::endpoints::{ClientEnd, ServerEnd},
        fidl_fuchsia_io::{DirectoryMarker, NodeMarker, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE},
        fidl_fuchsia_sys2 as fsys,
        fuchsia_vfs_pseudo_fs::directory::entry::DirectoryEntry,
        fuchsia_zircon as zx,
        std::{iter, path::PathBuf},
    };

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

        let hub =
            Arc::new(Hub::new("/my_component_url_string".to_string(), root_directory).unwrap());
        hooks.push(hub);
        let model = model::Model::new(model::ModelParams {
            ambient: Box::new(mocks::MockAmbientEnvironment::new()),
            root_component_url: "test:///root".to_string(),
            root_resolver_registry: resolver,
            root_default_runner: Box::new(runner),
            hooks,
        });

        // Bind to the top component, and check that it and the eager components were started.
        {
            let res = await!(model.look_up_and_bind_instance(model::AbsoluteMoniker::root()));
            let expected_res: Result<(), model::ModelError> = Ok(());
            assert_eq!(format!("{:?}", res), format!("{:?}", expected_res));
        }

        let hub_proxy = ClientEnd::<DirectoryMarker>::new(client_chan)
            .into_proxy()
            .expect("failed to create directory proxy");
        let self_url_file_proxy = io_util::open_file(&hub_proxy, &PathBuf::from("self/url"))
            .expect("failed to open file");
        let res = await!(io_util::read_file(&self_url_file_proxy));
        assert_eq!("/my_component_url_string", res.expect("Unable to read URL."));

        let a_url_file_proxy =
            io_util::open_file(&hub_proxy, &PathBuf::from("self/children/a/url"))
                .expect("failed to open file");
        let res_a = await!(io_util::read_file(&a_url_file_proxy));
        assert_eq!("test:///a", res_a.expect("Unable to read URL"));
    }
}
