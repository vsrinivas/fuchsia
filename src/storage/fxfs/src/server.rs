// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{object_store::FxFilesystem, server::volume::FxVolumeAndRoot, volume::root_volume},
    anyhow::{Context, Error},
    fidl_fuchsia_fs::{AdminRequest, AdminRequestStream, QueryRequest, QueryRequestStream},
    fidl_fuchsia_io::{self as fio, DirectoryMarker},
    fuchsia_component::server::ServiceFs,
    fuchsia_zircon::{self as zx},
    futures::stream::{StreamExt, TryStreamExt},
    futures::TryFutureExt,
    std::sync::Arc,
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
    fs: Arc<FxFilesystem>,
    // TODO(jfsulliv): we'd like to support multiple volumes, but not clear how to multiplex
    // requests.
    volume: FxVolumeAndRoot,
}

impl FxfsServer {
    /// Creates a new FxfsServer by opening or creating |volume_name| in |fs|.
    pub async fn new(fs: Arc<FxFilesystem>, volume_name: &str) -> Result<Self, Error> {
        let volume_dir = root_volume(&fs).await?;
        let volume = FxVolumeAndRoot::new(
            volume_dir
                .open_or_create_volume(volume_name)
                .await
                .expect("Open or create volume failed"),
        )
        .await;
        Ok(Self { fs, volume })
    }

    pub async fn run(&mut self, outgoing_chan: zx::Channel) -> Result<(), Error> {
        // VFS initialization.
        let registry = token_registry::Simple::new();
        let scope = ExecutionScope::build().token_registry(registry).new();
        let (proxy, server) = fidl::endpoints::create_proxy::<DirectoryMarker>()?;

        self.volume.root().clone().open(
            scope.clone(),
            fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE,
            0,
            Path::empty(),
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
            self.handle_request(request, &scope).unwrap_or_else(|e| log::error!("{:?}", e))
        })
        .await;

        // At this point all direct connections to ServiceFs will have been closed (and cannot be
        // resurrected), but before we finish, we must wait for all VFS connections to be closed.
        scope.wait().await;

        self.fs.close().await.unwrap_or_else(|e| log::error!("Failed to shutdown fxfs: {:?}", e));

        Ok(())
    }

    async fn handle_admin(&self, scope: &ExecutionScope, req: AdminRequest) -> Result<(), Error> {
        match req {
            AdminRequest::Shutdown { responder } => {
                scope.shutdown();
                responder.send()?;
            }
            _ => {
                panic!("Unimplemented")
            }
        }
        Ok(())
    }

    async fn handle_query(&self, _scope: &ExecutionScope, _req: QueryRequest) -> Result<(), Error> {
        panic!("Unimplemented")
    }

    async fn handle_request(&self, stream: Services, scope: &ExecutionScope) -> Result<(), Error> {
        match stream {
            Services::Admin(mut stream) => {
                while let Some(request) = stream.try_next().await.context("Reading request")? {
                    self.handle_admin(scope, request).await?;
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
        crate::{object_store::FxFilesystem, server::FxfsServer, testing::fake_device::FakeDevice},
        anyhow::Error,
        fidl_fuchsia_fs::AdminMarker,
        fidl_fuchsia_io::DirectoryMarker,
        fuchsia_async as fasync,
        std::sync::Arc,
    };

    #[fasync::run(2, test)]
    async fn test_lifecycle() -> Result<(), Error> {
        let device = Arc::new(FakeDevice::new(2048, 512));
        let filesystem = FxFilesystem::new_empty(device.clone()).await?;
        let mut server = FxfsServer::new(filesystem, "root").await.expect("Create server failed");

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
