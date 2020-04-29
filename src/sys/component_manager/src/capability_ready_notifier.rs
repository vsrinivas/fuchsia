// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        error::ModelError,
        hooks::{
            Event, EventError, EventErrorPayload, EventPayload, EventType, Hook, HooksRegistration,
        },
        model::Model,
        moniker::AbsoluteMoniker,
        realm::Realm,
        rights::{Rights, READ_RIGHTS, WRITE_RIGHTS},
    },
    async_trait::async_trait,
    cm_rust::{CapabilityPath, ExposeDecl, ExposeDirectoryDecl, ExposeProtocolDecl},
    fidl::endpoints::{Proxy, ServerEnd},
    fidl_fuchsia_io::{self as fio, DirectoryProxy, NodeEvent, NodeMarker, NodeProxy},
    fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::stream::StreamExt,
    io_util,
    log::*,
    std::sync::{Arc, Weak},
};

/// Awaits for `Started` events and for each capability exposed to framework, dispatches a
/// `CapabilityReady` event.
pub struct CapabilityReadyNotifier {
    model: Weak<Model>,
}

impl CapabilityReadyNotifier {
    pub fn new(model: Weak<Model>) -> Self {
        Self { model }
    }

    pub fn hooks(self: &Arc<Self>) -> Vec<HooksRegistration> {
        vec![HooksRegistration::new(
            "CapabilityReadyNotifier",
            vec![EventType::Started],
            Arc::downgrade(self) as Weak<dyn Hook>,
        )]
    }

