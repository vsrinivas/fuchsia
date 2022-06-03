// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        crypt::Crypt,
        errors::FxfsError,
        object_store::{
            directory::ObjectDescriptor,
            transaction::LockKey,
            volume::{OpenOrCreateResult, RootVolume},
        },
        platform::fuchsia::{
            errors::map_to_status,
            volume::{FxVolumeAndRoot, DEFAULT_FLUSH_PERIOD},
        },
    },
    anyhow::{ensure, Error},
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io as fio,
    fuchsia_zircon::{self as zx, AsHandleRef, Status},
    std::{
        collections::HashMap,
        sync::{Arc, Mutex},
    },
    vfs::{
        self,
        common::send_on_open_with_error,
        directory::{
            entry::{DirectoryEntry, EntryInfo},
            helper::DirectlyMutable,
        },
        execution_scope::ExecutionScope,
        path::Path,
    },
};

// The single parameter is the store object ID.
struct VolumesDirectoryEntry(u64);

impl DirectoryEntry for VolumesDirectoryEntry {
    fn open(
        self: Arc<Self>,
        _scope: ExecutionScope,
        flags: fio::OpenFlags,
        _mode: u32,
        _path: Path,
        server_end: ServerEnd<fio::NodeMarker>,
    ) {
        // TODO(fxbug.dev/99182): Export a node which speaks fuchsia.fxfs.Volume.
        send_on_open_with_error(flags, server_end, Status::NOT_SUPPORTED);
    }

    fn entry_info(&self) -> EntryInfo {
        EntryInfo::new(self.0, fio::DirentType::Service)
    }
}

/// VolumesDirectory is a special pseudo-directory used to enumerate and operate on volumes.
/// TODO(fxbug.dev/99663): Volumes should move back into |directory_node| and be re-locked when
/// their last connection drops.
///
/// Volume creation happens via fuchsia.fxfs.Volumes.Create, rather than open.
///
/// Note that VolumesDirectory assumes exclusive access to |root_volume| and if volumes are
/// manipulated from elsewhere, strange things will happen.
pub struct VolumesDirectory {
    root_volume: RootVolume,
    directory_node: Arc<vfs::directory::immutable::Simple>,
    mounted_volumes: Arc<Mutex<HashMap<String, FxVolumeAndRoot>>>,
}

impl VolumesDirectory {
    /// Fills the VolumesDirectory with all volumes in |root_volume|.  No volume is opened during
    /// this.
    pub async fn new(root_volume: RootVolume) -> Result<Self, Error> {
        let layer_set = root_volume.volume_directory().store().tree().layer_set();
        let mut merger = layer_set.merger();
        let mut iter = root_volume.volume_directory().iter(&mut merger).await?;
        let directory_node = vfs::directory::immutable::simple();
        while let Some((name, store_object_id, object_descriptor)) = iter.get() {
            ensure!(*object_descriptor == ObjectDescriptor::Volume, FxfsError::Inconsistent);
            directory_node.add_entry(name, Arc::new(VolumesDirectoryEntry(store_object_id)))?;
            iter.advance().await?;
        }
        Ok(Self {
            root_volume,
            directory_node,
            mounted_volumes: Arc::new(Mutex::new(HashMap::new())),
        })
    }

    /// Returns the directory node which can be used to provide connections for e.g. enumerating
    /// entries in the VolumesDirectory.
    /// Directly manipulating the entries in this node will result in strange behaviour.
    #[cfg(test)]
    pub fn directory_node(&self) -> Arc<vfs::directory::immutable::Simple> {
        self.directory_node.clone()
    }

