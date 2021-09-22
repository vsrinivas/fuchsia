// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        component::{ComponentInstance, InstanceState},
        error::ModelError,
        events::{
            filter::EventFilter,
            synthesizer::{EventSynthesisProvider, ExtendedComponent},
        },
        hooks::{
            Event, EventError, EventErrorPayload, EventPayload, EventType, Hook, HooksRegistration,
        },
        model::Model,
        rights::Rights,
    },
    async_trait::async_trait,
    cm_rust::{
        CapabilityName, CapabilityPath, ComponentDecl, ExposeDecl, ExposeDirectoryDecl,
        ExposeSource, ExposeTarget,
    },
    fidl::endpoints::{Proxy, ServerEnd},
    fidl_fuchsia_io::{self as fio, DirectoryProxy, NodeEvent, NodeMarker, NodeProxy},
    fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::stream::StreamExt,
    io_util,
    log::*,
    moniker::{AbsoluteMoniker, AbsoluteMonikerBase, PartialAbsoluteMoniker},
    std::sync::{Arc, Mutex, Weak},
};

/// Awaits for `Started` events and for each capability exposed to framework, dispatches a
/// `DirectoryReady` event.
pub struct DirectoryReadyNotifier {
    model: Weak<Model>,
    /// Capabilities offered by component manager that we wish to provide through `DirectoryReady`
    /// events. For example, the diagnostics directory hosting inspect data.
    builtin_capabilities: Mutex<Vec<(String, NodeProxy)>>,
}

impl DirectoryReadyNotifier {
    pub fn new(model: Weak<Model>) -> Self {
        Self { model, builtin_capabilities: Mutex::new(Vec::new()) }
    }

    pub fn hooks(self: &Arc<Self>) -> Vec<HooksRegistration> {
        vec![HooksRegistration::new(
            "DirectoryReadyNotifier",
            vec![EventType::Started],
            Arc::downgrade(self) as Weak<dyn Hook>,
        )]
    }

    pub fn register_component_manager_capability(&self, name: impl Into<String>, node: NodeProxy) {
        if let Ok(mut guard) = self.builtin_capabilities.lock() {
            guard.push((name.into(), node));
        }
    }

    async fn on_component_started(
        self: &Arc<Self>,
        target_moniker: &AbsoluteMoniker,
        outgoing_dir: &DirectoryProxy,
        decl: ComponentDecl,
    ) -> Result<(), ModelError> {
        // Forward along errors into the new task so that dispatch can forward the
        // error as an event.
        let outgoing_node_result = clone_outgoing_root(&outgoing_dir, &target_moniker).await;

        // Don't block the handling on the event on the exposed capabilities being ready
        let this = self.clone();
        let moniker = target_moniker.to_partial();
        fasync::Task::spawn(async move {
            // If we can't find the component then we can't dispatch any DirectoryReady event,
            // error or otherwise. This isn't necessarily an error as the model or component might've been
            // destroyed in the intervening time, so we just exit early.
            let target = match this.model.upgrade() {
                Some(model) => {
                    if let Ok(component) = model.look_up(&moniker).await {
                        component
                    } else {
                        return;
                    }
                }
                None => return,
            };

            let matching_exposes = filter_matching_exposes(&decl, None);
            this.dispatch_capabilities_ready(
                outgoing_node_result,
                &decl,
                matching_exposes,
                &target,
            )
            .await;
        })
        .detach();
        Ok(())
    }

    /// Waits for the OnOpen event on the directory. This will hang until the component starts
    /// serving that directory. The directory should have been cloned/opened with DESCRIBE.
    async fn wait_for_on_open(
        &self,
        node: &NodeProxy,
        target_moniker: &AbsoluteMoniker,
        path: String,
    ) -> Result<(), ModelError> {
        let mut events = node.take_event_stream();
        match events.next().await {
            Some(Ok(NodeEvent::OnOpen_ { s: status, info: _ })) => zx::Status::ok(status)
                .map_err(|_| ModelError::open_directory_error(target_moniker.to_partial(), path)),
            Some(Ok(NodeEvent::OnConnectionInfo { .. })) => Ok(()),
            _ => Err(ModelError::open_directory_error(target_moniker.to_partial(), path)),
        }
    }

