// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        crypt::Crypt,
        errors::FxfsError,
        log::*,
        object_store::{
            allocator::Allocator,
            directory::ObjectDescriptor,
            transaction::{LockKey, Options},
            volume::RootVolume,
            ObjectStore,
        },
        platform::{
            fuchsia::{
                component::map_to_raw_status,
                volume::{FxVolumeAndRoot, DEFAULT_FLUSH_PERIOD},
            },
            RemoteCrypt,
        },
    },
    anyhow::{anyhow, bail, ensure, Context, Error},
    fidl::endpoints::{ClientEnd, DiscoverableProtocolMarker, ServerEnd},
    fidl_fuchsia_fs::{AdminMarker, AdminRequest, AdminRequestStream},
    fidl_fuchsia_fxfs::{
        CryptMarker, CryptProxy, MountOptions, VolumeRequest, VolumeRequestStream,
    },
    fidl_fuchsia_io as fio,
    fs_inspect::{FsInspect, FsInspectTree},
    fuchsia_async as fasync,
    fuchsia_zircon::{self as zx, AsHandleRef, Status},
    futures::TryStreamExt,
    std::{
        collections::{hash_map::Entry::Occupied, HashMap},
        sync::{Arc, Mutex, Weak},
    },
    vfs::{
        self,
        directory::{entry::DirectoryEntry, helper::DirectlyMutable},
        path::Path,
    },
};

/// VolumesDirectory is a special pseudo-directory used to enumerate and operate on volumes.
/// Volume creation happens via fuchsia.fxfs.Volumes.Create, rather than open.
///
/// Note that VolumesDirectory assumes exclusive access to |root_volume| and if volumes are
/// manipulated from elsewhere, strange things will happen.
pub struct VolumesDirectory {
    root_volume: RootVolume,
    directory_node: Arc<vfs::directory::immutable::Simple>,
    mounted_volumes: Mutex<HashMap<u64, FxVolumeAndRoot>>,
    // TODO(fxbug.dev/99792): The inspect hierarchy assumes a single volume.  For now, we bind
    // _inspect_tree to the first volume which is created or mounted, but eventually it should cover
    // all volumes.
    _inspect_tree: Mutex<Option<FsInspectTree>>,
}

impl VolumesDirectory {
    /// Fills the VolumesDirectory with all volumes in |root_volume|.  No volume is opened during
    /// this.
    pub async fn new(root_volume: RootVolume) -> Result<Arc<Self>, Error> {
        let layer_set = root_volume.volume_directory().store().tree().layer_set();
        let mut merger = layer_set.merger();
        let me = Arc::new(Self {
            root_volume,
            directory_node: vfs::directory::immutable::simple(),
            mounted_volumes: Mutex::new(HashMap::new()),
            _inspect_tree: Mutex::new(None),
        });
        let mut iter = me.root_volume.volume_directory().iter(&mut merger).await?;
        while let Some((name, store_id, object_descriptor)) = iter.get() {
            ensure!(*object_descriptor == ObjectDescriptor::Volume, FxfsError::Inconsistent);

            me.add_directory_entry(name, store_id);

            iter.advance().await?;
        }
        Ok(me)
    }

    /// Returns the directory node which can be used to provide connections for e.g. enumerating
    /// entries in the VolumesDirectory.
    /// Directly manipulating the entries in this node will result in strange behaviour.
    pub fn directory_node(&self) -> &Arc<vfs::directory::immutable::Simple> {
        &self.directory_node
    }

    fn add_directory_entry(self: &Arc<Self>, name: &str, store_id: u64) {
        let weak = Arc::downgrade(self);
        self.directory_node
            .add_entry(
                name,
                vfs::service::host(move |requests| {
                    let weak = weak.clone();
                    async move {
                        if let Some(me) = weak.upgrade() {
                            let _ = me.handle_volume_requests(requests, store_id).await;
                        }
                    }
                }),
            )
            .unwrap();
    }

