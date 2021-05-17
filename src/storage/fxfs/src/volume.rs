// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        errors::FxfsError,
        object_store::{
            directory::Directory,
            filesystem::FxFilesystem,
            transaction::{LockKey, TransactionHandler},
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

const VOLUMES_DIRECTORY: &str = "volumes";

/// RootVolume is the top-level volume which stores references to all of the other Volumes.
pub struct RootVolume {
    _root_directory: Directory<ObjectStore>,
    volume_directory: Directory<ObjectStore>,
    filesystem: Arc<FxFilesystem>,
}

impl RootVolume {
    pub fn volume_directory(&self) -> &Directory<ObjectStore> {
        &self.volume_directory
    }

    /// Creates a new volume.  This is not thread-safe.
    pub async fn new_volume(&self, volume_name: &str) -> Result<Arc<ObjectStore>, Error> {
        let root_store = self.filesystem.root_store();
        let store;
        let mut transaction = self.filesystem.clone().new_transaction(&[]).await?;
        store = root_store.create_child_store(&mut transaction).await?;

        let root_directory = Directory::create(&mut transaction, &store).await?;
        store.set_root_directory_object_id(&mut transaction, root_directory.object_id());

        self.volume_directory
            .add_child_volume(&mut transaction, volume_name, store.store_object_id())
            .await?;
        transaction.commit().await;

        Ok(store)
    }

    /// Returns the volume with the given name.  This is not thread-safe.
    pub async fn volume(&self, volume_name: &str) -> Result<Arc<ObjectStore>, Error> {
        let object_id =
            match self.volume_directory.lookup(volume_name).await?.ok_or(FxfsError::NotFound)? {
                (object_id, ObjectDescriptor::Volume) => object_id,
                _ => bail!(FxfsError::Inconsistent),
            };
        Ok(if let Some(volume_store) = self.filesystem.store(object_id) {
            volume_store
        } else {
            self.filesystem.root_store().open_store(object_id).await?
        })
    }

    pub async fn open_or_create_volume(
        &self,
        volume_name: &str,
    ) -> Result<Arc<ObjectStore>, Error> {
        match self.volume(volume_name).await {
            Ok(volume) => Ok(volume),
            Err(e) => {
                let cause = e.root_cause().downcast_ref::<FxfsError>().cloned();
                if let Some(FxfsError::NotFound) = cause {
                    self.new_volume(volume_name).await
                } else {
                    Err(e)
                }
            }
        }
    }
}

/// Returns the root volume for the filesystem or creates it if it does not exist.
pub async fn root_volume(fs: &Arc<FxFilesystem>) -> Result<RootVolume, Error> {
    let root_store = fs.root_store();

    let root_directory = Directory::open(&root_store, root_store.root_directory_object_id())
        .await
        .context("Unable to open root volume directory")?;

    let mut transaction = None;
    let volume_directory = loop {
        match root_directory.lookup(VOLUMES_DIRECTORY).await? {
            None => {
                if let Some(mut transaction) = transaction {
                    let directory = root_directory
                        .create_child_dir(&mut transaction, VOLUMES_DIRECTORY)
                        .await?;
                    transaction.commit().await;
                    break directory;
                } else {
                    transaction = Some(
                        fs.clone()
                            .new_transaction(&[LockKey::object(
                                root_store.store_object_id(),
                                root_directory.object_id(),
                            )])
                            .await?,
                    );
                }
            }
            Some((object_id, ObjectDescriptor::Directory)) => {
                break Directory::open(&root_store, object_id)
                    .await
                    .context("unable to open volumes directory")?;
            }
            _ => {
                return Err(anyhow!(FxfsError::Inconsistent)
                    .context("Unexpected type for volumes directory"));
            }
        }
    };

    Ok(RootVolume { _root_directory: root_directory, volume_directory, filesystem: fs.clone() })
}

#[cfg(test)]
mod tests {
    use {
        super::root_volume,
        crate::object_store::{
            directory::Directory,
            filesystem::{Filesystem, FxFilesystem, SyncOptions},
            transaction::TransactionHandler,
        },
        anyhow::Error,
        fuchsia_async as fasync,
        storage_device::{fake_device::FakeDevice, DeviceHolder},
    };

    #[fasync::run_singlethreaded(test)]
    async fn test_lookup_nonexistent_volume() -> Result<(), Error> {
        let device = DeviceHolder::new(FakeDevice::new(2048, 512));
        let filesystem = FxFilesystem::new_empty(device).await?;
        let root_volume = root_volume(&filesystem).await.expect("root_volume failed");
        root_volume.volume("vol").await.err().expect("Volume shouldn't exist");
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_add_volume() {
        let device = DeviceHolder::new(FakeDevice::new(2048, 512));
        let filesystem = FxFilesystem::new_empty(device).await.expect("new_empty failed");
        {
            let root_volume = root_volume(&filesystem).await.expect("root_volume failed");
            let store = root_volume.new_volume("vol").await.expect("new_volume failed");
            let mut transaction =
                filesystem.clone().new_transaction(&[]).await.expect("new transaction failed");
            let root_directory = Directory::open(&store, store.root_directory_object_id())
                .await
                .expect("open failed");
            root_directory
                .create_child_file(&mut transaction, "foo")
                .await
                .expect("create_child_file failed");
            transaction.commit().await;
            filesystem.sync(SyncOptions::default()).await.expect("sync failed");
        };
        {
            let filesystem =
                FxFilesystem::open(filesystem.take_device().await).await.expect("open failed");
            let root_volume = root_volume(&filesystem).await.expect("root_volume failed");
            let volume = root_volume.volume("vol").await.expect("volume failed");
            let root_directory = Directory::open(&volume, volume.root_directory_object_id())
                .await
                .expect("open failed");
            root_directory.lookup("foo").await.expect("lookup failed").expect("not found");
        };
    }
}
