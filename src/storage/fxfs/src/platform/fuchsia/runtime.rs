// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        crypt::Crypt,
        filesystem::{Filesystem, Info, OpenFxFilesystem},
        object_store::volume::root_volume,
        platform::fuchsia::{volumes_directory::VolumesDirectory, RemoteCrypt},
        serialized_types::LATEST_VERSION,
    },
    anyhow::{Context, Error},
    async_trait::async_trait,
    fidl_fuchsia_fs::{AdminRequest, AdminRequestStream},
    fidl_fuchsia_fxfs::{VolumesRequest, VolumesRequestStream},
    fidl_fuchsia_io as fio,
    fuchsia_component::server::ServiceFs,
    fuchsia_zircon::{self as zx, AsHandleRef},
    futures::{
        stream::{StreamExt, TryStreamExt},
        TryFutureExt,
    },
    inspect_runtime::service::{TreeServerSendPreference, TreeServerSettings},
    std::sync::{
        atomic::{self, AtomicBool},
        Arc,
    },
    vfs::{
        directory::entry::DirectoryEntry,
        execution_scope::ExecutionScope,
        inspect::{FsInspect, FsInspectTree, InfoData, UsageData, VolumeData},
        path::Path,
        registry::token_registry,
    },
};

// The correct number here is arguably u64::MAX - 1 (because node 0 is reserved). There's a bug
// where inspect test cases fail if we try and use that, possibly because of a signed/unsigned bug.
// See fxbug.dev/87152.  Until that's fixed, we'll have to use i64::MAX.
const TOTAL_NODES: u64 = i64::MAX as u64;

const VFS_TYPE_FXFS: u32 = 0x73667866;

const FXFS_INFO_NAME: &'static str = "fxfs";

// An array used to initialize the FilesystemInfo |name| field. This just spells "fxfs" 0-padded to
// 32 bytes.
const FXFS_INFO_NAME_FIDL: [i8; 32] = [
    0x66, 0x78, 0x66, 0x73, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0,
];

enum Services {
    Admin(AdminRequestStream),
    Volumes(VolumesRequestStream),
}

pub struct FxfsServer {
    /// Ensure the filesystem is closed when this object is dropped.
    fs: OpenFxFilesystem,

    /// The volumes directory, which manages currently opened volumes and implements
    /// fuchsia.fxfs.Volumes.
    volumes: VolumesDirectory,

    /// Set to true once the associated ExecutionScope associated with the server is shut down.
    closed: AtomicBool,

    /// Unique identifier for this filesystem instance (not preserved across reboots) based on
    /// the kernel object ID to guarantee uniqueness within the system.
    unique_id: zx::Event,
}

const DEFAULT_VOLUME_NAME: &str = "default";

impl FxfsServer {
    pub async fn new(fs: OpenFxFilesystem) -> Result<Arc<Self>, Error> {
        let unique_id = zx::Event::create().expect("Failed to create event");
        let volumes = VolumesDirectory::new(root_volume(&fs).await?).await?;
        Ok(Arc::new(Self { fs, volumes, closed: AtomicBool::new(false), unique_id }))
    }

    pub async fn run(
        self: Arc<Self>,
        outgoing_chan: zx::Channel,
        crypt: Option<Arc<dyn Crypt>>,
    ) -> Result<(), Error> {
        // VFS initialization.
        let registry = token_registry::Simple::new();
        let scope = ExecutionScope::build().token_registry(registry).new();
        let (proxy, server) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>()?;

        // TODO(fxbug.dev/99182): We should eventually not open any volumes initially.  For now, to
        // avoid changes to fs_management, keep this behaviour.
        let volume = self
            .volumes
            .open_or_create_volume(DEFAULT_VOLUME_NAME, crypt, false)
            .await
            .expect("open_or_create failed");
        volume.root().clone().open(
            scope.clone(),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
            0,
            Path::dot(),
            server.into_channel().into(),
        );

        // Since fshost currently exports all filesystem inspect trees under its own diagnostics
        // directory, in order to work properly with Lapis, each filesystem must use a uniquely
        // named root node.

        // The Inspect nodes will remain live until `_fs_inspect_nodes` goes out of scope.
        let self_weak = Arc::downgrade(&self);
        let _fs_inspect_nodes =
            FsInspectTree::new(self_weak, &crate::metrics::FXFS_ROOT_NODE.lock().unwrap());

        // Export the root directory in our outgoing directory.
        // TODO(fxbug.dev/99182): Export the volumes directory.
        let mut fs = ServiceFs::new();
        fs.add_remote("root", proxy);
        fs.add_fidl_service(Services::Admin).add_fidl_service(Services::Volumes);
        // Serve static Inspect instance from fuchsia_inspect::component to diagnostic directory.
        // We fall back to DeepCopy mode instead of Live mode (the default) if we could not create a
        // child copy of the backing VMO. This helps prevent issues with the reader acquiring a lock
        // while the the filesystem is under load. See fxbug.dev/57330 for details.
        inspect_runtime::serve_with_options(
            fuchsia_inspect::component::inspector(),
            TreeServerSettings {
                send_vmo_preference: TreeServerSendPreference::frozen_or(
                    TreeServerSendPreference::DeepCopy,
                ),
            },
            &mut fs,
        )?;
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

        if !self.closed.load(atomic::Ordering::SeqCst) {
            volume.volume().terminate().await;
            self.fs.close().await.unwrap_or_else(|e| log::error!("Failed to shutdown fxfs: {}", e));
        }

        Ok(())
    }