    /// Creates a volume.  If |crypt| is set, the volume will be encrypted.
    pub async fn create_volume(
        self: &Arc<Self>,
        name: &str,
        crypt: Option<Arc<dyn Crypt>>,
    ) -> Result<FxVolumeAndRoot, Error> {
        let store = self.root_volume.volume_directory().store();
        let fs = store.filesystem();
        let _write_guard = fs
            .write_lock(&[LockKey::object(
                store.store_object_id(),
                self.root_volume.volume_directory().object_id(),
            )])
            .await;
        ensure!(
            matches!(self.directory_node.get_entry(name), Err(Status::NOT_FOUND)),
            FxfsError::AlreadyExists
        );
        let volume = self.mount_store(self.root_volume.new_volume(name, crypt).await?).await?;
        self.add_directory_entry(name, volume.volume().store().store_object_id());
        Ok(volume)
    }

    #[cfg(test)]
    async fn mount_volume(
        self: &Arc<Self>,
        name: &str,
        crypt: Option<Arc<dyn Crypt>>,
    ) -> Result<FxVolumeAndRoot, Error> {
        let store = self.root_volume.volume_directory().store();
        let fs = store.filesystem();
        let _write_guard = fs
            .write_lock(&[LockKey::object(
                store.store_object_id(),
                self.root_volume.volume_directory().object_id(),
            )])
            .await;
        let store = self.root_volume.volume(name, crypt).await?;
        ensure!(
            !self.mounted_volumes.lock().unwrap().contains_key(&store.store_object_id()),
            FxfsError::AlreadyBound
        );
        self.mount_store(store).await
    }

    // Mounts the given store.  A lock *must* be held on the volume directory.
    async fn mount_store(
        self: &Arc<Self>,
        store: Arc<ObjectStore>,
    ) -> Result<FxVolumeAndRoot, Error> {
        let store_id = store.store_object_id();
        store.record_metrics(&format!("{}", store_id));
        let unique_id = zx::Event::create().expect("Failed to create event");
        let volume = FxVolumeAndRoot::new(store, unique_id.get_koid().unwrap().raw_koid()).await?;
        volume.volume().start_flush_task(DEFAULT_FLUSH_PERIOD);
        self.mounted_volumes.lock().unwrap().insert(store_id, volume.clone());
        self._inspect_tree.lock().unwrap().get_or_insert_with(|| {
            FsInspectTree::new(
                Arc::downgrade(volume.volume()) as Weak<dyn FsInspect + Send + Sync>,
                &crate::metrics::FXFS_ROOT_NODE.lock().unwrap(),
            )
        });
        Ok(volume)
    }

    /// Removes a volume. The volume must exist but encrypted volume keys are not required.
    pub async fn remove_volume(&self, name: &str) -> Result<(), Error> {
        let store = self.root_volume.volume_directory().store();
        let fs = store.filesystem();
        let _write_guard = fs
            .write_lock(&[LockKey::object(
                store.store_object_id(),
                self.root_volume.volume_directory().object_id(),
            )])
            .await;
        let object_id = match self
            .root_volume
            .volume_directory()
            .lookup(name)
            .await?
            .ok_or(FxfsError::NotFound)?
        {
            (object_id, ObjectDescriptor::Volume) => object_id,
            _ => bail!(anyhow!(FxfsError::Inconsistent).context("Expected volume")),
        };
        // Cowardly refuse to delete a mounted volume.
        ensure!(
            !self.mounted_volumes.lock().unwrap().contains_key(&object_id),
            FxfsError::AlreadyBound
        );
        self.root_volume.delete_volume(name).await?;
        // This shouldn't fail because the entry should exist.
        self.directory_node.remove_entry(name, /* must_be_directory: */ false).unwrap();
        Ok(())
    }

    /// Terminates all opened volumes.
    pub async fn terminate(&self) {
        let root_store = self.root_volume.volume_directory().store();
        let fs = root_store.filesystem();
        let _write_guard = fs
            .write_lock(&[LockKey::object(
                root_store.store_object_id(),
                self.root_volume.volume_directory().object_id(),
            )])
            .await;

        let volumes = std::mem::take(&mut *self.mounted_volumes.lock().unwrap());
        for (_, volume) in volumes {
            volume.volume().terminate().await;
        }
    }