    /// Waits for the outgoing directory to be ready and then notifies hooks of all the capabilities
    /// inside it that were exposed to the framework by the component.
    async fn dispatch_capabilities_ready(
        &self,
        outgoing_node_result: Result<NodeProxy, ModelError>,
        decl: &ComponentDecl,
        matching_exposes: Vec<&ExposeDecl>,
        target: &Arc<ComponentInstance>,
    ) {
        let directory_ready_events =
            self.create_events(outgoing_node_result, decl, matching_exposes, target).await;
        for directory_ready_event in directory_ready_events {
            target.hooks.dispatch(&directory_ready_event).await.unwrap_or_else(|e| {
                warn!("Error notifying directory ready for {}: {:?}", target.abs_moniker, e)
            });
        }
    }

    async fn create_events(
        &self,
        outgoing_node_result: Result<NodeProxy, ModelError>,
        decl: &ComponentDecl,
        matching_exposes: Vec<&ExposeDecl>,
        target: &Arc<ComponentInstance>,
    ) -> Vec<Event> {
        // Forward along the result for opening the outgoing directory into the DirectoryReady
        // dispatch in order to propagate any potential errors as an event.
        let outgoing_dir_result = async move {
            let outgoing_node = outgoing_node_result?;
            self.wait_for_on_open(&outgoing_node, &target.abs_moniker, "/".to_string()).await?;
            io_util::node_to_directory(outgoing_node)
                .map_err(|_| ModelError::open_directory_error(target.abs_moniker.to_partial(), "/"))
        }
        .await;

        let mut events = Vec::new();
        for expose_decl in matching_exposes {
            let event = match expose_decl {
                ExposeDecl::Directory(ExposeDirectoryDecl { source_name, target_name, .. }) => {
                    let (source_path, rights) = {
                        if let Some(directory_decl) = decl.find_directory_source(source_name) {
                            (
                                directory_decl
                                    .source_path
                                    .as_ref()
                                    .expect("missing directory source path"),
                                directory_decl.rights,
                            )
                        } else {
                            panic!("Missing directory declaration for expose: {:?}", decl);
                        }
                    };
                    self.create_event(
                        &target,
                        outgoing_dir_result.as_ref(),
                        Rights::from(rights),
                        source_path,
                        target_name,
                    )
                    .await
                }
                _ => {
                    unreachable!("should have skipped above");
                }
            };
            events.push(event);
        }

        events
    }

    /// Creates an event with the directory at the given `target_path` inside the provided
    /// outgoing directory if the capability is available.
    async fn create_event(
        &self,
        target: &Arc<ComponentInstance>,
        outgoing_dir_result: Result<&DirectoryProxy, &ModelError>,
        rights: Rights,
        source_path: &CapabilityPath,
        target_name: &CapabilityName,
    ) -> Event {
        let target_name = target_name.to_string();

        let node_result = async move {
            // DirProxy.open fails on absolute paths.
            let source_path = source_path.to_string();
            let canonicalized_path = io_util::canonicalize_path(&source_path);
            let outgoing_dir = outgoing_dir_result.map_err(|e| e.clone())?;

            let (node, server_end) = fidl::endpoints::create_proxy::<NodeMarker>().unwrap();

            outgoing_dir
                .open(
                    rights.into_legacy() | fio::OPEN_FLAG_DESCRIBE,
                    fio::MODE_TYPE_DIRECTORY,
                    &canonicalized_path,
                    ServerEnd::new(server_end.into_channel()),
                )
                .map_err(|_| {
                    ModelError::open_directory_error(
                        target.abs_moniker.to_partial(),
                        source_path.clone(),
                    )
                })?;
            self.wait_for_on_open(&node, &target.abs_moniker, canonicalized_path.to_string())
                .await?;
            Ok(node)
        }
        .await;

        match node_result {
            Ok(node) => {
                Event::new(&target, Ok(EventPayload::DirectoryReady { name: target_name, node }))
            }
            Err(e) => Event::new(
                &target,
                Err(EventError::new(&e, EventErrorPayload::DirectoryReady { name: target_name })),
            ),
        }
    }

    async fn provide_builtin(&self, filter: &EventFilter) -> Vec<Event> {
        if let Ok(capabilities) = self.builtin_capabilities.lock() {
            (*capabilities)
                .iter()
                .filter_map(|(name, node)| {
                    if !filter.contains("name", vec![name.to_string()]) {
                        return None;
                    }
                    let (node_clone, server_end) = fidl::endpoints::create_proxy().unwrap();
                    let event = node
                        .clone(fio::CLONE_FLAG_SAME_RIGHTS, server_end)
                        .map(|_| {
                            Event::new_builtin(Ok(EventPayload::DirectoryReady {
                                name: name.clone(),
                                node: node_clone,
                            }))
                        })
                        .unwrap_or_else(|_| {
                            let err = ModelError::clone_node_error(
                                PartialAbsoluteMoniker::root(),
                                name.clone(),
                            );
                            Event::new_builtin(Err(EventError::new(
                                &err,
                                EventErrorPayload::DirectoryReady { name: name.clone() },
                            )))
                        });
                    Some(event)
                })
                .collect()
        } else {
            vec![]
        }
    }
}

