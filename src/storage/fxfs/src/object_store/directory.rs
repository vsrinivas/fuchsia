// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        errors::FxfsError,
        lsm_tree::types::{ItemRef, LayerIterator},
        object_handle::ObjectHandle,
        object_store::{
            record::{ObjectItem, ObjectKey, ObjectKeyData, ObjectKind, ObjectValue},
            transaction::{Mutation, Transaction},
            HandleOptions, ObjectStore, StoreObjectHandle,
        },
    },
    anyhow::{anyhow, bail, Context, Error},
    std::{ops::Bound, sync::Arc},
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

    // TODO(jfsulliv): This is only necessary because we don't have some sort of iterator for
    // children yet. When we implement that, get rid of this.
    pub async fn has_children(&self) -> Result<bool, Error> {
        let layer_set = self.object_store().tree().layer_set();
        let mut merger = layer_set.merger();
        let key = ObjectKey::child(self.object_id, "").search_key();
        let mut iter = merger.seek(Bound::Included(&key)).await?;
        loop {
            match iter.get() {
                Some(ItemRef {
                    key: ObjectKey { object_id, data: ObjectKeyData::Child { .. } },
                    value: ObjectValue::Child { .. },
                }) if *object_id == self.object_id => return Ok(true),
                Some(ItemRef {
                    key: ObjectKey { object_id, data: ObjectKeyData::Child { .. } },
                    value: ObjectValue::None,
                }) if *object_id == self.object_id => {
                    // TODO(csuter): The choice of key for this call is actually irrelevant, since
                    // the first seek call should have loaded all layer iterators, and the hint
                    // provided to |advance_with_hint| only serves to abort early if the lower
                    // layers need to be loaded. Consider replacing the above |seek| with a variant
                    // that would let us simply |advance|.
                    iter.advance_with_hint(&key).await?;
                }
                _ => break,
            }
        }
        Ok(false)
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
        } else if let ObjectValue::None = item.value {
            bail!(FxfsError::NotFound);
        } else {
            Err(anyhow!(FxfsError::Inconsistent)
                .context(format!("Unexpected item in lookup: {:?}", item)))
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

    /// Removes |name| from the directory.
    ///
    /// Requires transaction locks on |self|.
    pub async fn remove_child<'a>(
        &'a self,
        transaction: &mut Transaction<'a>,
        name: &str,
    ) -> Result<(), Error> {
        self.replace_child(transaction, name, ObjectValue::None).await
    }

    async fn replace_child<'a>(
        &'a self,
        transaction: &mut Transaction<'a>,
        name: &str,
        new_value: ObjectValue,
    ) -> Result<(), Error> {
        let (object_id, descriptor) = self.lookup(name).await?;
        match descriptor {
            ObjectDescriptor::File => {
                self.store
                    .adjust_refs(transaction, object_id, -1)
                    .await
                    .context("Failed to adjust refs")?;
            }
            ObjectDescriptor::Directory => {
                let dir = self.store.open_directory(object_id).await?;
                if dir.has_children().await? {
                    bail!(FxfsError::NotEmpty);
                }
            }
            ObjectDescriptor::Volume(_) => {
                bail!("deleting volumes is unimplemented");
            }
        }
        transaction.add(
            self.store.store_object_id(),
            Mutation::replace_or_insert_object(ObjectKey::child(self.object_id, name), new_value),
        );
        Ok(())
    }
}

