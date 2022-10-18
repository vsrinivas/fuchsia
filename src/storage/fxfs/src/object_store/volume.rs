// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        crypt::Crypt,
        errors::FxfsError,
        filesystem::Filesystem,
        object_store::{
            allocator::Allocator, directory::Directory, transaction::Options, NewChildStoreOptions,
            ObjectDescriptor, ObjectStore,
        },
    },
    anyhow::{anyhow, bail, Context, Error},
    std::sync::Arc,
};

// Volumes are a grouping of an object store and a root directory within this object store. They
// model a hierarchical tree of objects within a single store.
//
// Typically there will be one root volume which is referenced directly by the superblock. This root
// volume stores references to all other volumes on the system (as volumes/foo, volumes/bar, ...).
// For now, this hierarchy is only one deep.

pub const VOLUMES_DIRECTORY: &str = "volumes";

/// RootVolume is the top-level volume which stores references to all of the other Volumes.
pub struct RootVolume {
    _root_directory: Directory<ObjectStore>,
    filesystem: Arc<dyn Filesystem>,
}

impl RootVolume {
    pub fn volume_directory(&self) -> &Directory<ObjectStore> {
        self.filesystem.object_manager().volume_directory()
    }

    /// Creates a new volume.  This is not thread-safe and will not check to see if the volume
    /// already exists (and if it does, it will result in an inconsistent volume).
    pub async fn new_volume(
        &self,
        volume_name: &str,
        crypt: Option<Arc<dyn Crypt>>,
    ) -> Result<Arc<ObjectStore>, Error> {
        let root_store = self.filesystem.root_store();
        let store;
        let mut transaction =
            self.filesystem.clone().new_transaction(&[], Options::default()).await?;

        store = root_store
            .new_child_store(&mut transaction, NewChildStoreOptions { crypt, ..Default::default() })
            .await?;
        store.set_trace(self.filesystem.trace());

        // We must register the store here because create will add mutations for the store.
        self.filesystem.object_manager().add_store(store.clone());

        // If the transaction fails, we must unregister the store.
        struct CleanUp<'a>(&'a ObjectStore);
        impl Drop for CleanUp<'_> {
            fn drop(&mut self) {
                self.0.filesystem().object_manager().forget_store(self.0.store_object_id());
            }
        }
        let clean_up = CleanUp(&store);

        // Actually create the store in the transaction.
        store.create(&mut transaction).await?;

        self.volume_directory()
            .add_child_volume(&mut transaction, volume_name, store.store_object_id())
            .await?;
        transaction.commit().await?;

        std::mem::forget(clean_up);

        Ok(store)
    }

    /// Returns the volume with the given name.  This is not thread-safe.
    pub async fn volume(
        &self,
        volume_name: &str,
        crypt: Option<Arc<dyn Crypt>>,
    ) -> Result<Arc<ObjectStore>, Error> {
        self.volume_from_id(
            match self.volume_directory().lookup(volume_name).await?.ok_or(FxfsError::NotFound)? {
                (object_id, ObjectDescriptor::Volume) => object_id,
                _ => bail!(anyhow!(FxfsError::Inconsistent).context("Expected volume")),
            },
            crypt,
        )
        .await
    }

    /// Returns the volume with the given id.  This is not thread-safe.
    pub async fn volume_from_id(
        &self,
        store_object_id: u64,
        crypt: Option<Arc<dyn Crypt>>,
    ) -> Result<Arc<ObjectStore>, Error> {
        let store =
            self.filesystem.object_manager().store(store_object_id).ok_or(FxfsError::NotFound)?;
        store.set_trace(self.filesystem.trace());
        if let Some(crypt) = crypt {
            store.unlock(crypt).await?;
        } else if store.is_locked() {
            bail!(FxfsError::AccessDenied);
        }
        Ok(store)
    }

    pub async fn delete_volume(&self, volume_name: &str) -> Result<(), Error> {
        let object_id =
            match self.volume_directory().lookup(volume_name).await?.ok_or(FxfsError::NotFound)? {
                (object_id, ObjectDescriptor::Volume) => object_id,
                _ => bail!(anyhow!(FxfsError::Inconsistent).context("Expected volume")),
            };
        let root_store = self.filesystem.root_store();
        let store = self.filesystem.object_manager().store(object_id).ok_or(FxfsError::NotFound)?;

        // Delete all the layers and encrypted mutations stored in root_store for this volume.
        // This includes the StoreInfo itself.
        let mut objects_to_delete = store.parent_objects();
        objects_to_delete.push(store.store_object_id());

        let mut transaction = self
            .filesystem
            .clone()
            .new_transaction(&[], Options { borrow_metadata_space: true, ..Default::default() })
            .await?;
        for object_id in &objects_to_delete {
            root_store.adjust_refs(&mut transaction, *object_id, -1).await?;
        }
        // Mark all volume data as deleted.
        self.filesystem
            .allocator()
            .mark_for_deletion(&mut transaction, store.store_object_id())
            .await;
        // Remove the volume entry from the VolumeDirectory.
        self.volume_directory()
            .delete_child_volume(&mut transaction, volume_name, store.store_object_id())
            .await?;
        transaction.commit().await.context("commit")?;
        // Tombstone the deleted objects.
        for object_id in &objects_to_delete {
            root_store.tombstone(*object_id, Options::default()).await?;
        }
        Ok(())
    }
}

