// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        error::ModelError,
        hooks::{Event, EventPayload, EventType, Hook, HooksRegistration},
        model::Model,
        moniker::AbsoluteMoniker,
        rights::{Rights, READ_RIGHTS, WRITE_RIGHTS},
    },
    async_trait::async_trait,
    cm_rust::{CapabilityPath, ExposeDecl, ExposeDirectoryDecl, ExposeProtocolDecl},
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{self as fio, DirectoryEvent, DirectoryProxy, NodeMarker},
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
        let outgoing_dir = io_util::clone_directory(
            &outgoing_dir,
            fio::CLONE_FLAG_SAME_RIGHTS | fio::OPEN_FLAG_DESCRIBE,
        )
        .map_err(|_| ModelError::open_directory_error(target_moniker.clone(), "/"))?;

        // Don't block the handling on the event on the exposed capabilities being ready
        let this = self.clone();
        let moniker = target_moniker.clone();
        fasync::spawn(async move {
            if let Err(e) = this.wait_for_on_open(&outgoing_dir, &moniker).await {
                error!("Failed waiting for OnOpen on outgoing dir for {}: {:?}", moniker, e);
                return;
            }
            this.dispatch_capabilities_ready(outgoing_dir, expose_decls, &moniker).await;
        });
        Ok(())
    }

    /// Waits for the OnOpen event on the directory. This will hang until the component starts
    /// serving that directory. The directory should have been cloned/opened with DESCRIBE.
    async fn wait_for_on_open(
        &self,
        outgoing_dir: &DirectoryProxy,
        target_moniker: &AbsoluteMoniker,
    ) -> Result<(), ModelError> {
        let mut events = outgoing_dir.take_event_stream();
        match events.next().await {
            Some(Ok(DirectoryEvent::OnOpen_ { s: status, info: _ })) => zx::Status::ok(status)
                .map_err(|_| ModelError::open_directory_error(target_moniker.clone(), "/")),
            _ => Err(ModelError::open_directory_error(target_moniker.clone(), "/")),
        }
    }

    /// Waits for the exposed directory to be ready and then notifies about all the capabilities
    /// inside it that were exposed to the framework by the component.
    async fn dispatch_capabilities_ready(
        &self,
        outgoing_dir: DirectoryProxy,
        expose_decls: Vec<ExposeDecl>,
        target_moniker: &AbsoluteMoniker,
    ) {
        for expose_decl in expose_decls {
            match expose_decl {
                ExposeDecl::Directory(ExposeDirectoryDecl { target_path, rights, .. }) => {
                    self.dispatch_capability_ready(
                        &outgoing_dir,
                        fio::MODE_TYPE_DIRECTORY,
                        Rights::from(rights.unwrap_or(*READ_RIGHTS)),
                        target_path,
                        target_moniker.clone(),
                    )
                    .await
                }
                ExposeDecl::Protocol(ExposeProtocolDecl { target_path, .. }) => {
                    self.dispatch_capability_ready(
                        &outgoing_dir,
                        fio::MODE_TYPE_SERVICE,
                        Rights::from(*WRITE_RIGHTS),
                        target_path,
                        target_moniker.clone(),
                    )
                    .await
                }
                _ => Ok(()),
            }
            .unwrap_or_else(|e| {
                error!("Error notifying capability ready for {}: {:?}", target_moniker, e)
            });
        }
    }

    /// Dispatches an event with the directory at the given `path` inside the `directory`
    async fn dispatch_capability_ready(
        &self,
        dir_proxy: &DirectoryProxy,
        mode: u32,
        rights: Rights,
        target_path: CapabilityPath,
        target_moniker: AbsoluteMoniker,
    ) -> Result<(), ModelError> {
        let realm = match self.model.upgrade() {
            Some(model) => model.look_up_realm(&target_moniker).await?,
            None => return Err(ModelError::ModelNotAvailable),
        };

        // DirProxy.open fails on absolute paths.
        let path = target_path.to_string();
        let canonicalized_path = io_util::canonicalize_path(&path);
        let (node, server_end) = fidl::endpoints::create_proxy::<NodeMarker>().unwrap();

        dir_proxy
            .open(
                rights.into_legacy(),
                mode,
                &canonicalized_path,
                ServerEnd::new(server_end.into_channel()),
            )
            .map_err(|_| {
                ModelError::open_directory_error(target_moniker.clone(), canonicalized_path)
            })?;

        let event = Event::new(target_moniker, EventPayload::CapabilityReady { path, node });
        realm.hooks.dispatch(&event).await?;

        Ok(())
    }
}

#[async_trait]
impl Hook for CapabilityReadyNotifier {
    async fn on(self: Arc<Self>, event: &Event) -> Result<(), ModelError> {
        match &event.payload {
            EventPayload::Started { runtime, component_decl, .. } => {
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