/// Moves src/src_name to dst/dst_name.
///
/// If |dst| already has a child |dst_name|, that child is removed; the constraints of remove_child
/// apply.
///
/// Requires transaction locks on both of |src| and |dst|.
pub async fn move_child<'a>(
    transaction: &mut Transaction<'a>,
    src: &'a Directory,
    src_name: &str,
    dst: &'a Directory,
    dst_name: &str,
) -> Result<(), Error> {
    let (object_id, descriptor) = src.lookup(src_name).await?;
    if let Err(e) = dst
        .replace_child(transaction, dst_name, ObjectValue::child(object_id, descriptor.clone()))
        .await
    {
        if FxfsError::NotFound.matches(&e) {
            transaction.add(
                dst.store.store_object_id(),
                Mutation::insert_object(
                    ObjectKey::child(dst.object_id, dst_name),
                    ObjectValue::child(object_id, descriptor),
                ),
            );
        } else {
            bail!(e);
        }
    }
    transaction.add(
        src.store.store_object_id(),
        Mutation::replace_or_insert_object(
            ObjectKey::child(src.object_id, src_name),
            ObjectValue::None,
        ),
    );
    Ok(())
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
                directory::move_child,
                filesystem::{FxFilesystem, SyncOptions},
                transaction::TransactionHandler,
                HandleOptions, ObjectDescriptor, ObjectHandle, ObjectHandleExt,
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

    #[fasync::run_singlethreaded(test)]
    async fn test_delete_child() {
        let device = Arc::new(FakeDevice::new(2048, TEST_DEVICE_BLOCK_SIZE));
        let fs = FxFilesystem::new_empty(device.clone()).await.expect("new_empty failed");
        let mut transaction =
            fs.clone().new_transaction(&[]).await.expect("new_transaction failed");
        let dir = fs
            .root_store()
            .create_directory(&mut transaction)
            .await
            .expect("create_directory failed");

        dir.create_child_file(&mut transaction, "foo").await.expect("create_child_file failed");
        fs.commit_transaction(transaction).await;

        let mut transaction =
            fs.clone().new_transaction(&[]).await.expect("new_transaction failed");
        dir.remove_child(&mut transaction, "foo").await.expect("remove_child failed");
        fs.commit_transaction(transaction).await;

        assert_eq!(
            dir.lookup("foo")
                .await
                .expect_err("lookup succeeded")
                .downcast::<FxfsError>()
                .expect("wrong error"),
            FxfsError::NotFound
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_delete_child_with_children_fails() {
        let device = Arc::new(FakeDevice::new(2048, TEST_DEVICE_BLOCK_SIZE));
        let fs = FxFilesystem::new_empty(device.clone()).await.expect("new_empty failed");
        let mut transaction =
            fs.clone().new_transaction(&[]).await.expect("new_transaction failed");
        let dir = fs
            .root_store()
            .create_directory(&mut transaction)
            .await
            .expect("create_directory failed");

        let child =
            dir.create_child_dir(&mut transaction, "foo").await.expect("create_child_dir failed");
        child.create_child_file(&mut transaction, "bar").await.expect("create_child_file failed");
        fs.commit_transaction(transaction).await;

        let mut transaction =
            fs.clone().new_transaction(&[]).await.expect("new_transaction failed");
        assert_eq!(
            dir.remove_child(&mut transaction, "foo")
                .await
                .expect_err("remove_child succeeded")
                .downcast::<FxfsError>()
                .expect("wrong error"),
            FxfsError::NotEmpty
        );

        child.remove_child(&mut transaction, "bar").await.expect("remove_child failed");
        fs.commit_transaction(transaction).await;

        let mut transaction =
            fs.clone().new_transaction(&[]).await.expect("new_transaction failed");
        dir.remove_child(&mut transaction, "foo").await.expect("remove_child failed");
        fs.commit_transaction(transaction).await;

        assert_eq!(
            dir.lookup("foo")
                .await
                .expect_err("lookup succeeded")
                .downcast::<FxfsError>()
                .expect("wrong error"),
            FxfsError::NotFound
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_delete_and_reinsert_child() {
        let device = Arc::new(FakeDevice::new(2048, TEST_DEVICE_BLOCK_SIZE));
        let fs = FxFilesystem::new_empty(device.clone()).await.expect("new_empty failed");
        let mut transaction =
            fs.clone().new_transaction(&[]).await.expect("new_transaction failed");
        let dir = fs
            .root_store()
            .create_directory(&mut transaction)
            .await
            .expect("create_directory failed");

        dir.create_child_file(&mut transaction, "foo").await.expect("create_child_file failed");
        fs.commit_transaction(transaction).await;

        let mut transaction =
            fs.clone().new_transaction(&[]).await.expect("new_transaction failed");
        dir.remove_child(&mut transaction, "foo").await.expect("remove_child failed");
        fs.commit_transaction(transaction).await;

        let mut transaction =
            fs.clone().new_transaction(&[]).await.expect("new_transaction failed");
        dir.create_child_file(&mut transaction, "foo").await.expect("create_child_file failed");
        fs.commit_transaction(transaction).await;

        dir.lookup("foo").await.expect("lookup failed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_delete_child_persists() {
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

            dir.create_child_file(&mut transaction, "foo").await.expect("create_child_file failed");
            fs.commit_transaction(transaction).await;
            dir.lookup("foo").await.expect("lookup failed");

            let mut transaction =
                fs.clone().new_transaction(&[]).await.expect("new_transaction failed");
            dir.remove_child(&mut transaction, "foo").await.expect("remove_child failed");
            fs.commit_transaction(transaction).await;

            fs.sync(SyncOptions::default()).await.expect("sync failed");
            dir.object_id()
        };

        let fs = FxFilesystem::open(device.clone()).await.expect("new_empty failed");
        let dir = fs.root_store().open_directory(object_id).await.expect("open_directory failed");
        assert_eq!(
            dir.lookup("foo")
                .await
                .expect_err("lookup succeeded")
                .downcast::<FxfsError>()
                .expect("wrong error"),
            FxfsError::NotFound
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_move_child() {
        let device = Arc::new(FakeDevice::new(2048, TEST_DEVICE_BLOCK_SIZE));
        let fs = FxFilesystem::new_empty(device.clone()).await.expect("new_empty failed");
        let mut transaction =
            fs.clone().new_transaction(&[]).await.expect("new_transaction failed");
        let dir = fs
            .root_store()
            .create_directory(&mut transaction)
            .await
            .expect("create_directory failed");

        let child_dir1 =
            dir.create_child_dir(&mut transaction, "dir1").await.expect("create_child_dir failed");
        let child_dir2 =
            dir.create_child_dir(&mut transaction, "dir2").await.expect("create_child_dir failed");
        child_dir1
            .create_child_file(&mut transaction, "foo")
            .await
            .expect("create_child_file failed");
        fs.commit_transaction(transaction).await;

        let mut transaction =
            fs.clone().new_transaction(&[]).await.expect("new_transaction failed");
        move_child(&mut transaction, &child_dir1, "foo", &child_dir2, "bar")
            .await
            .expect("move_child failed");
        fs.commit_transaction(transaction).await;

        assert_eq!(
            child_dir1
                .lookup("foo")
                .await
                .expect_err("lookup succeeded")
                .downcast::<FxfsError>()
                .expect("wrong error"),
            FxfsError::NotFound
        );
        child_dir2.lookup("bar").await.expect("lookup failed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_move_child_overwrites_dst() {
        let device = Arc::new(FakeDevice::new(2048, TEST_DEVICE_BLOCK_SIZE));
        let fs = FxFilesystem::new_empty(device.clone()).await.expect("new_empty failed");
        let mut transaction =
            fs.clone().new_transaction(&[]).await.expect("new_transaction failed");
        let dir = fs
            .root_store()
            .create_directory(&mut transaction)
            .await
            .expect("create_directory failed");

        let child_dir1 =
            dir.create_child_dir(&mut transaction, "dir1").await.expect("create_child_dir failed");
        let child_dir2 =
            dir.create_child_dir(&mut transaction, "dir2").await.expect("create_child_dir failed");
        let foo = child_dir1
            .create_child_file(&mut transaction, "foo")
            .await
            .expect("create_child_file failed");
        let bar = child_dir2
            .create_child_file(&mut transaction, "bar")
            .await
            .expect("create_child_file failed");
        fs.commit_transaction(transaction).await;

        {
            let mut buf = foo.allocate_buffer(TEST_DEVICE_BLOCK_SIZE as usize);
            buf.as_mut_slice().fill(0xaa);
            foo.write(0, buf.as_ref()).await.expect("write failed");
            buf.as_mut_slice().fill(0xbb);
            bar.write(0, buf.as_ref()).await.expect("write failed");
        }
        std::mem::drop(bar);
        std::mem::drop(foo);

        let mut transaction =
            fs.clone().new_transaction(&[]).await.expect("new_transaction failed");
        move_child(&mut transaction, &child_dir1, "foo", &child_dir2, "bar")
            .await
            .expect("move_child failed");
        fs.commit_transaction(transaction).await;

        assert_eq!(
            child_dir1
                .lookup("foo")
                .await
                .expect_err("lookup succeeded")
                .downcast::<FxfsError>()
                .expect("wrong error"),
            FxfsError::NotFound
        );

        // Check the contents to ensure that the file was replaced.
        let (oid, object_descriptor) = child_dir2.lookup("bar").await.expect("lookup failed");
        assert_eq!(object_descriptor, ObjectDescriptor::File);
        let bar = child_dir2
            .object_store()
            .open_object(oid, HandleOptions::default())
            .await
            .expect("Open failed");
        let mut buf = bar.allocate_buffer(TEST_DEVICE_BLOCK_SIZE as usize);
        bar.read(0, buf.as_mut()).await.expect("read failed");
        assert_eq!(buf.as_slice(), vec![0xaa; TEST_DEVICE_BLOCK_SIZE as usize]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_move_child_fails_if_would_overwrite_nonempty_dir() {
        let device = Arc::new(FakeDevice::new(2048, TEST_DEVICE_BLOCK_SIZE));
        let fs = FxFilesystem::new_empty(device.clone()).await.expect("new_empty failed");
        let mut transaction =
            fs.clone().new_transaction(&[]).await.expect("new_transaction failed");
        let dir = fs
            .root_store()
            .create_directory(&mut transaction)
            .await
            .expect("create_directory failed");

        let child_dir1 =
            dir.create_child_dir(&mut transaction, "dir1").await.expect("create_child_dir failed");
        let child_dir2 =
            dir.create_child_dir(&mut transaction, "dir2").await.expect("create_child_dir failed");
        child_dir1
            .create_child_file(&mut transaction, "foo")
            .await
            .expect("create_child_file failed");
        let nested_child = child_dir2
            .create_child_dir(&mut transaction, "bar")
            .await
            .expect("create_child_file failed");
        nested_child
            .create_child_file(&mut transaction, "baz")
            .await
            .expect("create_child_file failed");
        fs.commit_transaction(transaction).await;

        let mut transaction =
            fs.clone().new_transaction(&[]).await.expect("new_transaction failed");
        assert_eq!(
            move_child(&mut transaction, &child_dir1, "foo", &child_dir2, "bar")
                .await
                .expect_err("move_child succeeded")
                .downcast::<FxfsError>()
                .expect("wrong error"),
            FxfsError::NotEmpty
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_move_child_within_dir() {
        let device = Arc::new(FakeDevice::new(2048, TEST_DEVICE_BLOCK_SIZE));
        let fs = FxFilesystem::new_empty(device.clone()).await.expect("new_empty failed");
        let mut transaction =
            fs.clone().new_transaction(&[]).await.expect("new_transaction failed");
        let dir = fs
            .root_store()
            .create_directory(&mut transaction)
            .await
            .expect("create_directory failed");
        dir.create_child_file(&mut transaction, "foo").await.expect("create_child_file failed");
        fs.commit_transaction(transaction).await;

        let mut transaction =
            fs.clone().new_transaction(&[]).await.expect("new_transaction failed");
        move_child(&mut transaction, &dir, "foo", &dir, "bar").await.expect("move_child failed");
        fs.commit_transaction(transaction).await;

        assert_eq!(
            dir.lookup("foo")
                .await
                .expect_err("lookup old name succeeded")
                .downcast::<FxfsError>()
                .expect("wrong error"),
            FxfsError::NotFound
        );
        dir.lookup("bar").await.expect("lookup new name failed");
    }
}