/// Returns the root volume for the filesystem.
pub async fn root_volume(filesystem: Arc<dyn Filesystem>) -> Result<RootVolume, Error> {
    let root_store = filesystem.root_store();
    let root_directory = Directory::open(&root_store, root_store.root_directory_object_id())
        .await
        .context("Unable to open root volume directory")?;
    Ok(RootVolume { _root_directory: root_directory, filesystem })
}

/// Returns the object IDs for all volumes.
pub async fn list_volumes(volume_directory: &Directory<ObjectStore>) -> Result<Vec<u64>, Error> {
    let layer_set = volume_directory.store().tree().layer_set();
    let mut merger = layer_set.merger();
    let mut iter = volume_directory.iter(&mut merger).await?;
    let mut object_ids = vec![];
    while let Some((_, id, _)) = iter.get() {
        object_ids.push(id);
        iter.advance().await?;
    }
    Ok(object_ids)
}

#[cfg(test)]
mod tests {
    use {
        super::root_volume,
        crate::{
            crypt::insecure::InsecureCrypt,
            filesystem::{Filesystem, FxFilesystem, JournalingObject, SyncOptions},
            object_handle::{ObjectHandle, WriteObjectHandle},
            object_store::{
                allocator::Allocator,
                directory::Directory,
                transaction::{Options, TransactionHandler},
            },
        },
        fuchsia_async as fasync,
        std::sync::Arc,
        storage_device::{fake_device::FakeDevice, DeviceHolder},
    };

