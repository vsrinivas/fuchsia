// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        errors::FxfsError,
        object_store::{
            filesystem::{Filesystem, OpenFxFilesystem},
            volume::root_volume,
        },
        server::volume::FxVolumeAndRoot,
    },
    anyhow::{Context, Error},
    fidl_fuchsia_fs::{
        AdminRequest, AdminRequestStream, FilesystemInfo, FsType, QueryRequest, QueryRequestStream,
    },
    fidl_fuchsia_io::{self as fio, DirectoryMarker},
    fuchsia_component::server::ServiceFs,
    fuchsia_zircon::{self as zx},
    futures::stream::{StreamExt, TryStreamExt},
    futures::TryFutureExt,
    std::sync::atomic::{self, AtomicBool},
    vfs::{
        directory::entry::DirectoryEntry, execution_scope::ExecutionScope, path::Path,
        registry::token_registry,
    },
};

pub mod directory;
pub mod errors;
pub mod file;
pub mod node;
pub mod volume;

#[cfg(test)]
mod testing;

enum Services {
    Admin(AdminRequestStream),
    Query(QueryRequestStream),
}

pub struct FxfsServer {
    fs: OpenFxFilesystem,
    // TODO(jfsulliv): we'd like to support multiple volumes, but not clear how to multiplex
    // requests.
    volume: FxVolumeAndRoot,
    closed: AtomicBool,
}

impl FxfsServer {
    /// Creates a new FxfsServer by opening or creating |volume_name| in |fs|.
    pub async fn new(fs: OpenFxFilesystem, volume_name: &str) -> Result<Self, Error> {
        let volume_dir = root_volume(&fs).await?.ok_or(FxfsError::Inconsistent)?;
        let volume = FxVolumeAndRoot::new(
            volume_dir
                .open_or_create_volume(volume_name)
                .await
                .expect("Open or create volume failed"),
        )
        .await?;
        Ok(Self { fs, volume, closed: AtomicBool::new(false) })
    }

    pub async fn run(self, outgoing_chan: zx::Channel) -> Result<(), Error> {
        // VFS initialization.
        let registry = token_registry::Simple::new();
        let scope = ExecutionScope::build().token_registry(registry).new();
        let (proxy, server) = fidl::endpoints::create_proxy::<DirectoryMarker>()?;

        self.volume.volume().start_flush_task(volume::DEFAULT_FLUSH_PERIOD);

        self.volume.root().clone().open(
            scope.clone(),
            fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE,
            0,
            Path::dot(),
            server.into_channel().into(),
        );

        // Export the root directory in our outgoing directory.
        let mut fs = ServiceFs::new();
        fs.add_remote("root", proxy);
        fs.dir("svc").add_fidl_service(Services::Admin).add_fidl_service(Services::Query);
        fs.serve_connection(outgoing_chan)?;

        // Handle all ServiceFs connections. VFS connections will be spawned as separate tasks.
        const MAX_CONCURRENT: usize = 10_000;
        fs.for_each_concurrent(MAX_CONCURRENT, |request| {
            self.handle_request(request, &scope).unwrap_or_else(|e| log::error!("{}", e))
        })
        .await;

        // At this point all direct connections to ServiceFs will have been closed (and cannot be
        // resurrected), but before we finish, we must wait for all VFS connections to be closed.
        scope.wait().await;

        if !self.closed.load(atomic::Ordering::Relaxed) {
            self.volume.volume().terminate().await;
            self.fs.close().await.unwrap_or_else(|e| log::error!("Failed to shutdown fxfs: {}", e));
        }

        Ok(())
    }

    // Returns true if we should close the connection.
    async fn handle_admin(&self, scope: &ExecutionScope, req: AdminRequest) -> Result<bool, Error> {
        match req {
            AdminRequest::Shutdown { responder } => {
                // TODO(csuter): shutdown is brutal and will just drop running tasks which could
                // leave transactions in a half completed state.  VFS should be fixed so that it
                // drops connections at some point that isn't midway through processing a request.
                scope.shutdown();
                self.closed.store(true, atomic::Ordering::Relaxed);
                self.volume.volume().terminate().await;
                self.fs
                    .close()
                    .await
                    .unwrap_or_else(|e| log::error!("Failed to shutdown fxfs: {}", e));
                responder
                    .send()
                    .unwrap_or_else(|e| log::warn!("Failed to send shutdown response: {}", e));
                return Ok(true);
            }
            _ => {
                panic!("Unimplemented")
            }
        }
    }

    async fn handle_query(&self, _scope: &ExecutionScope, req: QueryRequest) -> Result<(), Error> {
        match req {
            QueryRequest::GetInfo { responder, .. } => {
                // TODO(csuter): Support all the fields.
                let info = self.fs.get_info();
                responder.send(&mut Ok(FilesystemInfo {
                    total_bytes: Some(info.total_bytes),
                    used_bytes: Some(info.used_bytes),
                    block_size: Some(info.block_size),
                    max_node_name_size: Some(255), // This is limited by Fuchsia.io
                    fs_type: Some(FsType::Fxfs),
                    ..FilesystemInfo::EMPTY
                }))?;
            }
            _ => panic!("Unimplemented"),
        }
        Ok(())
    }

    async fn handle_request(&self, stream: Services, scope: &ExecutionScope) -> Result<(), Error> {
        match stream {
            Services::Admin(mut stream) => {
                while let Some(request) = stream.try_next().await.context("Reading request")? {
                    if self.handle_admin(scope, request).await? {
                        break;
                    }
                }
            }
            Services::Query(mut stream) => {
                while let Some(request) = stream.try_next().await.context("Reading request")? {
                    self.handle_query(scope, request).await?;
                }
            }
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {
        crate::{
            object_store::{crypt::InsecureCrypt, volume::create_root_volume, FxFilesystem},
            server::FxfsServer,
        },
        anyhow::Error,
        fidl_fuchsia_fs::AdminMarker,
        fidl_fuchsia_io::DirectoryMarker,
        fuchsia_async as fasync,
        std::sync::Arc,
        storage_device::{fake_device::FakeDevice, DeviceHolder},
    };

    #[fasync::run(2, test)]
    async fn test_lifecycle() -> Result<(), Error> {
        let device = DeviceHolder::new(FakeDevice::new(16384, 512));
        let filesystem = FxFilesystem::new_empty(device, Arc::new(InsecureCrypt::new())).await?;
        create_root_volume(&filesystem).await?;
        let server = FxfsServer::new(filesystem, "root").await.expect("Create server failed");

        let (client_end, server_end) = fidl::endpoints::create_endpoints::<DirectoryMarker>()?;
        let client_proxy = client_end.into_proxy().expect("Create DirectoryProxy failed");
        fasync::Task::spawn(async move {
            let admin_proxy = fuchsia_component::client::connect_to_protocol_at_dir_svc::<
                AdminMarker,
            >(&client_proxy)
            .expect("Connect to Admin service failed");
            admin_proxy.shutdown().await.expect("Shutdown failed");
        })
        .detach();

        server.run(server_end.into_channel()).await.expect("Run returned an error");
        Ok(())
    }
}
