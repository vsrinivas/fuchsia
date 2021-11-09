// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        lsm_tree::types::{Item, ItemRef, LayerIterator},
        object_handle::ObjectHandle,
        object_store::{
            allocator::{
                Allocator, AllocatorKey, AllocatorValue, CoalescingIterator, SimpleAllocator,
            },
            crypt::InsecureCrypt,
            directory::Directory,
            filesystem::{Filesystem, FxFilesystem, OpenFxFilesystem},
            fsck::{
                errors::{FsckError, FsckFatal, FsckWarning},
                fsck_with_options, FsckOptions,
            },
            object_record::ObjectDescriptor,
            transaction::{self, Options, TransactionHandler},
            HandleOptions, ObjectStore,
        },
    },
    anyhow::Error,
    fuchsia_async as fasync,
    matches::assert_matches,
    std::{
        ops::{Bound, Deref},
        sync::{Arc, Mutex},
    },
    storage_device::{fake_device::FakeDevice, DeviceHolder},
};

const TEST_DEVICE_BLOCK_SIZE: u32 = 512;

struct FsckTest {
    filesystem: Option<OpenFxFilesystem>,
    errors: Mutex<Vec<FsckError>>,
}

impl FsckTest {
    async fn new() -> Self {
        let filesystem = FxFilesystem::new_empty(
            DeviceHolder::new(FakeDevice::new(8192, TEST_DEVICE_BLOCK_SIZE)),
            Arc::new(InsecureCrypt::new()),
        )
        .await
        .expect("new_empty failed");

        Self { filesystem: Some(filesystem), errors: Mutex::new(vec![]) }
    }
    async fn remount(&mut self) -> Result<(), Error> {
        let fs = self.filesystem.take().unwrap();
        fs.close().await.expect("Failed to close FS");
        let device = fs.take_device().await;
        device.reopen();
        self.filesystem =
            Some(FxFilesystem::open_read_only(device, Arc::new(InsecureCrypt::new())).await?);
        Ok(())
    }
    async fn run(&self, halt_on_warning: bool) -> Result<(), Error> {
        let options = FsckOptions {
            halt_on_warning,
            on_error: |err| {
                self.errors.lock().unwrap().push(err.clone());
            },
        };
        fsck_with_options(&self.filesystem(), options).await
    }
    fn filesystem(&self) -> Arc<FxFilesystem> {
        self.filesystem.as_ref().unwrap().deref().clone()
    }
    fn errors(&self) -> Vec<FsckError> {
        self.errors.lock().unwrap().clone()
    }
}

#[fasync::run_singlethreaded(test)]
async fn test_missing_graveyard() {
    let mut test = FsckTest::new().await;

    {
        let fs = test.filesystem();
        let root_store = fs.root_store();
        let mut transaction = fs
            .clone()
            .new_transaction(
                &[],
                transaction::Options {
                    skip_journal_checks: true,
                    borrow_metadata_space: true,
                    ..Default::default()
                },
            )
            .await
            .expect("New transaction failed");
        root_store.set_graveyard_directory_object_id(&mut transaction, u64::MAX - 1);
        transaction.commit().await.expect("Commit transaction failed");
    }

    test.remount().await.expect_err("Mount should fail");
}

#[fasync::run_singlethreaded(test)]
async fn test_extra_allocation() {
    let mut test = FsckTest::new().await;

    {
        let fs = test.filesystem();
        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        let offset = 4095 * TEST_DEVICE_BLOCK_SIZE as u64;
        fs.allocator()
            .mark_allocated(&mut transaction, offset..offset + TEST_DEVICE_BLOCK_SIZE as u64)
            .await
            .expect("mark_allocated failed");
        transaction.commit().await.expect("commit failed");
    }

    test.remount().await.expect("Remount failed");
    test.run(false).await.expect_err("Fsck should fail");
    assert_matches!(test.errors()[..], [FsckError::Fatal(FsckFatal::ExtraAllocations(_))]);
}