    /// Serves the given volume on `outgoing_dir_server_end`.
    pub async fn serve_volume(
        self: &Arc<Self>,
        volume: &FxVolumeAndRoot,
        outgoing_dir_server_end: ServerEnd<fio::DirectoryMarker>,
    ) -> Result<(), Error> {
        let outgoing_dir = vfs::directory::immutable::simple();

        outgoing_dir.add_entry("root", volume.root().clone())?;
        let svc_dir = vfs::directory::immutable::simple();
        outgoing_dir.add_entry("svc", svc_dir.clone())?;

        let store_id = volume.volume().store().store_object_id();
        let me = self.clone();
        svc_dir.add_entry(
            AdminMarker::PROTOCOL_NAME,
            vfs::service::host(move |requests| {
                let me = me.clone();
                async move {
                    let _ = me.handle_admin_requests(requests, store_id).await;
                }
            }),
        )?;

        // Use the volume's scope here which should be OK for now.  In theory the scope represents a
        // filesystem instance and the pseudo filesystem we are using is arguably a different
        // filesystem to the volume we are exporting.  The reality is that it only matters for
        // GetToken and mutable methods which are not supported by the immutable version of Simple.
        let scope = volume.volume().scope().clone();
        outgoing_dir.open(
            scope.clone(),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
            0,
            Path::dot(),
            outgoing_dir_server_end.into_channel().into(),
        );

        // Automatically unmount when all channels are closed.
        let me = self.clone();
        fasync::Task::spawn(async move {
            scope.wait().await;
            info!(store_id, "Last connection to volume closed, shutting down");

            let root_store = me.root_volume.volume_directory().store();
            let fs = root_store.filesystem();
            let _write_guard = fs
                .write_lock(&[LockKey::object(
                    root_store.store_object_id(),
                    me.root_volume.volume_directory().object_id(),
                )])
                .await;

            let volume = {
                let mut mounted_volumes = me.mounted_volumes.lock().unwrap();
                let entry = mounted_volumes.entry(store_id);
                if let Occupied(entry) = entry {
                    if entry.get().volume().scope() == &scope {
                        entry.remove_entry().1
                    } else {
                        return;
                    }
                } else {
                    return;
                }
            };
            volume.volume().terminate().await;
        })
        .detach();

        info!(store_id, "Serving volume");
        Ok(())
    }

    /// Creates and serves the volume with the given name.
    pub async fn create_and_serve_volume(
        self: &Arc<Self>,
        name: &str,
        crypt: Option<ClientEnd<CryptMarker>>,
        outgoing_directory: ServerEnd<fio::DirectoryMarker>,
    ) -> Result<(), Error> {
        let crypt = crypt
            .map(|crypt| Arc::new(RemoteCrypt::new(crypt.into_proxy().unwrap())) as Arc<dyn Crypt>);
        self.serve_volume(&self.create_volume(&name, crypt).await?, outgoing_directory).await
    }

    async fn handle_volume_requests(
        self: &Arc<Self>,
        mut requests: VolumeRequestStream,
        store_id: u64,
    ) -> Result<(), Error> {
        while let Some(request) = requests.try_next().await? {
            match request {
                VolumeRequest::Mount { responder, outgoing_directory, options } => responder.send(
                    &mut self
                        .handle_mount(store_id, outgoing_directory, options)
                        .await
                        .map_err(map_to_raw_status),
                )?,
                VolumeRequest::SetLimit { responder, bytes } => responder.send(
                    &mut self.handle_set_limit(store_id, bytes).await.map_err(map_to_raw_status),
                )?,
            }
        }
        Ok(())
    }

    async fn handle_set_limit(self: &Arc<Self>, store_id: u64, bytes: u64) -> Result<(), Error> {
        let fs = self.root_volume.volume_directory().store().filesystem();
        let mut transaction = fs.clone().new_transaction(&[], Options::default()).await?;
        fs.allocator().set_bytes_limit(&mut transaction, store_id, bytes).await?;
        transaction.commit().await?;
        Ok(())
    }

