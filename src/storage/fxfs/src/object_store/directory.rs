// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        errors::FxfsError,
        lsm_tree::{
            merge::{Merger, MergerIterator},
            types::{ItemRef, LayerIterator},
        },
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

    pub async fn has_children(&self) -> Result<bool, Error> {
        let layer_set = self.store.tree().layer_set();
        let mut merger = layer_set.merger();
        Ok(self.iter(&mut merger).await?.get().is_some())
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

    /// Inserts a child into the directory.
    ///
    /// Requires transaction locks on |self|.
    pub fn insert_child<'a>(
        &'a self,
        transaction: &mut Transaction<'a>,
        name: &str,
        object_id: u64,
        descriptor: ObjectDescriptor,
    ) {
        transaction.add(
            self.store.store_object_id(),
            Mutation::replace_or_insert_object(
                ObjectKey::child(self.object_id, name),
                ObjectValue::child(object_id, descriptor),
            ),
        );
    }

    /// Returns an iterator that will return directory entries skipping deleted ones.  Example
    /// usage:
    ///
    ///   let layer_set = dir.object_store().tree().layer_set();
    ///   let mut merger = layer_set.merger();
    ///   let mut iter = dir.iter(&mut merger).await?;
    ///
    pub async fn iter<'a, 'b>(
        &self,
        merger: &'a mut Merger<'b, ObjectKey, ObjectValue>,
    ) -> Result<DirectoryIterator<'a, 'b>, Error> {
        self.iter_from(merger, "").await
    }

    /// Like "iter", but seeks from a specific filename (inclusive).  Example usage:
    ///
    ///   let layer_set = dir.object_store().tree().layer_set();
    ///   let mut merger = layer_set.merger();
    ///   let mut iter = dir.iter_from(&mut merger, "foo").await?;
    ///
    pub async fn iter_from<'a, 'b>(
        &self,
        merger: &'a mut Merger<'b, ObjectKey, ObjectValue>,
        from: &str,
    ) -> Result<DirectoryIterator<'a, 'b>, Error> {
        let mut iter =
            merger.seek(Bound::Included(&ObjectKey::child(self.object_id, from))).await?;
        // Skip deleted entries.
        // TODO(csuter): Remove this once we've developed a filtering iterator.
        loop {
            match iter.get() {
                Some(ItemRef { key: ObjectKey { object_id, .. }, value: ObjectValue::None })
                    if *object_id == self.object_id => {}
                _ => break,
            }
            iter.advance().await?;
        }
        Ok(DirectoryIterator { object_id: self.object_id, iter })
    }
}

pub struct DirectoryIterator<'a, 'b> {
    object_id: u64,
    iter: MergerIterator<'a, 'b, ObjectKey, ObjectValue>,
}

impl DirectoryIterator<'_, '_> {
    pub fn get(&self) -> Option<(&str, u64, &ObjectDescriptor)> {
        match self.iter.get() {
            Some(ItemRef {
                key: ObjectKey { object_id: oid, data: ObjectKeyData::Child { name } },
                value: ObjectValue::Child { object_id, object_descriptor },
            }) if *oid == self.object_id => Some((name, *object_id, object_descriptor)),
            _ => None,
        }
    }

    pub async fn advance(&mut self) -> Result<(), Error> {
        loop {
            self.iter.advance().await?;
            // Skip deleted entries.
            match self.iter.get() {
                Some(ItemRef { key: ObjectKey { object_id, .. }, value: ObjectValue::None })
                    if *object_id == self.object_id => {}
                _ => return Ok(()),
            }
        }
    }
}

