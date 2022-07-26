// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::{
        capability::CapabilityProvider,
        model::{
            component::{ComponentInstance, StartReason, WeakComponentInstance},
            error::ModelError,
            hooks::{Event, EventPayload},
            routing::error::OpenResourceError,
        },
    },
    ::routing::path::PathBufExt,
    async_trait::async_trait,
    cm_rust::{self, CapabilityName, CapabilityPath},
    cm_task_scope::TaskScope,
    cm_util::channel,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io as fio, fuchsia_zircon as zx,
    futures::lock::Mutex,
    std::{path::PathBuf, sync::Arc},
    vfs::{directory::entry::DirectoryEntry, execution_scope::ExecutionScope},
};

/// The default provider for a ComponentCapability.
/// This provider will start the source component instance and open the capability `name` at
/// `path` under the source component's outgoing namespace.
pub struct DefaultComponentCapabilityProvider {
    pub target: WeakComponentInstance,
    pub source: WeakComponentInstance,
    pub name: CapabilityName,
    pub path: CapabilityPath,
}

#[async_trait]
impl CapabilityProvider for DefaultComponentCapabilityProvider {
    async fn open(
        self: Box<Self>,
        _task_scope: TaskScope,
        flags: fio::OpenFlags,
        open_mode: u32,
        relative_path: PathBuf,
        server_end: &mut zx::Channel,
    ) -> Result<(), ModelError> {
        let capability = Arc::new(Mutex::new(Some(channel::take_channel(server_end))));
        let res = async {
            // Start the source component, if necessary
            let source = self.source.upgrade()?;
            source
                .start(&StartReason::AccessCapability {
                    target: self.target.abs_moniker.clone(),
                    name: self.name.clone(),
                })
                .await?;

            let event = Event::new(
                &self.target.upgrade()?,
                Ok(EventPayload::CapabilityRequested {
                    source_moniker: source.abs_moniker.clone(),
                    name: self.name.to_string(),
                    capability: capability.clone(),
                }),
            );
            source.hooks.dispatch(&event).await?;
            Result::<Arc<ComponentInstance>, ModelError>::Ok(source)
        }
        .await;

        // If the capability transported through the event above wasn't transferred
        // out, then we can open the capability through the component's outgoing directory.
        // If some hook consumes the capability, then we don't bother looking in the outgoing
        // directory.
        let capability = capability.lock().await.take();
        if let Some(mut server_end_in) = capability {
            // Pass back the channel so the caller can set the epitaph, if necessary.
            *server_end = channel::take_channel(&mut server_end_in);
            let path = self.path.to_path_buf().attach(relative_path);
            res?.open_outgoing(flags, open_mode, path, server_end).await?;
        } else {
            res?;
        }
        Ok(())
    }
}

/// The default provider for a Namespace Capability.
pub struct NamespaceCapabilityProvider {
    pub path: CapabilityPath,
}

#[async_trait]
impl CapabilityProvider for NamespaceCapabilityProvider {
    async fn open(
        self: Box<Self>,
        _task_scope: TaskScope,
        flags: fio::OpenFlags,
        _open_mode: u32,
        relative_path: PathBuf,
        server_end: &mut zx::Channel,
    ) -> Result<(), ModelError> {
        let namespace_path = self.path.to_path_buf().attach(relative_path);
        let namespace_path = namespace_path
            .to_str()
            .ok_or_else(|| ModelError::path_is_not_utf8(namespace_path.clone()))?;
        let server_end = channel::take_channel(server_end);
        fuchsia_fs::node::open_channel_in_namespace(
            namespace_path,
            flags,
            ServerEnd::new(server_end),
        )
        .map_err(|e| {
            OpenResourceError::open_component_manager_namespace_failed(namespace_path, e).into()
        })
    }
}

/// A `CapabilityProvider` that serves a pseudo directory entry.
#[derive(Clone)]
pub struct DirectoryEntryCapabilityProvider {
    /// Execution scope for requests to `entry`.
    pub execution_scope: ExecutionScope,

    /// The pseudo directory entry that backs this capability.
    pub entry: Arc<dyn DirectoryEntry>,
}

#[async_trait]
impl CapabilityProvider for DirectoryEntryCapabilityProvider {
    async fn open(
        self: Box<Self>,
        _task_scope: TaskScope,
        flags: fio::OpenFlags,
        open_mode: u32,
        relative_path: PathBuf,
        server_end: &mut zx::Channel,
    ) -> Result<(), ModelError> {
        let relative_path_utf8 = relative_path
            .to_str()
            .ok_or_else(|| ModelError::path_is_not_utf8(relative_path.clone()))?;
        let relative_path = if relative_path_utf8.is_empty() {
            vfs::path::Path::dot()
        } else {
            vfs::path::Path::validate_and_split(relative_path_utf8)
                .map_err(|_| ModelError::path_invalid(relative_path_utf8))?
        };

        self.entry.open(
            self.execution_scope.clone(),
            flags,
            open_mode,
            relative_path,
            ServerEnd::new(channel::take_channel(server_end)),
        );

        Ok(())
    }
}
