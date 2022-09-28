// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability::{CapabilityProvider, CapabilitySource, PERMITTED_FLAGS},
        model::{
            component::{ComponentInstance, InstanceState},
            error::ModelError,
            hooks::{Event, EventPayload, EventType, Hook, HooksRegistration},
            model::Model,
        },
    },
    async_trait::async_trait,
    cm_rust::CapabilityName,
    cm_task_scope::TaskScope,
    cm_util::channel,
    fidl::{
        endpoints::{ClientEnd, ServerEnd},
        prelude::*,
    },
    fidl_fuchsia_io as fio, fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
    fuchsia_zircon as zx,
    fuchsia_zircon::sys::ZX_CHANNEL_MAX_MSG_BYTES,
    futures::lock::Mutex,
    futures::StreamExt,
    lazy_static::lazy_static,
    measure_tape_for_instance_info::Measurable,
    moniker::{AbsoluteMoniker, AbsoluteMonikerBase, RelativeMoniker, RelativeMonikerBase},
    std::{
        path::PathBuf,
        sync::{Arc, Weak},
    },
    tracing::warn,
};

lazy_static! {
    pub static ref REALM_EXPLORER_CAPABILITY_NAME: CapabilityName =
        fsys::RealmExplorerMarker::PROTOCOL_NAME.into();
}

// Number of bytes the header of a vector occupies in a fidl message.
// TODO(https://fxbug.dev/98653): This should be a constant in a FIDL library.
const FIDL_VECTOR_HEADER_BYTES: usize = 16;

// Number of bytes the header of a fidl message occupies.
// TODO(https://fxbug.dev/98653): This should be a constant in a FIDL library.
const FIDL_HEADER_BYTES: usize = 16;

// Serves the fuchsia.sys2.RealmExplorer protocols.
pub struct RealmExplorer {
    model: Arc<Model>,
}

impl RealmExplorer {
    pub fn new(model: Arc<Model>) -> Self {
        Self { model }
    }

    pub fn hooks(self: &Arc<Self>) -> Vec<HooksRegistration> {
        vec![HooksRegistration::new(
            "RealmExplorer",
            vec![EventType::CapabilityRouted],
            Arc::downgrade(self) as Weak<dyn Hook>,
        )]
    }

    /// Given a `CapabilitySource`, determine if it is a framework-provided
    /// RealmExplorer capability. If so, serve the capability.
    async fn on_capability_routed_async(
        self: Arc<Self>,
        source: CapabilitySource,
        capability_provider: Arc<Mutex<Option<Box<dyn CapabilityProvider>>>>,
    ) -> Result<(), ModelError> {
        // If this is a scoped framework directory capability, then check the source path
        if let CapabilitySource::Framework { capability, component } = source {
            if capability.matches_protocol(&REALM_EXPLORER_CAPABILITY_NAME) {
                // Set the capability provider, if not already set.
                let mut capability_provider = capability_provider.lock().await;
                if capability_provider.is_none() {
                    *capability_provider = Some(Box::new(RealmExplorerCapabilityProvider::new(
                        self,
                        component.abs_moniker.clone(),
                    )));
                }
            }
        }
        Ok(())
    }

    /// Create the detailed instance info matching the given moniker string in this scope
    /// and return all live children of the instance.
    async fn get_instance_info_and_children(
        self: &Arc<Self>,
        scope_moniker: &AbsoluteMoniker,
        instance: &Arc<ComponentInstance>,
    ) -> (fsys::InstanceInfo, Vec<Arc<ComponentInstance>>) {
        let relative_moniker = extract_relative_moniker(scope_moniker, &instance.abs_moniker);
        let instance_id =
            self.model.component_id_index().look_up_moniker(&instance.abs_moniker).cloned();

        let (state, children) = {
            let state = instance.lock_state().await;
            let execution = instance.lock_execution().await;
            match &*state {
                InstanceState::Resolved(r) => {
                    let children = r.children().map(|(_, c)| c.clone()).collect();
                    if execution.runtime.is_some() {
                        (fsys::InstanceState::Started, children)
                    } else {
                        (fsys::InstanceState::Resolved, children)
                    }
                }
                _ => (fsys::InstanceState::Unresolved, vec![]),
            }
        };

        (
            fsys::InstanceInfo {
                moniker: relative_moniker.to_string(),
                url: instance.component_url.clone(),
                instance_id,
                state,
            },
            children,
        )
    }