#[fasync::run_singlethreaded(test)]
async fn test_allocation_mismatch() {
    let mut test = FsckTest::new().await;

    {
        let fs = test.filesystem();
        let allocator = fs.allocator().as_any().downcast::<SimpleAllocator>().unwrap();
        let range = {
            let layer_set = allocator.tree().layer_set();
            let mut merger = layer_set.merger();
            let iter = merger.seek(Bound::Unbounded).await.expect("seek failed");
            let ItemRef { key: AllocatorKey { device_range }, .. } =
                iter.get().expect("missing item");
            device_range.clone()
        };
        // This should just change the delta.
        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        allocator.add_ref(&mut transaction, range);
        transaction.commit().await.expect("commit failed");
    }

    test.remount().await.expect("Remount failed");
    test.run(false).await.expect_err("Fsck should fail");
    assert_matches!(test.errors()[..], [FsckError::Fatal(FsckFatal::AllocationMismatch(..))]);
}

#[fasync::run_singlethreaded(test)]
async fn test_missing_allocation() {
    let test = FsckTest::new().await;

    {
        let fs = test.filesystem();
        let allocator = fs.allocator().as_any().downcast::<SimpleAllocator>().unwrap();
        let key = {
            let layer_set = allocator.tree().layer_set();
            let mut merger = layer_set.merger();
            let iter = CoalescingIterator::new(Box::new(
                merger.seek(Bound::Unbounded).await.expect("seek failed"),
            ))
            .await
            .expect("new failed");
            let ItemRef { key, .. } = iter.get().expect("missing item");
            key.clone()
        };
        let lower_bound = key.lower_bound_for_merge_into();
        allocator
            .tree()
            .merge_into(Item::new(key, AllocatorValue { delta: -1 }), &lower_bound)
            .await;
    }
    // We intentionally don't remount here, since the above tree mutation wouldn't persist
    // otherwise.
    // Structuring this test to actually persist a bad allocation layer file is possible but tricky
    // since flushing or committing transactions might itself perform allocations, and it isn't that
    // important.
    test.run(false).await.expect_err("Fsck should fail");
    assert_matches!(test.errors()[..], [FsckError::Fatal(FsckFatal::MissingAllocation(..))]);
}

#[fasync::run_singlethreaded(test)]
async fn test_too_many_object_refs() {
    let mut test = FsckTest::new().await;

    {
        let fs = test.filesystem();

        let root_store = fs.root_store();
        let root_directory = Directory::open(&root_store, root_store.root_directory_object_id())
            .await
            .expect("open failed");

        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        let child_file = root_directory
            .create_child_file(&mut transaction, "child_file")
            .await
            .expect("create_child_file failed");
        let child_dir = root_directory
            .create_child_dir(&mut transaction, "child_dir")
            .await
            .expect("create_child_directory failed");

        // Add an extra reference to the child file.
        child_dir
            .insert_child(&mut transaction, "test", child_file.object_id(), ObjectDescriptor::File)
            .await
            .expect("insert_child failed");
        transaction.commit().await.expect("commit failed");
    }

    test.remount().await.expect("Remount failed");
    test.run(false).await.expect_err("Fsck should fail");
    assert_matches!(test.errors()[..], [FsckError::Fatal(FsckFatal::RefCountMismatch(..))]);
}

#[fasync::run_singlethreaded(test)]
async fn test_too_few_object_refs() {
    let mut test = FsckTest::new().await;

    {
        let fs = test.filesystem();
        let root_store = fs.root_store();

        // Create an object but no directory entry referencing that object, so it will end up with a
        // reference count of one, but zero references.
        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        ObjectStore::create_object(
            &root_store,
            &mut transaction,
            HandleOptions::default(),
            Some(0),
        )
        .await
        .expect("create_object failed");
        transaction.commit().await.expect("commit failed");
    }

    test.remount().await.expect("Remount failed");
    test.run(false).await.expect("Fsck should succeed");
    assert_matches!(test.errors()[..], [FsckError::Warning(FsckWarning::OrphanedObject(..))]);
}