fn filter_matching_exposes<'a>(
    decl: &'a ComponentDecl,
    filter: Option<&EventFilter>,
) -> Vec<&'a ExposeDecl> {
    decl.exposes
        .iter()
        .filter(|expose_decl| {
            match expose_decl {
                ExposeDecl::Directory(ExposeDirectoryDecl {
                    source, target, target_name, ..
                }) => {
                    if let Some(filter) = filter {
                        if !filter.contains("name", vec![target_name.to_string()]) {
                            return false;
                        }
                    }
                    if target != &ExposeTarget::Framework || source != &ExposeSource::Self_ {
                        return false;
                    }
                }
                _ => {
                    return false;
                }
            }
            true
        })
        .collect()
}

async fn clone_outgoing_root(
    outgoing_dir: &DirectoryProxy,
    target_moniker: &AbsoluteMoniker,
) -> Result<NodeProxy, ModelError> {
    let outgoing_dir = io_util::clone_directory(
        &outgoing_dir,
        fio::CLONE_FLAG_SAME_RIGHTS | fio::OPEN_FLAG_DESCRIBE,
    )
    .map_err(|_| ModelError::open_directory_error(target_moniker.to_partial(), "/"))?;
    let outgoing_dir_channel = outgoing_dir
        .into_channel()
        .map_err(|_| ModelError::open_directory_error(target_moniker.to_partial(), "/"))?;
    Ok(NodeProxy::from_channel(outgoing_dir_channel))
}

#[async_trait]
impl EventSynthesisProvider for DirectoryReadyNotifier {
    async fn provide(&self, component: ExtendedComponent, filter: &EventFilter) -> Vec<Event> {
        let component = match component {
            ExtendedComponent::ComponentManager => {
                return self.provide_builtin(filter).await;
            }
            ExtendedComponent::ComponentInstance(component) => component,
        };
        let decl = match *component.lock_state().await {
            InstanceState::Resolved(ref s) => s.decl().clone(),
            InstanceState::New | InstanceState::Discovered | InstanceState::Purged => {
                return vec![];
            }
        };
        let matching_exposes = filter_matching_exposes(&decl, Some(&filter));
        if matching_exposes.is_empty() {
            // Short-circuit if there are no matching exposes so we don't wait for the component's
            // outgoing directory if there are no DirectoryReady events to send.
            return vec![];
        }

        let maybe_outgoing_node_result = async {
            let execution = component.lock_execution().await;
            if execution.runtime.is_none() {
                return None;
            }
            let runtime = execution.runtime.as_ref().unwrap();
            let out_dir =
                match runtime.outgoing_dir.as_ref().ok_or(ModelError::open_directory_error(
                    component.abs_moniker.to_partial(),
                    "/".to_string(),
                )) {
                    Ok(out_dir) => out_dir,
                    Err(e) => return Some(Err(e)),
                };
            Some(clone_outgoing_root(&out_dir, &component.abs_moniker).await)
        }
        .await;
        let outgoing_node_result = match maybe_outgoing_node_result {
            None => return vec![],
            Some(result) => result,
        };

        self.create_events(outgoing_node_result, &decl, matching_exposes, &component).await
    }
}

#[async_trait]
impl Hook for DirectoryReadyNotifier {
    async fn on(self: Arc<Self>, event: &Event) -> Result<(), ModelError> {
        let target_moniker = event
            .target_moniker
            .unwrap_instance_moniker_or(ModelError::UnexpectedComponentManagerMoniker)?;
        match &event.result {
            Ok(EventPayload::Started { runtime, component_decl, .. }) => {
                if filter_matching_exposes(&component_decl, None).is_empty() {
                    // Short-circuit if there are no matching exposes so we don't spawn a task
                    // if there's nothing to do. In particular, don't wait for the component's
                    // outgoing directory if there are no DirectoryReady events to send.
                    return Ok(());
                }
                if let Some(outgoing_dir) = &runtime.outgoing_dir {
                    self.on_component_started(
                        &target_moniker,
                        outgoing_dir,
                        component_decl.clone(),
                    )
                    .await?;
                }
            }
            _ => {}
        }
        Ok(())
    }
}