    #[fasync::run_singlethreaded(test)]
    async fn test_lookup_nonexistent_volume() {
        let device = DeviceHolder::new(FakeDevice::new(8192, 512));
        let filesystem = FxFilesystem::new_empty(device).await.expect("new_empty failed");
        let root_volume = root_volume(filesystem.clone()).await.expect("root_volume failed");
        root_volume
            .volume("vol", Some(Arc::new(InsecureCrypt::new())))
            .await
            .err()
            .expect("Volume shouldn't exist");
        filesystem.close().await.expect("Close failed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_add_volume() {
        let device = DeviceHolder::new(FakeDevice::new(16384, 512));
        let filesystem = FxFilesystem::new_empty(device).await.expect("new_empty failed");
        let crypt = Arc::new(InsecureCrypt::new());
        {
            let root_volume = root_volume(filesystem.clone()).await.expect("root_volume failed");
            let store = root_volume
                .new_volume("vol", Some(crypt.clone()))
                .await
                .expect("new_volume failed");
            let mut transaction = filesystem
                .clone()
                .new_transaction(&[], Options::default())
                .await
                .expect("new transaction failed");
            let root_directory = Directory::open(&store, store.root_directory_object_id())
                .await
                .expect("open failed");
            root_directory
                .create_child_file(&mut transaction, "foo")
                .await
                .expect("create_child_file failed");
            transaction.commit().await.expect("commit failed");
            filesystem.sync(SyncOptions::default()).await.expect("sync failed");
        };
        {
            filesystem.close().await.expect("Close failed");
            let device = filesystem.take_device().await;
            device.reopen(false);
            let filesystem = FxFilesystem::open(device).await.expect("open failed");
            let root_volume = root_volume(filesystem.clone()).await.expect("root_volume failed");
            let volume = root_volume.volume("vol", Some(crypt)).await.expect("volume failed");
            let root_directory = Directory::open(&volume, volume.root_directory_object_id())
                .await
                .expect("open failed");
            root_directory.lookup("foo").await.expect("lookup failed").expect("not found");
            filesystem.close().await.expect("Close failed");
        };
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_delete_volume() {
        let device = DeviceHolder::new(FakeDevice::new(16384, 512));
        let filesystem = FxFilesystem::new_empty(device).await.expect("new_empty failed");
        let crypt = Arc::new(InsecureCrypt::new());
        let store_object_id;
        let parent_objects;
        // Add volume and a file (some data).
        {
            let root_volume = root_volume(filesystem.clone()).await.expect("root_volume failed");
            let store = root_volume
                .new_volume("vol", Some(crypt.clone()))
                .await
                .expect("new_volume failed");
            store_object_id = store.store_object_id();
            let root_directory = Directory::open(&store, store.root_directory_object_id())
                .await
                .expect("open failed");
            let mut transaction = filesystem
                .clone()
                .new_transaction(&[], Options::default())
                .await
                .expect("new transaction failed");
            let handle = root_directory
                .create_child_file(&mut transaction, "foo")
                .await
                .expect("create_child_file failed");
            transaction.commit().await.expect("commit failed");

            let mut buf = handle.allocate_buffer(8192);
            buf.as_mut_slice().fill(0xaa);
            handle.write_or_append(Some(0), buf.as_ref()).await.expect("write failed");
            store.flush().await.expect("flush failed");
            filesystem.sync(SyncOptions::default()).await.expect("sync failed");
            parent_objects = store.parent_objects();
            // Confirm parent objects exist.
            for object_id in &parent_objects {
                let _ = filesystem
                    .root_store()
                    .get_file_size(*object_id)
                    .await
                    .expect("Layer file missing? Bug in test.");
            }
        }
        filesystem.close().await.expect("Close failed");
        let device = filesystem.take_device().await;
        device.reopen(false);
        let filesystem = FxFilesystem::open(device).await.expect("open failed");
        {
            // Expect 8kiB accounted to the new volume.
            assert_eq!(
                filesystem.allocator().get_owner_allocated_bytes().get(&store_object_id),
                Some(&8192)
            );
            let root_volume = root_volume(filesystem.clone()).await.expect("root_volume failed");
            root_volume.delete_volume("vol").await.expect("delete_volume");
            // Confirm data allocation is gone.
            assert_eq!(
                filesystem
                    .allocator()
                    .get_owner_allocated_bytes()
                    .get(&store_object_id)
                    .unwrap_or(&0i64),
                &0i64,
            );
            // Confirm volume entry is gone.
            root_volume
                .volume("vol", Some(crypt.clone()))
                .await
                .err()
                .expect("volume shouldn't exist anymore.");
        }
        filesystem.close().await.expect("Close failed");
        let device = filesystem.take_device().await;
        device.reopen(false);
        // All artifacts of the original volume should be gone.
        let filesystem = FxFilesystem::open(device).await.expect("open failed");
        for object_id in &parent_objects {
            let _ = filesystem
                .root_store()
                .get_file_size(*object_id)
                .await
                .err()
                .expect("File wasn't deleted.");
        }
        filesystem.close().await.expect("Close failed");
    }
}