/// Moves src.0/src.1 to dst.0/dst.1.
///
/// If |dst.0| already has a child |dst.1|, it is removed. If that child was a directory, it must
/// be empty. If |graveyard| is set, the child is moved there instead of being deleted.
///
/// If |src| is None, this is effectively the same as unlink(dst.0/dst.1).
pub async fn replace_child<'a>(
    transaction: &mut Transaction<'a>,
    src: Option<(&'a Directory, &str)>,
    dst: (&'a Directory, &str),
    graveyard: Option<&'a Directory>,
) -> Result<(), Error> {
    let deleted_id_and_descriptor = match dst.0.lookup(dst.1).await {
        Ok((old_id, ObjectDescriptor::File)) => {
            if graveyard.is_none() {
                dst.0
                    .store
                    .adjust_refs(transaction, old_id, -1)
                    .await
                    .context("Failed to adjust refs")?;
            }
            Some((old_id, ObjectDescriptor::File))
        }
        Ok((old_id, ObjectDescriptor::Directory)) => {
            let dir = dst.0.store.open_directory(old_id).await?;
            if dir.has_children().await? {
                bail!(FxfsError::NotEmpty);
            }
            Some((old_id, ObjectDescriptor::Directory))
        }
        Ok((_, ObjectDescriptor::Volume(_))) => bail!(FxfsError::Inconsistent),
        Err(e) if FxfsError::NotFound.matches(&e) => {
            if src.is_none() {
                // Neither src nor dst exist
                bail!(FxfsError::NotFound);
            }
            None
        }
        Err(e) => bail!(e),
    };
    let new_value = if let Some((src_dir, src_name)) = src {
        transaction.add(
            src_dir.store.store_object_id(),
            Mutation::replace_or_insert_object(
                ObjectKey::child(src_dir.object_id, src_name),
                ObjectValue::None,
            ),
        );
        src_dir.lookup(src_name).await.map(|(id, descriptor)| ObjectValue::child(id, descriptor))?
    } else {
        ObjectValue::None
    };
    transaction.add(
        dst.0.store.store_object_id(),
        Mutation::replace_or_insert_object(ObjectKey::child(dst.0.object_id, dst.1), new_value),
    );
    if let Some(graveyard) = graveyard {
        if let Some((id, descriptor)) = deleted_id_and_descriptor {
            transaction.add(
                graveyard.store.store_object_id(),
                Mutation::insert_object(
                    ObjectKey::child(graveyard.object_id, &format!("{}", id)),
                    ObjectValue::child(id, descriptor),
                ),
            );
        }
    }
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
                directory::replace_child,
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
        transaction.commit().await;

        let mut transaction =
            fs.clone().new_transaction(&[]).await.expect("new_transaction failed");
        replace_child(&mut transaction, None, (&dir, "foo"), None)
            .await
            .expect("replace_child failed");
        transaction.commit().await;

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
    async fn test_delete_child_into_graveyard() {
        let device = Arc::new(FakeDevice::new(2048, TEST_DEVICE_BLOCK_SIZE));
        let fs = FxFilesystem::new_empty(device.clone()).await.expect("new_empty failed");
        let mut transaction =
            fs.clone().new_transaction(&[]).await.expect("new_transaction failed");
        let dir = fs
            .root_store()
            .create_directory(&mut transaction)
            .await
            .expect("create_directory failed");
        let graveyard = fs
            .root_store()
            .create_directory(&mut transaction)
            .await
            .expect("create_directory failed");

        dir.create_child_file(&mut transaction, "foo").await.expect("create_child_file failed");
        transaction.commit().await;

        assert!(!graveyard.has_children().await.expect("has_children failed"));

        let mut transaction =
            fs.clone().new_transaction(&[]).await.expect("new_transaction failed");
        replace_child(&mut transaction, None, (&dir, "foo"), Some(&graveyard))
            .await
            .expect("replace_child failed");
        transaction.commit().await;

        assert_eq!(
            dir.lookup("foo")
                .await
                .expect_err("lookup succeeded")
                .downcast::<FxfsError>()
                .expect("wrong error"),
            FxfsError::NotFound
        );
        assert!(graveyard.has_children().await.expect("has_children failed"));
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
        transaction.commit().await;

        let mut transaction =
            fs.clone().new_transaction(&[]).await.expect("new_transaction failed");
        assert_eq!(
            replace_child(&mut transaction, None, (&dir, "foo"), None)
                .await
                .expect_err("replace_child succeeded")
                .downcast::<FxfsError>()
                .expect("wrong error"),
            FxfsError::NotEmpty
        );

        let mut transaction =
            fs.clone().new_transaction(&[]).await.expect("new_transaction failed");
        replace_child(&mut transaction, None, (&child, "bar"), None)
            .await
            .expect("replace_child failed");
        transaction.commit().await;

        let mut transaction =
            fs.clone().new_transaction(&[]).await.expect("new_transaction failed");
        replace_child(&mut transaction, None, (&dir, "foo"), None)
            .await
            .expect("replace_child failed");
        transaction.commit().await;

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
        transaction.commit().await;

        let mut transaction =
            fs.clone().new_transaction(&[]).await.expect("new_transaction failed");
        replace_child(&mut transaction, None, (&dir, "foo"), None)
            .await
            .expect("replace_child failed");
        transaction.commit().await;

        let mut transaction =
            fs.clone().new_transaction(&[]).await.expect("new_transaction failed");
        dir.create_child_file(&mut transaction, "foo").await.expect("create_child_file failed");
        transaction.commit().await;

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
            transaction.commit().await;
            dir.lookup("foo").await.expect("lookup failed");

            let mut transaction =
                fs.clone().new_transaction(&[]).await.expect("new_transaction failed");
            replace_child(&mut transaction, None, (&dir, "foo"), None)
                .await
                .expect("replace_child failed");
            transaction.commit().await;

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
    async fn test_replace_child() {
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
        transaction.commit().await;

        let mut transaction =
            fs.clone().new_transaction(&[]).await.expect("new_transaction failed");
        replace_child(&mut transaction, Some((&child_dir1, "foo")), (&child_dir2, "bar"), None)
            .await
            .expect("replace_child failed");
        transaction.commit().await;

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
    async fn test_replace_child_overwrites_dst() {
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
        transaction.commit().await;

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
        replace_child(&mut transaction, Some((&child_dir1, "foo")), (&child_dir2, "bar"), None)
            .await
            .expect("replace_child failed");
        transaction.commit().await;

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
    async fn test_replace_child_fails_if_would_overwrite_nonempty_dir() {
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
        transaction.commit().await;

        let mut transaction =
            fs.clone().new_transaction(&[]).await.expect("new_transaction failed");
        assert_eq!(
            replace_child(&mut transaction, Some((&child_dir1, "foo")), (&child_dir2, "bar"), None)
                .await
                .expect_err("replace_child succeeded")
                .downcast::<FxfsError>()
                .expect("wrong error"),
            FxfsError::NotEmpty
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_replace_child_within_dir() {
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
        transaction.commit().await;

        let mut transaction =
            fs.clone().new_transaction(&[]).await.expect("new_transaction failed");
        replace_child(&mut transaction, Some((&dir, "foo")), (&dir, "bar"), None)
            .await
            .expect("replace_child failed");
        transaction.commit().await;

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

    #[fasync::run_singlethreaded(test)]
    async fn test_iterate() {
        let device = Arc::new(FakeDevice::new(2048, TEST_DEVICE_BLOCK_SIZE));
        let fs = FxFilesystem::new_empty(device.clone()).await.expect("new_empty failed");
        let mut transaction =
            fs.clone().new_transaction(&[]).await.expect("new_transaction failed");
        let dir = fs
            .root_store()
            .create_directory(&mut transaction)
            .await
            .expect("create_directory failed");
        let _cat =
            dir.create_child_file(&mut transaction, "cat").await.expect("create_child_file failed");
        let _ball = dir
            .create_child_file(&mut transaction, "ball")
            .await
            .expect("create_child_file failed");
        let _apple = dir
            .create_child_file(&mut transaction, "apple")
            .await
            .expect("create_child_file failed");
        let _dog =
            dir.create_child_file(&mut transaction, "dog").await.expect("create_child_file failed");
        transaction.commit().await;
        let mut transaction =
            fs.clone().new_transaction(&[]).await.expect("new_transaction failed");
        replace_child(&mut transaction, None, (&dir, "apple"), None)
            .await
            .expect("rereplace_child failed");
        transaction.commit().await;
        let layer_set = dir.object_store().tree().layer_set();
        let mut merger = layer_set.merger();
        let mut iter = dir.iter(&mut merger).await.expect("iter failed");
        let mut entries = Vec::new();
        while let Some((name, _, _)) = iter.get() {
            entries.push(name.to_string());
            iter.advance().await.expect("advance failed");
        }
        assert_eq!(&entries, &["ball", "cat", "dog"]);
    }
}