    // Returns true if we should close the connection.
    async fn handle_admin(&self, scope: &ExecutionScope, req: AdminRequest) -> Result<bool, Error> {
        match req {
            AdminRequest::Shutdown { responder } => {
                scope.shutdown();
                if self.closed.swap(true, atomic::Ordering::SeqCst) {
                    return Ok(true);
                }
                self.volumes.terminate().await;
                self.fs
                    .close()
                    .await
                    .unwrap_or_else(|e| log::error!("Failed to shutdown fxfs: {}", e));
                responder
                    .send()
                    .unwrap_or_else(|e| log::warn!("Failed to send shutdown response: {}", e));
                return Ok(true);
            }
        }
    }

    async fn handle_volumes(
        &self,
        scope: &ExecutionScope,
        req: VolumesRequest,
    ) -> Result<(), Error> {
        match req {
            VolumesRequest::Create { name, crypt, outgoing_directory, responder } => {
                log::info!("Create volume {:?}", name);
                let crypt = crypt.map(|crypt| {
                    Arc::new(RemoteCrypt::new(crypt.into_proxy().unwrap())) as Arc<dyn Crypt>
                });
                match self.volumes.open_or_create_volume(&name, crypt, true).await {
                    Ok(volume) => {
                        volume.root().clone().open(
                            scope.clone(),
                            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
                            0,
                            Path::dot(),
                            outgoing_directory,
                        );
                        responder.send(&mut Ok(())).unwrap_or_else(|e| {
                            log::warn!("Failed to send volume creation response: {}", e)
                        });
                    }
                    Err(status) => {
                        responder
                            .send_no_shutdown_on_err(&mut Err(status.into_raw()))
                            .unwrap_or_else(|e| {
                                log::warn!("Failed to send volume creation response: {}", e)
                            });
                    }
                };
                Ok(())
            }
            VolumesRequest::Remove { name: _, responder } => {
                // TODO(fxbug.dev/99182)
                responder
                    .send_no_shutdown_on_err(&mut Err(zx::Status::INTERNAL.into_raw()))
                    .unwrap();
                Ok(())
            }
        }
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
            Services::Volumes(mut stream) => {
                while let Some(request) = stream.try_next().await.context("Reading request")? {
                    self.handle_volumes(scope, request).await?;
                }
            }
        }
        Ok(())
    }
}

pub fn info_to_filesystem_info(
    info: Info,
    block_size: u64,
    object_count: u64,
    fs_id: u64,
) -> fio::FilesystemInfo {
    fio::FilesystemInfo {
        total_bytes: info.total_bytes,
        used_bytes: info.used_bytes,
        total_nodes: TOTAL_NODES,
        used_nodes: object_count,
        // TODO(fxbug.dev/93770): Support free_shared_pool_bytes.
        free_shared_pool_bytes: 0,
        fs_id,
        block_size: block_size as u32,
        max_filename_size: fio::MAX_FILENAME as u32,
        fs_type: VFS_TYPE_FXFS,
        padding: 0,
        name: FXFS_INFO_NAME_FIDL,
    }
}

#[async_trait]
impl FsInspect for FxfsServer {
    fn get_info_data(&self) -> InfoData {
        InfoData {
            id: self.unique_id.get_koid().unwrap().raw_koid(),
            fs_type: VFS_TYPE_FXFS.into(),
            name: FXFS_INFO_NAME.into(),
            version_major: LATEST_VERSION.major.into(),
            version_minor: LATEST_VERSION.minor.into(),
            block_size: self.fs.block_size(),
            max_filename_length: fio::MAX_FILENAME,
            // TODO(fxbug.dev/93770): Determine how to report oldest on-disk version if required.
            oldest_version: None,
        }
    }

    async fn get_usage_data(&self) -> UsageData {
        // TODO(fxbug.dev/99792): Figure out how to work with multiple volumes
        let info = self.fs.get_info();
        let object_count =
            self.volumes.get_volume(DEFAULT_VOLUME_NAME).await.unwrap().store().object_count();
        UsageData {
            total_bytes: info.total_bytes,
            used_bytes: info.used_bytes,
            total_nodes: TOTAL_NODES,
            used_nodes: object_count,
        }
    }

    fn get_volume_data(&self) -> VolumeData {
        // Since we're not using FVM these values should all be set to zero.
        VolumeData {
            size_bytes: 0,
            size_limit_bytes: 0,
            available_space_bytes: 0,
            // TODO(fxbug.dev/93770): Handle out of space events.
            out_of_space_events: 0,
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        crate::{crypt::insecure::InsecureCrypt, filesystem::FxFilesystem, platform::FxfsServer},
        fidl_fuchsia_fs::AdminMarker,
        fidl_fuchsia_io as fio, fuchsia_async as fasync,
        std::sync::Arc,
        storage_device::{fake_device::FakeDevice, DeviceHolder},
    };

    #[fasync::run(2, test)]
    async fn test_lifecycle() {
        let device = DeviceHolder::new(FakeDevice::new(16384, 512));
        let filesystem =
            FxFilesystem::new_empty(device).await.expect("FxFilesystem::new_empty failed");
        let server = FxfsServer::new(filesystem).await.expect("FxfsServer::new failed");

        let (client_end, server_end) =
            fidl::endpoints::create_endpoints::<fio::DirectoryMarker>().unwrap();
        let client_proxy = client_end.into_proxy().expect("Create DirectoryProxy failed");
        fasync::Task::spawn(async move {
            let admin_proxy = fuchsia_component::client::connect_to_protocol_at_dir_root::<
                AdminMarker,
            >(&client_proxy)
            .expect("Connect to Admin service failed");
            admin_proxy.shutdown().await.expect("Shutdown failed");
        })
        .detach();

        server
            .run(server_end.into_channel(), Some(Arc::new(InsecureCrypt::new())))
            .await
            .expect("Run returned an error");
    }
}