    /// Take a snapshot of all instances in the given scope and create the instance info
    /// FIDL object for each.
    async fn snapshot_instance_infos(
        self: &Arc<Self>,
        scope_moniker: &AbsoluteMoniker,
    ) -> Result<Vec<fsys::InstanceInfo>, fsys::RealmExplorerError> {
        let mut instance_infos = vec![];

        // Only take instances contained within the scope realm
        // TODO(https://fxbug.dev/108532): Close the connection if the scope root cannot be found.
        let scope_root = self
            .model
            .find(scope_moniker)
            .await
            .ok_or(fsys::RealmExplorerError::InstanceNotFound)?;

        let mut queue = vec![scope_root];

        while !queue.is_empty() {
            let cur = queue.pop().unwrap();

            let (instance_info, mut children) =
                self.get_instance_info_and_children(scope_moniker, &cur).await;
            instance_infos.push(instance_info);
            queue.append(&mut children);
        }

        Ok(instance_infos)
    }

    /// Serve the fuchsia.sys2.RealmExplorer protocol for a given scope on a given stream
    async fn serve(
        self: Arc<Self>,
        scope_moniker: AbsoluteMoniker,
        mut stream: fsys::RealmExplorerRequestStream,
    ) {
        loop {
            let fsys::RealmExplorerRequest::GetAllInstanceInfos { responder } =
                match stream.next().await {
                    Some(Ok(request)) => request,
                    Some(Err(error)) => {
                        warn!(?error, "Could not get next RealmExplorer request");
                        break;
                    }
                    None => break,
                };
            let mut result = match self.snapshot_instance_infos(&scope_moniker).await {
                Ok(fidl_instances) => {
                    let client_end = serve_instance_info_iterator(fidl_instances);
                    Ok(client_end)
                }
                Err(e) => Err(e),
            };
            if let Err(error) = responder.send(&mut result) {
                warn!(?error, "Could not respond to GetAllInstanceInfos request");
                break;
            }
        }
    }
}

fn serve_instance_info_iterator(
    mut instance_infos: Vec<fsys::InstanceInfo>,
) -> ClientEnd<fsys::InstanceInfoIteratorMarker> {
    let (client_end, server_end) =
        fidl::endpoints::create_endpoints::<fsys::InstanceInfoIteratorMarker>().unwrap();
    fasync::Task::spawn(async move {
        let mut stream: fsys::InstanceInfoIteratorRequestStream = server_end.into_stream().unwrap();
        while let Some(Ok(fsys::InstanceInfoIteratorRequest::Next { responder })) =
            stream.next().await
        {
            let mut bytes_used: usize = FIDL_HEADER_BYTES + FIDL_VECTOR_HEADER_BYTES;
            let mut instance_count = 0;

            // Determine how many info objects can be sent in a single FIDL message.
            // TODO(https://fxbug.dev/98653): This logic should be handled by FIDL.
            for info in &instance_infos {
                bytes_used += info.measure().num_bytes;
                if bytes_used > ZX_CHANNEL_MAX_MSG_BYTES as usize {
                    break;
                }
                instance_count += 1;
            }

            let mut batch: Vec<fsys::InstanceInfo> =
                instance_infos.drain(0..instance_count).collect();

            let result = responder.send(&mut batch.iter_mut());
            if let Err(error) = result {
                warn!(?error, "RealmExplorer encountered error sending instance info batch");
                break;
            }
        }
    })
    .detach();
    client_end
}

#[async_trait]
impl Hook for RealmExplorer {
    async fn on(self: Arc<Self>, event: &Event) -> Result<(), ModelError> {
        match &event.result {
            Ok(EventPayload::CapabilityRouted { source, capability_provider }) => {
                self.on_capability_routed_async(source.clone(), capability_provider.clone())
                    .await?;
            }
            _ => {}
        }
        Ok(())
    }
}

pub struct RealmExplorerCapabilityProvider {
    explorer: Arc<RealmExplorer>,
    scope_moniker: AbsoluteMoniker,
}

impl RealmExplorerCapabilityProvider {
    pub fn new(explorer: Arc<RealmExplorer>, scope_moniker: AbsoluteMoniker) -> Self {
        Self { explorer, scope_moniker }
    }
}

