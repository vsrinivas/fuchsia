// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        errors::FxfsError,
        object_handle::{ObjectHandle, ObjectHandleExt},
        object_store::{
            directory::Directory, filesystem::FxFilesystem, transaction::LockKey,
            transaction::TransactionHandler, HandleOptions, ObjectDescriptor, ObjectStore,
            INVALID_OBJECT_ID,
        },
    },
    anyhow::{anyhow, Context, Error},
    bincode::{deserialize_from, serialize_into},
    serde::{Deserialize, Serialize},
    std::sync::Arc,
};

// Volumes are a grouping of an object store and a root directory within this object store. They
// model a hierarchical tree of objects within a single store.
//
// Typically there will be one root volume which is referenced directly by the superblock. This root
// volume stores references to all other volumes on the system (as volumes/foo, volumes/bar, ...).
// For now, this hierarchy is only one deep.

const MAX_VOLUME_INFO_SERIALIZED_SIZE: usize = 131072;

const VOLUMES_DIRECTORY: &str = "volumes";

#[derive(Clone, Default, Serialize, Deserialize)]
struct VolumeInfo {
    root_directory_object_id: u64,
    graveyard_object_id: u64,
}

/// RootVolume is the top-level volume which stores references to all of the other Volumes.
/// Each child volume is a direct child of the RootVolume's root directory.
/// The object ID for the directory is stored within the super-block.
pub struct RootVolume {
    _root_directory: Directory,
    volume_directory: Directory,
    _graveyard: Directory,
    filesystem: Arc<FxFilesystem>,
}

impl RootVolume {
    /// Creates a new volume.
    pub async fn new_volume(&self, volume_name: &str) -> Result<Volume, Error> {
        let root_store = self.filesystem.root_store();
        let volume_info_handle;
        let mut transaction = self.filesystem.clone().new_transaction(&[]).await?;
        let store = root_store.create_child_store(&mut transaction).await?;

        let root_directory = store.create_directory(&mut transaction).await?;
        let graveyard = store.create_directory(&mut transaction).await?;

        volume_info_handle =
            store.create_object(&mut transaction, HandleOptions::default()).await?;
        let info = VolumeInfo {
            root_directory_object_id: root_directory.object_id(),
            graveyard_object_id: graveyard.object_id(),
        };
        // TODO(jfsulliv): Can we find out how big the object will be in advance and serialize
        // directly into bufer?
        let mut serialized_info = Vec::new();
        serialize_into(&mut serialized_info, &info)?;
        let mut buf = volume_info_handle.allocate_buffer(serialized_info.len());
        buf.as_mut_slice()[..serialized_info.len()].copy_from_slice(&serialized_info[..]);
        volume_info_handle.txn_write(&mut transaction, 0u64, buf.as_ref()).await?;
        self.volume_directory.add_child_volume(
            &mut transaction,
            volume_name,
            store.store_object_id(),
            volume_info_handle.object_id(),
        );
        transaction.commit().await;

        Ok(Volume::new(store, root_directory, graveyard))
    }

    /// Returns the volume with the given name.
    pub async fn volume(&self, volume_name: &str) -> Result<Volume, Error> {
        let (object_id, object_type) = self.volume_directory.lookup(volume_name).await?;
        let volume_info_object_id = {
            if let ObjectDescriptor::Volume(object_id) = object_type {
                object_id
            } else {
                return Err(anyhow!(FxfsError::Inconsistent).context(format!(
                    "Unexpected type {:?} for volume {}",
                    object_type, volume_name
                )));
            }
        };
        let volume_store = {
            if let Some(volume_store) = self.filesystem.store(object_id) {
                volume_store
            } else {
                self.filesystem.root_store().open_store(object_id).await?
            }
        };
        let handle =
            volume_store.open_object(volume_info_object_id, HandleOptions::default()).await?;
        let serialized_info = handle.contents(MAX_VOLUME_INFO_SERIALIZED_SIZE).await?;
        let info: VolumeInfo = deserialize_from(&serialized_info[..])?;
        let root_dir = volume_store.open_directory(info.root_directory_object_id).await?;
        let graveyard = volume_store.open_directory(info.graveyard_object_id).await?;

        Ok(Volume::new(volume_store, root_dir, graveyard))
    }