    /// Opens or creates a volume.  If |crypt| is set, the volume will be encrypted.
    /// If |create_only| is set, and the volume already exists, the call will return an error.
    pub async fn open_or_create_volume(
        &self,
        name: &str,
        crypt: Option<Arc<dyn Crypt>>,
        create_only: bool,
    ) -> Result<FxVolumeAndRoot, Status> {
        if self.mounted_volumes.lock().unwrap().contains_key(name) {
            return Err(if create_only { Status::ALREADY_EXISTS } else { Status::ALREADY_BOUND });
        }
        let store = self.root_volume.volume_directory().store();
        let fs = store.filesystem();
        let _write_guard = fs
            .write_lock(&[LockKey::object(
                store.store_object_id(),
                self.root_volume.volume_directory().object_id(),
            )])
            .await;
        let (store, created) = match self.root_volume.open_or_create_volume(name, crypt).await {
            Ok(OpenOrCreateResult::Opened(store)) => {
                // Have to check this again to avoid races.  The above check was just a fail-fast.
                if create_only {
                    return Err(Status::ALREADY_EXISTS);
                }
                (store, false)
            }
            Ok(OpenOrCreateResult::Created(store)) => (store, true),
            Err(e) => return Err(map_to_status(e)),
        };
        let store_id = store.store_object_id();
        store.record_metrics(name);
        let unique_id = zx::Event::create().expect("Failed to create event");
        let volume = FxVolumeAndRoot::new(store, unique_id.get_koid().unwrap().raw_koid())
            .await
            .map_err(map_to_status)?;
        volume.volume().start_flush_task(DEFAULT_FLUSH_PERIOD);
        self.mounted_volumes.lock().unwrap().insert(name.to_owned(), volume.clone());
        if created {
            self.directory_node.add_entry(name, Arc::new(VolumesDirectoryEntry(store_id))).unwrap();
        }
        Ok(volume)
    }

    /// Deletes a volume. The volume must exist but encrypted volume keys are not required.
    #[allow(unused)]
    pub async fn delete_volume(&self, name: &str) -> Result<(), Status> {
        // Cowardly refuse to delete a mounted volume.
        if self.mounted_volumes.lock().unwrap().contains_key(name) {
            return Err(Status::ALREADY_BOUND);
        }
        self.root_volume.delete_volume(name).await.map_err(map_to_status)
    }

    /// Terminates all opened volumes.
    pub async fn terminate(&self) {
        let volumes = std::mem::take(&mut *self.mounted_volumes.lock().unwrap());
        for (_, volume) in volumes {
            volume.volume().terminate().await;
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        crate::{
            crypt::{insecure::InsecureCrypt, Crypt},
            filesystem::FxFilesystem,
            object_store::volume::root_volume,
            platform::fuchsia::volumes_directory::VolumesDirectory,
        },
        fidl::endpoints::ServerEnd,
        fidl_fuchsia_io as fio, fuchsia_async as fasync,
        fuchsia_zircon::Status,
        std::sync::Arc,
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
                .open_or_create_volume("encrypted", Some(crypt.clone()), true)
                .await
                .expect("create encrypted volume failed");
            vol.volume().store().store_object_id()
        };

        volumes_directory.terminate().await;
        std::mem::drop(volumes_directory);
        filesystem.close().await.expect("close filesystem failed");
        let device = filesystem.take_device().await;
        device.reopen();
        let filesystem = FxFilesystem::open(device).await.unwrap();
        let volumes_directory =
            VolumesDirectory::new(root_volume(&filesystem).await.unwrap()).await.unwrap();

        let status = volumes_directory
            .open_or_create_volume("encrypted", Some(crypt.clone()), true)
            .await
            .err()
            .expect("Creating existing encrypted volume should fail");
        assert_eq!(status, Status::ALREADY_EXISTS);
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
                .open_or_create_volume("encrypted", Some(crypt.clone()), true)
                .await
                .expect("create encrypted volume failed");
            vol.volume().store().store_object_id()
        };

        volumes_directory.terminate().await;
        std::mem::drop(volumes_directory);
        filesystem.close().await.expect("close filesystem failed");
        let device = filesystem.take_device().await;
        device.reopen();
        let filesystem = FxFilesystem::open(device).await.unwrap();
        let volumes_directory =
            VolumesDirectory::new(root_volume(&filesystem).await.unwrap()).await.unwrap();

