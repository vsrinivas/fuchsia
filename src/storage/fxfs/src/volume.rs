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
    anyhow::{bail, Context, Error},
    bincode::{deserialize_from, serialize_into},
    serde::{Deserialize, Serialize},
    std::sync::Arc,
};

const MAX_VOLUME_INFO_SERIALIZED_SIZE: usize = 131072;

/// VolumeDirectory is a directory that lives in the root object store that contains child volumes.
/// The object ID for the directory is stored within the super-block.
pub struct VolumeDirectory {
    directory: Directory,
    filesystem: Arc<FxFilesystem>,
}

impl VolumeDirectory {
    /// Creates a new volume.
    pub async fn new_volume(&self, volume_name: &str) -> Result<Volume, Error> {
        let root_store = self.filesystem.root_store();
        let volume_info_handle;
        let mut transaction = self.filesystem.clone().new_transaction(&[]).await?;
        let store = root_store.create_child_store(&mut transaction).await?;

        let dir_handle = store.create_directory(&mut transaction).await?;

        volume_info_handle =
            store.create_object(&mut transaction, HandleOptions::default()).await?;
        let info = VolumeInfo { root_directory_object_id: dir_handle.object_id() };
        // TODO(jfsulliv): Can we find out how big the object will be in advance and serialize
        // directly into bufer?
        let mut serialized_info = Vec::new();
        serialize_into(&mut serialized_info, &info)?;
        let mut buf = volume_info_handle.allocate_buffer(serialized_info.len());
        buf.as_mut_slice()[..serialized_info.len()].copy_from_slice(&serialized_info[..]);
        volume_info_handle.txn_write(&mut transaction, 0u64, buf.as_ref()).await?;
        self.directory.add_child_volume(
            &mut transaction,
            volume_name,
            store.store_object_id(),
            volume_info_handle.object_id(),
        );
        transaction.commit().await;

        Ok(Volume::new(store, dir_handle))
    }

    /// Returns the volume with the given name.
    pub async fn volume(&self, volume_name: &str) -> Result<Volume, Error> {
        let (object_id, object_type) = self.directory.lookup(volume_name).await?;
        let volume_info_object_id = {
            if let ObjectDescriptor::Volume(object_id) = object_type {
                object_id
            } else {
                bail!(FxfsError::Inconsistent);
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
        let dir = volume_store.open_directory(info.root_directory_object_id).await?;

        Ok(Volume::new(volume_store, dir))
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

/// Returns the volume directory for the filesystem or creates it if it does not exist.
pub async fn volume_directory(fs: &Arc<FxFilesystem>) -> Result<VolumeDirectory, Error> {
    let root_store = fs.root_store();
    let volume_info_object_id = loop {
        let volume_info_object_id = fs.volume_info_object_id();
        if volume_info_object_id != INVALID_OBJECT_ID {
            break volume_info_object_id;
        }
        // Create the volume directory, a directory that lists all child volumes.
        let handle;
        let mut transaction = fs.clone().new_transaction(&[LockKey::VolumeDirectory]).await?;
        let volume_info_object_id = fs.volume_info_object_id();
        // Check again in case we lost a race.
        if volume_info_object_id != INVALID_OBJECT_ID {
            break volume_info_object_id;
        }
        handle = root_store.create_object(&mut transaction, HandleOptions::default()).await?;
        let mut serialized_volume_info = Vec::new();
        let directory = root_store.create_directory(&mut transaction).await?;
        let info = VolumeInfo { root_directory_object_id: directory.object_id() };
        serialize_into(&mut serialized_volume_info, &info)?;
        // TODO(jfsulliv): Can we find out how big the object will be in advance and serialize
        // directly into bufer?
        let mut buf = handle.allocate_buffer(serialized_volume_info.len());
        buf.as_mut_slice().copy_from_slice(&serialized_volume_info[..]);
        handle.txn_write(&mut transaction, 0u64, buf.as_ref()).await?;
        transaction.commit().await;
        fs.set_volume_info_object_id(handle.object_id());
        return Ok(VolumeDirectory { directory, filesystem: fs.clone() });
    };
    let handle = root_store.open_object(volume_info_object_id, HandleOptions::default()).await?;
    let info: VolumeInfo = deserialize_from(
        &*handle
            .contents(MAX_VOLUME_INFO_SERIALIZED_SIZE)
            .await
            .context("unable to read volume info")?,
    )
    .context("unable to deserialize volume info")?;
    Ok(VolumeDirectory {
        directory: root_store
            .open_directory(info.root_directory_object_id)
            .await
            .context("unable to open volume directory")?,
        filesystem: fs.clone(),
    })
}

#[derive(Clone, Default, Serialize, Deserialize)]
struct VolumeInfo {
    root_directory_object_id: u64,
}

pub struct Volume {
    object_store: Arc<ObjectStore>,
    root_directory: Directory,
}

impl Volume {
    pub(crate) fn new(object_store: Arc<ObjectStore>, root_directory: Directory) -> Self {
        Self { object_store, root_directory }
    }

    pub fn root_directory(&self) -> &Directory {
        &self.root_directory
    }
}

// Convert from Volume to a tuple.
impl From<Volume> for (Arc<ObjectStore>, Directory) {
    fn from(volume: Volume) -> Self {
        (volume.object_store, volume.root_directory)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::volume_directory,
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
        let volume_directory =
            volume_directory(&filesystem).await.expect("volume_directory failed");
        volume_directory.volume("vol").await.err().expect("Volume shouldn't exist");
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_add_volume() {
        let device = Arc::new(FakeDevice::new(2048, 512));
        {
            let filesystem =
                FxFilesystem::new_empty(device.clone()).await.expect("new_empty failed");
            let volume_directory =
                volume_directory(&filesystem).await.expect("volume_directory failed");
            let volume = volume_directory.new_volume("vol").await.expect("new_volume failed");
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
            let volume_directory =
                volume_directory(&filesystem).await.expect("volume_directory failed");
            let volume = volume_directory.volume("vol").await.expect("volume failed");
            volume.root_directory().lookup("foo").await.expect("lookup failed");
        };
    }
}