#[async_trait]
impl CapabilityProvider for RealmExplorerCapabilityProvider {
    async fn open(
        self: Box<Self>,
        task_scope: TaskScope,
        flags: fio::OpenFlags,
        _open_mode: u32,
        relative_path: PathBuf,
        server_end: &mut zx::Channel,
    ) -> Result<(), ModelError> {
        let forbidden = flags - PERMITTED_FLAGS;
        if !forbidden.is_empty() {
            warn!(?forbidden, "RealmExplorer capability");
            return Ok(());
        }

        if relative_path.components().count() != 0 {
            warn!(
                path=%relative_path.display(),
                "RealmExplorer capability got open request with non-empty",
            );
            return Ok(());
        }

        let server_end = channel::take_channel(server_end);

        let server_end = ServerEnd::<fsys::RealmExplorerMarker>::new(server_end);
        let stream: fsys::RealmExplorerRequestStream =
            server_end.into_stream().map_err(ModelError::stream_creation_error)?;
        task_scope
            .add_task(async move {
                self.explorer.serve(self.scope_moniker, stream).await;
            })
            .await;

        Ok(())
    }
}

/// Takes a parent and child absolute moniker, strips out the parent portion from the child
/// and creates a relative moniker.
fn extract_relative_moniker(parent: &AbsoluteMoniker, child: &AbsoluteMoniker) -> RelativeMoniker {
    assert!(parent.contains_in_realm(child));
    let parent_len = parent.path().len();
    let mut children = child.path().clone();
    children.drain(0..parent_len);
    RelativeMoniker::new(vec![], children)
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::model::component::StartReason,
        crate::model::testing::test_helpers::{TestEnvironmentBuilder, TestModelResult},
        cm_rust::*,
        cm_rust_testing::ComponentDeclBuilder,
        fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_component as fcomponent, fidl_fuchsia_component_decl as fdecl,
        fuchsia_async as fasync,
        moniker::*,
        routing_test_helpers::component_id_index::make_index_file,
    };

    async fn get_instance_info(
        explorer: &fsys::RealmExplorerProxy,
        num_expected_instances: usize,
        expected_moniker: &str,
    ) -> fsys::InstanceInfo {
        let iterator = explorer.get_all_instance_infos().await.unwrap().unwrap();
        let iterator = iterator.into_proxy().unwrap();

        let mut instances = vec![];

        loop {
            let mut iteration = iterator.next().await.unwrap();
            if iteration.is_empty() {
                break;
            }
            instances.append(&mut iteration);
        }

        assert_eq!(instances.len(), num_expected_instances);

        for instance in instances.drain(..) {
            if instance.moniker == expected_moniker {
                return instance;
            }
        }

        panic!("Could not find instance matching moniker: {}", expected_moniker);
    }

    #[fuchsia::test]
    async fn basic_test() {
        // Create index.
        let iid = format!("1234{}", "5".repeat(60));
        let index_file = make_index_file(component_id_index::Index {
            instances: vec![component_id_index::InstanceIdEntry {
                instance_id: Some(iid.clone()),
                appmgr_moniker: None,
                moniker: Some(AbsoluteMoniker::parse_str("/").unwrap()),
            }],
            ..component_id_index::Index::default()
        })
        .unwrap();

        let components = vec![("root", ComponentDeclBuilder::new().build())];

        let TestModelResult { model, builtin_environment, .. } = TestEnvironmentBuilder::new()
            .set_components(components)
            .set_component_id_index_path(index_file.path().to_str().map(str::to_string))
            .build()
            .await;

        let realm_explorer = {
            let env = builtin_environment.lock().await;
            env.realm_explorer.clone().unwrap()
        };

        let (explorer, explorer_request_stream) =
            create_proxy_and_stream::<fsys::RealmExplorerMarker>().unwrap();

        let _explorer_task = fasync::Task::local(async move {
            realm_explorer.serve(AbsoluteMoniker::root(), explorer_request_stream).await
        });

        model.start().await;

        let info = get_instance_info(&explorer, 1, ".").await;
        assert_eq!(info.url, "test:///root");
        assert_eq!(info.state, fsys::InstanceState::Started);
        assert_eq!(info.instance_id.clone().unwrap(), iid);
    }

    #[fuchsia::test]
    async fn observe_dynamic_lifecycle() {
        let components = vec![
            (
                "root",
                ComponentDeclBuilder::new()
                    .add_collection(CollectionDecl {
                        name: "my_coll".to_string(),
                        durability: fdecl::Durability::Transient,
                        environment: None,
                        allowed_offers: cm_types::AllowedOffers::StaticOnly,
                        allow_long_names: false,
                        persistent_storage: None,
                    })
                    .build(),
            ),
            ("a", ComponentDeclBuilder::new().build()),
        ];

        let TestModelResult { model, builtin_environment, .. } =
            TestEnvironmentBuilder::new().set_components(components).build().await;

        let realm_explorer = {
            let env = builtin_environment.lock().await;
            env.realm_explorer.clone().unwrap()
        };

        let (explorer, explorer_request_stream) =
            create_proxy_and_stream::<fsys::RealmExplorerMarker>().unwrap();

        let _explorer_task = fasync::Task::local(async move {
            realm_explorer.serve(AbsoluteMoniker::root(), explorer_request_stream).await
        });

        model.start().await;

        get_instance_info(&explorer, 1, ".").await;

        let component_root = model.look_up(&AbsoluteMoniker::root()).await.unwrap();
        component_root
            .add_dynamic_child(
                "my_coll".to_string(),
                &ChildDecl {
                    name: "a".to_string(),
                    url: "test:///a".to_string(),
                    startup: fdecl::StartupMode::Lazy,
                    on_terminate: None,
                    environment: None,
                },
                fcomponent::CreateChildArgs::EMPTY,
            )
            .await
            .unwrap();

        // `a` should be unresolved
        let info = get_instance_info(&explorer, 2, "./my_coll:a").await;
        assert_eq!(info.url, "test:///a");
        assert_eq!(info.state, fsys::InstanceState::Unresolved);
        assert!(info.instance_id.is_none());

        let moniker_a = AbsoluteMoniker::parse_str("/my_coll:a").unwrap();
        let component_a = model.look_up(&moniker_a).await.unwrap();

        // `a` should be resolved
        let info = get_instance_info(&explorer, 2, "./my_coll:a").await;
        assert_eq!(info.url, "test:///a");
        assert_eq!(info.state, fsys::InstanceState::Resolved);
        assert!(info.instance_id.is_none());

        let result = component_a.start(&StartReason::Debug).await.unwrap();
        assert_eq!(result, fsys::StartResult::Started);

        // `a` should be started
        let info = get_instance_info(&explorer, 2, "./my_coll:a").await;
        assert_eq!(info.url, "test:///a");
        assert_eq!(info.state, fsys::InstanceState::Started);
        assert!(info.instance_id.is_none());

        component_a.stop_instance(false, false).await.unwrap();

        // `a` should be stopped
        let info = get_instance_info(&explorer, 2, "./my_coll:a").await;
        assert_eq!(info.url, "test:///a");
        assert_eq!(info.state, fsys::InstanceState::Resolved);
        assert!(info.instance_id.is_none());

        let child_moniker = ChildMoniker::parse("my_coll:a").unwrap();
        component_root.remove_dynamic_child(&child_moniker).await.unwrap();

        // `a` should be destroyed after purge
        get_instance_info(&explorer, 1, ".").await;
    }

    #[fuchsia::test]
    async fn scoped_to_child() {
        let components = vec![
            ("root", ComponentDeclBuilder::new().add_lazy_child("a").build()),
            ("a", ComponentDeclBuilder::new().build()),
        ];

        let TestModelResult { model, builtin_environment, .. } =
            TestEnvironmentBuilder::new().set_components(components).build().await;

        let realm_explorer = {
            let env = builtin_environment.lock().await;
            env.realm_explorer.clone().unwrap()
        };

        let (explorer, explorer_request_stream) =
            create_proxy_and_stream::<fsys::RealmExplorerMarker>().unwrap();

        let moniker_a = AbsoluteMoniker::parse_str("/a").unwrap();

        let _explorer_task = fasync::Task::local(async move {
            realm_explorer.serve(moniker_a, explorer_request_stream).await
        });

        model.start().await;

        // `a` should be unresolved
        let info = get_instance_info(&explorer, 1, ".").await;
        assert_eq!(info.url, "test:///a");
        assert_eq!(info.state, fsys::InstanceState::Unresolved);
        assert!(info.instance_id.is_none());

        let moniker_a = AbsoluteMoniker::parse_str("/a").unwrap();
        let component_a = model.look_up(&moniker_a).await.unwrap();

        // `a` should be resolved
        let info = get_instance_info(&explorer, 1, ".").await;
        assert_eq!(info.url, "test:///a");
        assert_eq!(info.state, fsys::InstanceState::Resolved);
        assert!(info.instance_id.is_none());

        let result = component_a.start(&StartReason::Debug).await.unwrap();
        assert_eq!(result, fsys::StartResult::Started);

        // `a` should be started
        let info = get_instance_info(&explorer, 1, ".").await;
        assert_eq!(info.url, "test:///a");
        assert_eq!(info.state, fsys::InstanceState::Started);
        assert!(info.instance_id.is_none());

        component_a.stop_instance(false, false).await.unwrap();

        // `a` should be stopped
        let info = get_instance_info(&explorer, 1, ".").await;
        assert_eq!(info.url, "test:///a");
        assert_eq!(info.state, fsys::InstanceState::Resolved);
        assert!(info.instance_id.is_none());
    }
}