    async fn on_component_started(
        self: &Arc<Self>,
        target_moniker: &AbsoluteMoniker,
        outgoing_dir: &DirectoryProxy,
        expose_decls: Vec<ExposeDecl>,
    ) -> Result<(), ModelError> {
        // Forward along errors into the new task so that dispatch can forward the
        // error as an event.
        let outgoing_node_result = async move {
            let outgoing_dir = io_util::clone_directory(
                &outgoing_dir,
                fio::CLONE_FLAG_SAME_RIGHTS | fio::OPEN_FLAG_DESCRIBE,
            )
            .map_err(|_| ModelError::open_directory_error(target_moniker.clone(), "/"))?;
            let outgoing_dir_channel = outgoing_dir
                .into_channel()
                .map_err(|_| ModelError::open_directory_error(target_moniker.clone(), "/"))?;
            Ok(NodeProxy::from_channel(outgoing_dir_channel))
        }
        .await;

        // Don't block the handling on the event on the exposed capabilities being ready
        let this = self.clone();
        let moniker = target_moniker.clone();
        fasync::spawn(async move {
            // If we can't find the realm then we can't dispatch any CapabilityReady event,
            // error or otherwise. This isn't necessarily an error as the model or realm might've been
            // destroyed in the intervening time, so we just exit early.
            let target_realm = match this.model.upgrade() {
                Some(model) => {
                    if let Ok(realm) = model.look_up_realm(&moniker).await {
                        realm
                    } else {
                        return;
                    }
                }
                None => return,
            };

            if let Err(e) = this
                .dispatch_capabilities_ready(outgoing_node_result, expose_decls, &target_realm)
                .await
            {
                error!("Failed CapabilityReady dispatch for {}: {:?}", moniker, e);
                return;
            }
        });
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
                .map_err(|_| ModelError::open_directory_error(target_moniker.clone(), path)),
            _ => Err(ModelError::open_directory_error(target_moniker.clone(), path)),
        }
    }

    /// Waits for the outgoing directory to be ready and then notifies hooks of all the capabilities
    /// inside it that were exposed to the framework by the component.
    async fn dispatch_capabilities_ready(
        &self,
        outgoing_node_result: Result<NodeProxy, ModelError>,
        expose_decls: Vec<ExposeDecl>,
        target_realm: &Arc<Realm>,
    ) -> Result<(), ModelError> {
        // Forward along the result for opening the outgoing directory into the CapabilityReady
        // dispatch in order to propagate any potential errors as an event.
        let outgoing_dir_result = async move {
            let outgoing_node = outgoing_node_result?;
            self.wait_for_on_open(&outgoing_node, &target_realm.abs_moniker, "/".to_string())
                .await?;
            io_util::node_to_directory(outgoing_node).map_err(|_| {
                ModelError::open_directory_error(target_realm.abs_moniker.clone(), "/")
            })
        }
        .await;

        for expose_decl in expose_decls {
            match expose_decl {
                ExposeDecl::Directory(ExposeDirectoryDecl { target_path, rights, .. }) => {
                    self.dispatch_capability_ready(
                        &target_realm,
                        outgoing_dir_result.as_ref(),
                        fio::MODE_TYPE_DIRECTORY,
                        Rights::from(rights.unwrap_or(*READ_RIGHTS)),
                        target_path,
                    )
                    .await
                }
                ExposeDecl::Protocol(ExposeProtocolDecl { target_path, .. }) => {
                    self.dispatch_capability_ready(
                        &target_realm,
                        outgoing_dir_result.as_ref(),
                        fio::MODE_TYPE_SERVICE,
                        Rights::from(*WRITE_RIGHTS),
                        target_path,
                    )
                    .await
                }
                _ => Ok(()),
            }
            .unwrap_or_else(|e| {
                error!("Error notifying capability ready for {}: {:?}", target_realm.abs_moniker, e)
            });
        }

        Ok(())
    }

    /// Dispatches an event with the directory at the given `target_path` inside the provided
    /// outgoing directory if the capability is available.
    async fn dispatch_capability_ready(
        &self,
        target_realm: &Arc<Realm>,
        outgoing_dir_result: Result<&DirectoryProxy, &ModelError>,
        mode: u32,
        rights: Rights,
        target_path: CapabilityPath,
    ) -> Result<(), ModelError> {
        // DirProxy.open fails on absolute paths.
        let path = target_path.to_string();
        let canonicalized_path = io_util::canonicalize_path(&path);

        let node_result = async move {
            let outgoing_dir = outgoing_dir_result.map_err(|e| e.clone())?;

            let (node, server_end) = fidl::endpoints::create_proxy::<NodeMarker>().unwrap();

            outgoing_dir
                .open(
                    rights.into_legacy() | fio::OPEN_FLAG_DESCRIBE,
                    mode,
                    &canonicalized_path,
                    ServerEnd::new(server_end.into_channel()),
                )
                .map_err(|_| {
                    ModelError::open_directory_error(
                        target_realm.abs_moniker.clone(),
                        target_path.to_string(),
                    )
                })?;
            self.wait_for_on_open(&node, &target_realm.abs_moniker, canonicalized_path.to_string())
                .await?;
            Ok(node)
        }
        .await;

        match node_result {
            Ok(node) => {
                let event = Event::new(
                    target_realm.abs_moniker.clone(),
                    Ok(EventPayload::CapabilityReady { path, node }),
                );
                target_realm.hooks.dispatch(&event).await
            }
            Err(e) => {
                let event = Event::new(
                    target_realm.abs_moniker.clone(),
                    Err(EventError::new(&e, EventErrorPayload::CapabilityReady { path })),
                );
                target_realm.hooks.dispatch(&event).await?;
                return Err(e);
            }
        }
    }
}

#[async_trait]
impl Hook for CapabilityReadyNotifier {
    async fn on(self: Arc<Self>, event: &Event) -> Result<(), ModelError> {
        match &event.result {
            Ok(EventPayload::Started { runtime, component_decl, .. }) => {
                let expose_decls = component_decl.get_self_capabilities_exposed_to_framework();
                if expose_decls.is_empty() {
                    return Ok(());
                }
                if let Some(outgoing_dir) = &runtime.outgoing_dir {
                    self.on_component_started(&event.target_moniker, outgoing_dir, expose_decls)
                        .await?;
                }
            }
            _ => {}
        }
        Ok(())
    }
}