    pub async fn open_or_create_volume(&self, volume_name: &str) -> Result<Volume, Error> {
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
    let root_volume_info_object_id = loop {
        let root_volume_object_id = fs.root_volume_info_object_id();
        if root_volume_object_id != INVALID_OBJECT_ID {
            break root_volume_object_id;
        }
        // The root volume is absent, so create it.
        let handle;
        let mut transaction = fs.clone().new_transaction(&[LockKey::RootVolume]).await?;
        let root_volume_object_id = fs.root_volume_info_object_id();
        // Check again in case we lost a race since the write lock was acquired.
        if root_volume_object_id != INVALID_OBJECT_ID {
            break root_volume_object_id;
        }

        handle = root_store.create_object(&mut transaction, HandleOptions::default()).await?;
        let root_directory = root_store.create_directory(&mut transaction).await?;
        let volume_directory =
            root_directory.create_child_dir(&mut transaction, VOLUMES_DIRECTORY).await?;
        let graveyard = root_store.create_directory(&mut transaction).await?;

        let mut serialized_volume_info = Vec::new();
        let info = VolumeInfo {
            root_directory_object_id: root_directory.object_id(),
            graveyard_object_id: graveyard.object_id(),
        };
        serialize_into(&mut serialized_volume_info, &info)?;

        // TODO(jfsulliv): Can we find out how big the object will be in advance and serialize
        // directly into bufer?
        let mut buf = handle.allocate_buffer(serialized_volume_info.len());
        buf.as_mut_slice().copy_from_slice(&serialized_volume_info[..]);
        handle.txn_write(&mut transaction, 0u64, buf.as_ref()).await?;
        transaction.commit().await;
        fs.set_root_volume_info_object_id(handle.object_id());
        return Ok(RootVolume {
            _root_directory: root_directory,
            volume_directory,
            _graveyard: graveyard,
            filesystem: fs.clone(),
        });
    };

    let handle = root_store
        .open_object(root_volume_info_object_id, HandleOptions::default())
        .await
        .expect("Unable to open root volume info");
    let info: VolumeInfo = deserialize_from(
        &*handle
            .contents(MAX_VOLUME_INFO_SERIALIZED_SIZE)
            .await
            .context("unable to read root volume info")?,
    )
    .context("unable to deserialize root volume info")?;

    let root_directory = root_store
        .open_directory(info.root_directory_object_id)
        .await
        .context("Unable to open root volume directory")?;
    let volume_directory_oid = root_directory
        .lookup(VOLUMES_DIRECTORY)
        .await
        .and_then(|(object_id, object_descriptor)| {
            if let ObjectDescriptor::Directory = object_descriptor {
                Ok(object_id)
            } else {
                Err(anyhow!(FxfsError::Inconsistent)
                    .context("Unexpected type for volumes directory"))
            }
        })
        .context("unable to resolve volumes directory")?;
    let volume_directory = root_store
        .open_directory(volume_directory_oid)
        .await
        .context("unable to open volumes directory")?;
    let graveyard = root_store
        .open_directory(info.graveyard_object_id)
        .await
        .context("Unable to open root volume graveyard")?;
    Ok(RootVolume {
        _root_directory: root_directory,
        volume_directory,
        _graveyard: graveyard,
        filesystem: fs.clone(),
    })
}

/// Volumes are a grouping of an object store and a root directory within this object store. They
/// model a hierarchical tree of objects within a single store.
pub struct Volume {
    pub object_store: Arc<ObjectStore>,
    root_directory: Directory,
    graveyard: Directory,
}

impl Volume {
    pub(super) fn new(
        object_store: Arc<ObjectStore>,
        root_directory: Directory,
        graveyard: Directory,
    ) -> Self {
        Self { object_store, root_directory, graveyard }
    }

    pub fn root_directory(&self) -> &Directory {
        &self.root_directory
    }

    pub fn graveyard(&self) -> &Directory {
        &self.graveyard
    }
}

// Convert from Volume to a tuple.
// TODO(jfsulliv): This isn't the most graceful interface. Consider implementing From<Volume> for FxVolume instead.
impl From<Volume> for (Arc<ObjectStore>, Directory, Directory) {
    fn from(volume: Volume) -> Self {
        (volume.object_store, volume.root_directory, volume.graveyard)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::root_volume,
        crate::{
            object_store::{
                filesystem::{FxFilesystem, SyncOptions},
                transaction::TransactionHandler,
            },
            testing::fake_device::FakeDevice,
        },
        anyhow::Error,
        fuchsia_async as fasync,
        std::sync::Arc,
    };

    #[fasync::run_singlethreaded(test)]
    async fn test_lookup_nonexistent_volume() -> Result<(), Error> {
        let device = Arc::new(FakeDevice::new(2048, 512));
        let filesystem = FxFilesystem::new_empty(device.clone()).await?;
        let root_volume = root_volume(&filesystem).await.expect("root_volume failed");
        root_volume.volume("vol").await.err().expect("Volume shouldn't exist");
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_add_volume() {
        let device = Arc::new(FakeDevice::new(2048, 512));
        {
            let filesystem =
                FxFilesystem::new_empty(device.clone()).await.expect("new_empty failed");
            let root_volume = root_volume(&filesystem).await.expect("root_volume failed");
            let volume = root_volume.new_volume("vol").await.expect("new_volume failed");
            let mut transaction =
                filesystem.clone().new_transaction(&[]).await.expect("new transaction failed");
            volume
                .root_directory()
                .create_child_file(&mut transaction, "foo")
                .await
                .expect("create_child_file failed");
            transaction.commit().await;
            filesystem.sync(SyncOptions::default()).await.expect("sync failed");
        };
        {
            let filesystem = FxFilesystem::open(device.clone()).await.expect("open failed");
            let root_volume = root_volume(&filesystem).await.expect("root_volume failed");
            let volume = root_volume.volume("vol").await.expect("volume failed");
            volume.root_directory().lookup("foo").await.expect("lookup failed");
        };
    }
}