    async fn handle_mount(
        self: &Arc<Self>,
        store_id: u64,
        outgoing_directory: ServerEnd<fio::DirectoryMarker>,
        options: MountOptions,
    ) -> Result<(), Error> {
        let store = self.root_volume.volume_directory().store();
        let fs = store.filesystem();
        let _write_guard = fs
            .write_lock(&[LockKey::object(
                store.store_object_id(),
                self.root_volume.volume_directory().object_id(),
            )])
            .await;
        ensure!(
            !self.mounted_volumes.lock().unwrap().contains_key(&store_id),
            FxfsError::AlreadyBound
        );

        let crypt = if let Some(crypt) = options.crypt {
            Some(Arc::new(RemoteCrypt::new(CryptProxy::new(fasync::Channel::from_channel(
                crypt.into_channel(),
            )?))) as Arc<dyn Crypt>)
        } else {
            None
        };

        let volume =
            self.mount_store(self.root_volume.volume_from_id(store_id, crypt).await?).await?;

        self.serve_volume(&volume, outgoing_directory).await
    }

    async fn handle_admin_requests(
        self: &Arc<Self>,
        mut stream: AdminRequestStream,
        store_id: u64,
    ) -> Result<(), Error> {
        // If the Admin protocol ever supports more methods, this should change to a while.
        if let Some(request) = stream.try_next().await.context("Reading request")? {
            match request {
                AdminRequest::Shutdown { responder } => {
                    info!(store_id, "Received shutdown request for volume");

                    let root_store = self.root_volume.volume_directory().store();
                    let fs = root_store.filesystem();
                    let write_guard = fs
                        .write_lock(&[LockKey::object(
                            root_store.store_object_id(),
                            self.root_volume.volume_directory().object_id(),
                        )])
                        .await;
                    let me = self.clone();

                    // unmount will indirectly call scope.shutdown which will drop the task that we
                    // are running on, so we spawn onto a new task that won't get dropped.  An
                    // alternative would be to separate the execution-scopes for the volume and
                    // pseudo-filesystem.
                    fasync::Task::spawn(async move {
                        let _ = stream;
                        let _ = write_guard;
                        let _ = me.unmount(store_id).await;
                        responder
                            .send()
                            .unwrap_or_else(|e| warn!("Failed to send shutdown response: {}", e));
                    })
                    .detach();
                }
            }
        }
        Ok(())
    }

