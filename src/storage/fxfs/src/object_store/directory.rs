// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        errors::FxfsError,
        object_handle::ObjectHandle,
        object_store::{
            record::{ObjectItem, ObjectKey, ObjectKind, ObjectValue},
            transaction::{Mutation, Transaction},
            HandleOptions, ObjectStore, StoreObjectHandle,
        },
    },
    anyhow::{bail, Error},
    std::sync::Arc,
};

// ObjectDescriptor is exposed in Directory::lookup.
pub use crate::object_store::record::ObjectDescriptor;

/// A directory stores name to child object mappings.
pub struct Directory {
    store: Arc<ObjectStore>,
    object_id: u64,
}

impl Directory {
    pub fn new(store: Arc<ObjectStore>, object_id: u64) -> Self {
        Directory { store, object_id }
    }

    pub fn object_store(&self) -> Arc<ObjectStore> {
        self.store.clone()
    }

    pub fn object_id(&self) -> u64 {
        return self.object_id;
    }

    /// Returns the object ID and descriptor for the given child, or FxfsError::NotFound if not
    /// found.
    pub async fn lookup(&self, name: &str) -> Result<(u64, ObjectDescriptor), Error> {
        let item = self
            .store
            .tree()
            .find(&ObjectKey::child(self.object_id, name))
            .await?
            .ok_or(FxfsError::NotFound)?;
        if let ObjectValue::Child { object_id, object_descriptor } = item.value {
            Ok((object_id, object_descriptor))
        } else {
            bail!(FxfsError::Inconsistent);
        }
    }

    pub async fn create_child_dir(
        &self,
        transaction: &mut Transaction<'_>,
        name: &str,
    ) -> Result<Directory, Error> {
        let handle = self.store.create_directory(transaction).await?;
        transaction.add(
            self.store.store_object_id(),
            Mutation::insert_object(
                ObjectKey::child(self.object_id, name),
                ObjectValue::child(handle.object_id(), ObjectDescriptor::Directory),
            ),
        );
        Ok(handle)
    }

    pub async fn create_child_file(
        &self,
        transaction: &mut Transaction<'_>,
        name: &str,
    ) -> Result<StoreObjectHandle, Error> {
        let handle = self.store.create_object(transaction, HandleOptions::default()).await?;
        transaction.add(
            self.store.store_object_id(),
            Mutation::insert_object(
                ObjectKey::child(self.object_id, name),
                ObjectValue::child(handle.object_id(), ObjectDescriptor::File),
            ),
        );
        Ok(handle)
    }

    pub fn add_child_volume(
        &self,
        transaction: &mut Transaction<'_>,
        volume_name: &str,
        store_object_id: u64,
        volume_info_object_id: u64,
    ) {
        transaction.add(
            self.store.store_object_id(),
            Mutation::insert_object(
                ObjectKey::child(self.object_id, volume_name),
                ObjectValue::child(
                    store_object_id,
                    ObjectDescriptor::Volume(volume_info_object_id),
                ),
            ),
        );
    }
}

impl ObjectStore {
    pub async fn create_directory(
        self: &Arc<Self>,
        transaction: &mut Transaction<'_>,
    ) -> Result<Directory, Error> {
        self.ensure_open().await?;
        let object_id = self.get_next_object_id();
        transaction.add(
            self.store_object_id,
            Mutation::insert_object(
                ObjectKey::object(object_id),
                ObjectValue::Object { kind: ObjectKind::Directory },
            ),
        );
        Ok(Directory::new(self.clone(), object_id))
    }

    pub async fn open_directory(self: &Arc<Self>, object_id: u64) -> Result<Directory, Error> {
        if let ObjectItem { value: ObjectValue::Object { kind: ObjectKind::Directory }, .. } =
            self.tree.find(&ObjectKey::object(object_id)).await?.ok_or(FxfsError::NotFound)?
        {
            Ok(Directory::new(self.clone(), object_id))
        } else {
            bail!(FxfsError::NotDir);
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        crate::{
            errors::FxfsError,
            object_store::{
                filesystem::{FxFilesystem, SyncOptions},
                transaction::TransactionHandler,
                HandleOptions, ObjectDescriptor,
            },
            testing::fake_device::FakeDevice,
        },
        fuchsia_async as fasync,
        std::sync::Arc,
    };

    const TEST_DEVICE_BLOCK_SIZE: u32 = 512;

    #[fasync::run_singlethreaded(test)]
    async fn test_create_directory() {
        let device = Arc::new(FakeDevice::new(2048, TEST_DEVICE_BLOCK_SIZE));
        let object_id = {
            let fs = FxFilesystem::new_empty(device.clone()).await.expect("new_empty failed");
            let mut transaction =
                fs.clone().new_transaction(&[]).await.expect("new_transaction failed");
            let dir = fs
                .root_store()
                .create_directory(&mut transaction)
                .await
                .expect("create_directory failed");

            let child_dir = dir
                .create_child_dir(&mut transaction, "foo")
                .await
                .expect("create_child_dir failed");
            let _child_dir_file = child_dir
                .create_child_file(&mut transaction, "bar")
                .await
                .expect("create_child_file failed");
            let _child_file = dir
                .create_child_file(&mut transaction, "baz")
                .await
                .expect("create_child_file failed");
            dir.add_child_volume(&mut transaction, "corge", 100, 101);
            transaction.commit().await;
            fs.sync(SyncOptions::default()).await.expect("sync failed");
            dir.object_id()
        };
        {
            let fs = FxFilesystem::open(device).await.expect("open failed");
            let dir =
                fs.root_store().open_directory(object_id).await.expect("open_directory failed");
            let (object_id, object_descriptor) = dir.lookup("foo").await.expect("lookup failed");
            assert_eq!(object_descriptor, ObjectDescriptor::Directory);
            let child_dir =
                fs.root_store().open_directory(object_id).await.expect("open_directory failed");
            let (object_id, object_descriptor) =
                child_dir.lookup("bar").await.expect("lookup failed");
            assert_eq!(object_descriptor, ObjectDescriptor::File);
            let _child_dir_file = fs
                .root_store()
                .open_object(object_id, HandleOptions::default())
                .await
                .expect("open object failed");
            let (object_id, object_descriptor) = dir.lookup("baz").await.expect("lookup failed");
            assert_eq!(object_descriptor, ObjectDescriptor::File);
            let _child_file = fs
                .root_store()
                .open_object(object_id, HandleOptions::default())
                .await
                .expect("open object failed");
            let (object_id, object_descriptor) = dir.lookup("corge").await.expect("lookup failed");
            assert_eq!(object_id, 100);
            if let ObjectDescriptor::Volume(volume_info_object_id) = object_descriptor {
                assert_eq!(volume_info_object_id, 101);
            } else {
                panic!("wrong ObjectDescriptor");
            }

            assert_eq!(
                dir.lookup("qux")
                    .await
                    .expect_err("lookup succeeded")
                    .downcast::<FxfsError>()
                    .expect("wrong error"),
                FxfsError::NotFound
            );
        }
    }
}