        {
            let vol = volumes_directory
                .open_or_create_volume("encrypted", Some(crypt.clone()), false)
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
                .open_or_create_volume("unencrypted", None, true)
                .await
                .expect("create unencrypted volume failed");
            vol.volume().store().store_object_id()
        };

        volumes_directory.terminate().await;
        std::mem::drop(volumes_directory);
        filesystem.close().await.expect("close filesystem failed");
        let device = filesystem.take_device().await;
        device.reopen();
        let filesystem = FxFilesystem::open(device).await.unwrap();
        let volumes_directory =
            VolumesDirectory::new(root_volume(&filesystem).await.unwrap()).await.unwrap();

        let status = volumes_directory
            .open_or_create_volume("unencrypted", None, true)
            .await
            .err()
            .expect("Creating existing unencrypted volume should fail");
        assert_eq!(status, Status::ALREADY_EXISTS);

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
                .open_or_create_volume("unencrypted", None, true)
                .await
                .expect("create unencrypted volume failed");
            vol.volume().store().store_object_id()
        };

        volumes_directory.terminate().await;
        std::mem::drop(volumes_directory);
        filesystem.close().await.expect("close filesystem failed");
        let device = filesystem.take_device().await;
        device.reopen();
        let filesystem = FxFilesystem::open(device).await.unwrap();
        let volumes_directory =
            VolumesDirectory::new(root_volume(&filesystem).await.unwrap()).await.unwrap();

        {
            let vol = volumes_directory
                .open_or_create_volume("unencrypted", None, false)
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
                .open_or_create_volume("encrypted", Some(crypt.clone()), true)
                .await
                .expect("create encrypted volume failed");
        };
        // And an unencrypted volume.
        {
            volumes_directory
                .open_or_create_volume("unencrypted", None, true)
                .await
                .expect("create unencrypted volume failed");
        };

        // Restart, so that we can test enumeration of unopened volumes.
        volumes_directory.terminate().await;
        std::mem::drop(volumes_directory);
        filesystem.close().await.expect("close filesystem failed");
        let device = filesystem.take_device().await;
        device.reopen();
        let filesystem = FxFilesystem::open(device).await.unwrap();
        let volumes_directory =
            VolumesDirectory::new(root_volume(&filesystem).await.unwrap()).await.unwrap();

        let readdir = |dir: Arc<fio::DirectoryProxy>| async move {
            let status = dir.rewind().await.expect("FIDL call failed");
            Status::ok(status).expect("rewind failed");
            let (status, buf) = dir.read_dirents(fio::MAX_BUF).await.expect("FIDL call failed");
            Status::ok(status).expect("read_dirents failed");
            let mut entries = vec![];
            for res in files_async::parse_dir_entries(&buf) {
                entries.push(res.expect("Failed to parse entry").name);
            }
            entries
        };

        let (dir_proxy, dir_server_end) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>()
            .expect("Create proxy to succeed");
        let dir_proxy = Arc::new(dir_proxy);

        volumes_directory.directory_node().open(
            ExecutionScope::new(),
            fio::OpenFlags::DIRECTORY | fio::OpenFlags::RIGHT_READABLE,
            fio::MODE_TYPE_DIRECTORY,
            Path::dot(),
            ServerEnd::new(dir_server_end.into_channel()),
        );

        let entries = readdir(dir_proxy.clone()).await;
        assert_eq!(entries, [".", "encrypted", "unencrypted"]);

        let _vol = volumes_directory
            .open_or_create_volume("encrypted", Some(crypt.clone()), false)
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
            .open_or_create_volume(VOLUME_NAME, Some(crypt.clone()), false)
            .await
            .expect("create encrypted volume failed");
        // We have the volume mounted so delete attempts should fail.
        assert_eq!(
            volumes_directory
                .delete_volume(VOLUME_NAME)
                .await
                .err()
                .expect("Deleting volume should fail"),
            Status::ALREADY_BOUND
        );
        volumes_directory.terminate().await;
        std::mem::drop(volumes_directory);
        filesystem.close().await.expect("close filesystem failed");
    }
}