    // Unmounts the volume identified by `store_id`.  The caller should take locks to avoid races if
    // necessary.
    async fn unmount(&self, store_id: u64) -> Result<(), Error> {
        let volume =
            self.mounted_volumes.lock().unwrap().remove(&store_id).ok_or(FxfsError::NotFound)?;
        volume.volume().terminate().await;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {
        crate::{
            crypt::{
                insecure::{self, InsecureCrypt},
                Crypt,
            },
            errors::FxfsError,
            filesystem::{Filesystem, FxFilesystem},
            fsck::fsck,
            object_store::allocator::SimpleAllocator,
            object_store::volume::root_volume,
            platform::fuchsia::testing::open_file_checked,
            platform::fuchsia::volumes_directory::VolumesDirectory,
        },
        fidl::endpoints::create_request_stream,
        fidl::{encoding::Decodable, endpoints::ServerEnd},
        fidl_fuchsia_fs::AdminMarker,
        fidl_fuchsia_fxfs::{KeyPurpose, MountOptions, VolumeMarker},
        fidl_fuchsia_io as fio, fuchsia_async as fasync,
        fuchsia_component::client::connect_to_protocol_at_dir_svc,
        fuchsia_zircon::Status,
        futures::join,
        std::{sync::Arc, time::Duration},
        storage_device::{fake_device::FakeDevice, DeviceHolder},
        vfs::{directory::entry::DirectoryEntry, execution_scope::ExecutionScope, path::Path},
    };

    #[fasync::run_singlethreaded(test)]
    async fn test_volume_creation() {
        let device = DeviceHolder::new(FakeDevice::new(8192, 512));
        let filesystem = FxFilesystem::new_empty(device).await.unwrap();
        let volumes_directory =
            VolumesDirectory::new(root_volume(&filesystem).await.unwrap()).await.unwrap();

        let crypt = Arc::new(InsecureCrypt::new()) as Arc<dyn Crypt>;
        {
            let vol = volumes_directory
                .create_volume("encrypted", Some(crypt.clone()))
                .await
                .expect("create encrypted volume failed");
            vol.volume().store().store_object_id()
        };

        volumes_directory.terminate().await;
        std::mem::drop(volumes_directory);
        filesystem.close().await.expect("close filesystem failed");
        let device = filesystem.take_device().await;
        device.reopen(false);
        let filesystem = FxFilesystem::open(device).await.unwrap();
        let volumes_directory =
            VolumesDirectory::new(root_volume(&filesystem).await.unwrap()).await.unwrap();

        let error = volumes_directory
            .create_volume("encrypted", Some(crypt.clone()))
            .await
            .err()
            .expect("Creating existing encrypted volume should fail");
        assert!(FxfsError::AlreadyExists.matches(&error));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_volume_reopen() {
        let device = DeviceHolder::new(FakeDevice::new(8192, 512));
        let filesystem = FxFilesystem::new_empty(device).await.unwrap();
        let volumes_directory =
            VolumesDirectory::new(root_volume(&filesystem).await.unwrap()).await.unwrap();

        let crypt = Arc::new(InsecureCrypt::new()) as Arc<dyn Crypt>;
        let volume_id = {
            let vol = volumes_directory
                .create_volume("encrypted", Some(crypt.clone()))
                .await
                .expect("create encrypted volume failed");
            vol.volume().store().store_object_id()
        };

        volumes_directory.terminate().await;
        std::mem::drop(volumes_directory);
        filesystem.close().await.expect("close filesystem failed");
        let device = filesystem.take_device().await;
        device.reopen(false);
        let filesystem = FxFilesystem::open(device).await.unwrap();
        let volumes_directory =
            VolumesDirectory::new(root_volume(&filesystem).await.unwrap()).await.unwrap();

        {
            let vol = volumes_directory
                .mount_volume("encrypted", Some(crypt.clone()))
                .await
                .expect("open existing encrypted volume failed");
            assert_eq!(vol.volume().store().store_object_id(), volume_id);
        }

        volumes_directory.terminate().await;
        std::mem::drop(volumes_directory);
        filesystem.close().await.expect("close filesystem failed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_volume_creation_unencrypted() {
        let device = DeviceHolder::new(FakeDevice::new(8192, 512));
        let filesystem = FxFilesystem::new_empty(device).await.unwrap();
        let volumes_directory =
            VolumesDirectory::new(root_volume(&filesystem).await.unwrap()).await.unwrap();

        {
            let vol = volumes_directory
                .create_volume("unencrypted", None)
                .await
                .expect("create unencrypted volume failed");
            vol.volume().store().store_object_id()
        };

        volumes_directory.terminate().await;
        std::mem::drop(volumes_directory);
        filesystem.close().await.expect("close filesystem failed");
        let device = filesystem.take_device().await;
        device.reopen(false);
        let filesystem = FxFilesystem::open(device).await.unwrap();
        let volumes_directory =
            VolumesDirectory::new(root_volume(&filesystem).await.unwrap()).await.unwrap();

        let error = volumes_directory
            .create_volume("unencrypted", None)
            .await
            .err()
            .expect("Creating existing unencrypted volume should fail");
        assert!(FxfsError::AlreadyExists.matches(&error));

        volumes_directory.terminate().await;
        std::mem::drop(volumes_directory);
        filesystem.close().await.expect("close filesystem failed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_volume_reopen_unencrypted() {
        let device = DeviceHolder::new(FakeDevice::new(8192, 512));
        let filesystem = FxFilesystem::new_empty(device).await.unwrap();
        let volumes_directory =
            VolumesDirectory::new(root_volume(&filesystem).await.unwrap()).await.unwrap();

        let volume_id = {
            let vol = volumes_directory
                .create_volume("unencrypted", None)
                .await
                .expect("create unencrypted volume failed");
            vol.volume().store().store_object_id()
        };

        volumes_directory.terminate().await;
        std::mem::drop(volumes_directory);
        filesystem.close().await.expect("close filesystem failed");
        let device = filesystem.take_device().await;
        device.reopen(false);
        let filesystem = FxFilesystem::open(device).await.unwrap();
        let volumes_directory =
            VolumesDirectory::new(root_volume(&filesystem).await.unwrap()).await.unwrap();

        {
            let vol = volumes_directory
                .mount_volume("unencrypted", None)
                .await
                .expect("open existing unencrypted volume failed");
            assert_eq!(vol.volume().store().store_object_id(), volume_id);
        }

        volumes_directory.terminate().await;
        std::mem::drop(volumes_directory);
        filesystem.close().await.expect("close filesystem failed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_volume_enumeration() {
        let device = DeviceHolder::new(FakeDevice::new(8192, 512));
        let filesystem = FxFilesystem::new_empty(device).await.unwrap();
        let volumes_directory =
            VolumesDirectory::new(root_volume(&filesystem).await.unwrap()).await.unwrap();

        // Add an encrypted volume...
        let crypt = Arc::new(InsecureCrypt::new()) as Arc<dyn Crypt>;
        {
            volumes_directory
                .create_volume("encrypted", Some(crypt.clone()))
                .await
                .expect("create encrypted volume failed");
        };
        // And an unencrypted volume.
        {
            volumes_directory
                .create_volume("unencrypted", None)
                .await
                .expect("create unencrypted volume failed");
        };

        // Restart, so that we can test enumeration of unopened volumes.
        volumes_directory.terminate().await;
        std::mem::drop(volumes_directory);
        filesystem.close().await.expect("close filesystem failed");
        let device = filesystem.take_device().await;
        device.reopen(false);
        let filesystem = FxFilesystem::open(device).await.unwrap();
        let volumes_directory =
            VolumesDirectory::new(root_volume(&filesystem).await.unwrap()).await.unwrap();

        let readdir = |dir: Arc<fio::DirectoryProxy>| async move {
            let status = dir.rewind().await.expect("FIDL call failed");
            Status::ok(status).expect("rewind failed");
            let (status, buf) = dir.read_dirents(fio::MAX_BUF).await.expect("FIDL call failed");
            Status::ok(status).expect("read_dirents failed");
            let mut entries = vec![];
            for res in fuchsia_fs::directory::parse_dir_entries(&buf) {
                entries.push(res.expect("Failed to parse entry").name);
            }
            entries
        };

        let (dir_proxy, dir_server_end) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>()
            .expect("Create proxy to succeed");
        let dir_proxy = Arc::new(dir_proxy);

        volumes_directory.directory_node().clone().open(
            ExecutionScope::new(),
            fio::OpenFlags::DIRECTORY | fio::OpenFlags::RIGHT_READABLE,
            fio::MODE_TYPE_DIRECTORY,
            Path::dot(),
            ServerEnd::new(dir_server_end.into_channel()),
        );

        let entries = readdir(dir_proxy.clone()).await;
        assert_eq!(entries, [".", "encrypted", "unencrypted"]);

        let _vol = volumes_directory
            .mount_volume("encrypted", Some(crypt.clone()))
            .await
            .expect("Open encrypted volume failed");

        // Ensure that the behaviour is the same after we've opened a volume.
        let entries = readdir(dir_proxy).await;
        assert_eq!(entries, [".", "encrypted", "unencrypted"]);

        volumes_directory.terminate().await;
        std::mem::drop(volumes_directory);
        filesystem.close().await.expect("close filesystem failed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_deleted_encrypted_volume_while_mounted() {
        const VOLUME_NAME: &str = "encrypted";

        let device = DeviceHolder::new(FakeDevice::new(8192, 512));
        let filesystem = FxFilesystem::new_empty(device).await.unwrap();
        let crypt = Arc::new(InsecureCrypt::new()) as Arc<dyn Crypt>;
        let volumes_directory =
            VolumesDirectory::new(root_volume(&filesystem).await.unwrap()).await.unwrap();
        volumes_directory
            .create_volume(VOLUME_NAME, Some(crypt.clone()))
            .await
            .expect("create encrypted volume failed");
        // We have the volume mounted so delete attempts should fail.
        assert!(FxfsError::AlreadyBound.matches(
            &volumes_directory
                .remove_volume(VOLUME_NAME)
                .await
                .err()
                .expect("Deleting volume should fail")
        ));
        volumes_directory.terminate().await;
        std::mem::drop(volumes_directory);
        filesystem.close().await.expect("close filesystem failed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_mount_volume_using_volume_protocol() {
        let device = DeviceHolder::new(FakeDevice::new(8192, 512));
        let filesystem = FxFilesystem::new_empty(device).await.unwrap();
        let volumes_directory =
            VolumesDirectory::new(root_volume(&filesystem).await.unwrap()).await.unwrap();

        let crypt = Arc::new(InsecureCrypt::new()) as Arc<dyn Crypt>;
        let store_id = {
            let vol = volumes_directory
                .create_volume("encrypted", Some(crypt.clone()))
                .await
                .expect("create encrypted volume failed");
            vol.volume().store().store_object_id()
        };
        volumes_directory.unmount(store_id).await.expect("unmount failed");

        let (volume_proxy, volume_server_end) =
            fidl::endpoints::create_proxy::<VolumeMarker>().expect("Create proxy to succeed");
        volumes_directory.directory_node().clone().open(
            ExecutionScope::new(),
            fio::OpenFlags::RIGHT_READABLE,
            0,
            Path::validate_and_split("encrypted").unwrap(),
            volume_server_end.into_channel().into(),
        );

        let (dir_proxy, dir_server_end) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>()
            .expect("Create proxy to succeed");

        let crypt_service = fxfs_crypt::CryptService::new();
        crypt_service
            .add_wrapping_key(0, insecure::DATA_KEY.to_vec())
            .expect("add_wrapping_key failed");
        crypt_service
            .add_wrapping_key(1, insecure::METADATA_KEY.to_vec())
            .expect("add_wrapping_key failed");
        crypt_service.set_active_key(KeyPurpose::Data, 0).expect("set_active_key failed");
        crypt_service.set_active_key(KeyPurpose::Metadata, 1).expect("set_active_key failed");
        let (client1, stream1) = create_request_stream().expect("create_endpoints failed");
        let (client2, stream2) = create_request_stream().expect("create_endpoints failed");

        join!(
            async {
                volume_proxy
                    .mount(
                        dir_server_end,
                        &mut MountOptions { crypt: Some(client1), ..MountOptions::new_empty() },
                    )
                    .await
                    .expect("mount (fidl) failed")
                    .expect("mount failed");

                open_file_checked(
                    &dir_proxy,
                    fio::OpenFlags::RIGHT_READABLE
                        | fio::OpenFlags::RIGHT_WRITABLE
                        | fio::OpenFlags::CREATE,
                    0,
                    "root/test",
                )
                .await;

                // Attempting to mount again should fail with ALREADY_BOUND.
                let (_dir_proxy, dir_server_end) =
                    fidl::endpoints::create_proxy::<fio::DirectoryMarker>()
                        .expect("Create proxy to succeed");

                assert_eq!(
                    Status::from_raw(
                        volume_proxy
                            .mount(
                                dir_server_end,
                                &mut MountOptions {
                                    crypt: Some(client2),
                                    ..MountOptions::new_empty()
                                },
                            )
                            .await
                            .expect("mount (fidl) failed")
                            .expect_err("mount succeeded")
                    ),
                    Status::ALREADY_BOUND
                );

                std::mem::drop(dir_proxy);

                // The volume should get unmounted a short time later.
                let mut count = 0;
                loop {
                    if volumes_directory.mounted_volumes.lock().unwrap().is_empty() {
                        break;
                    }
                    count += 1;
                    assert!(count <= 100);
                    fasync::Timer::new(Duration::from_millis(100)).await;
                }
            },
            async {
                crypt_service
                    .handle_request(fxfs_crypt::Services::Crypt(stream1))
                    .await
                    .expect("handle_request failed");
                crypt_service
                    .handle_request(fxfs_crypt::Services::Crypt(stream2))
                    .await
                    .expect("handle_request failed");
            }
        );
        // Make sure the background thread that actually calls terminate() on the volume finishes
        // before exiting the test. terminate() should be a no-op since we already verified
        // mounted_directories is empty, but the volume's terminate() future in the background task
        // may still be outstanding. As both the background task and VolumesDirectory::terminate()
        // hold the write lock, we use that to block until the background task has completed.
        volumes_directory.terminate().await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_shutdown_volume() {
        let device = DeviceHolder::new(FakeDevice::new(8192, 512));
        let filesystem = FxFilesystem::new_empty(device).await.unwrap();
        let volumes_directory =
            VolumesDirectory::new(root_volume(&filesystem).await.unwrap()).await.unwrap();

        let crypt = Arc::new(InsecureCrypt::new()) as Arc<dyn Crypt>;
        let vol = volumes_directory
            .create_volume("encrypted", Some(crypt.clone()))
            .await
            .expect("create encrypted volume failed");

        let (dir_proxy, dir_server_end) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>()
            .expect("Create proxy to succeed");

        volumes_directory.serve_volume(&vol, dir_server_end).await.expect("serve_volume failed");

        let admin_proxy = connect_to_protocol_at_dir_svc::<AdminMarker>(&dir_proxy)
            .expect("Unable to connect to admin service");

        admin_proxy.shutdown().await.expect("shutdown failed");

        assert!(volumes_directory.mounted_volumes.lock().unwrap().is_empty());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_byte_limit_persistence() {
        const BYTES_LIMIT_1: u64 = 123456;
        const BYTES_LIMIT_2: u64 = 456789;
        const VOLUME_NAME: &str = "A";
        let mut device = DeviceHolder::new(FakeDevice::new(8192, 512));
        {
            let filesystem = FxFilesystem::new_empty(device).await.unwrap();
            let volumes_directory =
                VolumesDirectory::new(root_volume(&filesystem).await.unwrap()).await.unwrap();

            volumes_directory
                .create_volume(VOLUME_NAME, None)
                .await
                .expect("create unencrypted volume failed");

            let (volume_proxy, volume_server_end) =
                fidl::endpoints::create_proxy::<VolumeMarker>().expect("Create proxy to succeed");
            volumes_directory.directory_node().clone().open(
                ExecutionScope::new(),
                fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
                0,
                Path::validate_and_split(VOLUME_NAME).unwrap(),
                volume_server_end.into_channel().into(),
            );

            volume_proxy.set_limit(BYTES_LIMIT_1).await.unwrap().expect("To set limits");
            {
                let limits = (filesystem.allocator() as Arc<SimpleAllocator>).owner_byte_limits();
                assert_eq!(limits.len(), 1);
                assert_eq!(limits[0].1, BYTES_LIMIT_1);
            }

            volume_proxy.set_limit(BYTES_LIMIT_2).await.unwrap().expect("To set limits");
            {
                let limits = (filesystem.allocator() as Arc<SimpleAllocator>).owner_byte_limits();
                assert_eq!(limits.len(), 1);
                assert_eq!(limits[0].1, BYTES_LIMIT_2);
            }
            std::mem::drop(volume_proxy);
            volumes_directory.terminate().await;
            std::mem::drop(volumes_directory);
            filesystem.close().await.expect("close filesystem failed");
            device = filesystem.take_device().await;
        }
        device.ensure_unique();
        device.reopen(false);
        {
            let filesystem = FxFilesystem::open(device as DeviceHolder).await.unwrap();
            fsck(&filesystem, None).await.expect("Fsck");
            let volumes_directory =
                VolumesDirectory::new(root_volume(&filesystem).await.unwrap()).await.unwrap();
            {
                let limits = (filesystem.allocator() as Arc<SimpleAllocator>).owner_byte_limits();
                assert_eq!(limits.len(), 1);
                assert_eq!(limits[0].1, BYTES_LIMIT_2);
            }
            volumes_directory.remove_volume(VOLUME_NAME).await.expect("Volume deletion failed");
            {
                let limits = (filesystem.allocator() as Arc<SimpleAllocator>).owner_byte_limits();
                assert_eq!(limits.len(), 0);
            }
            volumes_directory.terminate().await;
            std::mem::drop(volumes_directory);
            filesystem.close().await.expect("close filesystem failed");
            device = filesystem.take_device().await;
        }
        device.ensure_unique();
        device.reopen(false);
        let filesystem = FxFilesystem::open(device as DeviceHolder).await.unwrap();
        fsck(&filesystem, None).await.expect("Fsck");
        let limits = (filesystem.allocator() as Arc<SimpleAllocator>).owner_byte_limits();
        assert_eq!(limits.len(), 0);
    }
}
