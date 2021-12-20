// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        lsm_tree::{
            simple_persistent_layer::SimplePersistentLayerWriter,
            types::{Item, ItemRef, LayerIterator, LayerWriter},
        },
        object_handle::{ObjectHandle, Writer},
        object_store::{
            allocator::{
                Allocator, AllocatorKey, AllocatorValue, CoalescingIterator, SimpleAllocator,
            },
            crypt::InsecureCrypt,
            directory::Directory,
            extent_record::{ExtentKey, ExtentValue},
            filesystem::{Filesystem, FxFilesystem, Mutations, OpenFxFilesystem, OpenOptions},
            fsck::{
                errors::{FsckError, FsckFatal, FsckIssue, FsckWarning},
                fsck_with_options, FsckOptions,
            },
            object_record::ObjectDescriptor,
            object_record::{ObjectKey, ObjectValue},
            transaction::{self, Options, TransactionHandler},
            volume::create_root_volume,
            HandleOptions, Mutation, ObjectStore,
        },
        round::round_down,
    },
    anyhow::{Context, Error},
    bincode::serialize_into,
    fuchsia_async as fasync,
    matches::assert_matches,
    std::{
        ops::{Bound, Deref},
        sync::{Arc, Mutex},
    },
    storage_device::{fake_device::FakeDevice, DeviceHolder},
};

const TEST_DEVICE_BLOCK_SIZE: u32 = 512;
const TEST_DEVICE_BLOCK_COUNT: u64 = 8192;

struct FsckTest {
    filesystem: Option<OpenFxFilesystem>,
    errors: Mutex<Vec<FsckIssue>>,
}

impl FsckTest {
    async fn new() -> Self {
        let filesystem = FxFilesystem::new_empty(
            DeviceHolder::new(FakeDevice::new(TEST_DEVICE_BLOCK_COUNT, TEST_DEVICE_BLOCK_SIZE)),
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
        self.filesystem = Some(
            FxFilesystem::open_with_options(
                device,
                OpenOptions { read_only: true, ..Default::default() },
                Arc::new(InsecureCrypt::new()),
            )
            .await
            .context("Failed to open FS")?,
        );
        Ok(())
    }
    async fn run(&self, halt_on_error: bool) -> Result<(), Error> {
        let options = FsckOptions {
            halt_on_error,
            do_slow_passes: true,
            on_error: |err| {
                self.errors.lock().unwrap().push(err.clone());
            },
        };
        fsck_with_options(&self.filesystem(), options).await
    }
    fn filesystem(&self) -> Arc<FxFilesystem> {
        self.filesystem.as_ref().unwrap().deref().clone()
    }
    fn errors(&self) -> Vec<FsckIssue> {
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
        // We need a discontiguous allocation, and some blocks will have been used up by other
        // things, so allocate the very last block.  Note that changing our allocation strategy
        // might break this test.
        let end =
            round_down(TEST_DEVICE_BLOCK_SIZE as u64 * TEST_DEVICE_BLOCK_COUNT, fs.block_size());
        fs.allocator()
            .mark_allocated(&mut transaction, end - fs.block_size()..end)
            .await
            .expect("mark_allocated failed");
        transaction.commit().await.expect("commit failed");
    }

    test.remount().await.expect("Remount failed");
    test.run(false).await.expect_err("Fsck should fail");
    assert_matches!(
        test.errors()[..],
        [
            FsckIssue::Error(FsckError::ExtraAllocations(_)),
            FsckIssue::Error(FsckError::AllocatedBytesMismatch(..))
        ]
    );
}

#[fasync::run_singlethreaded(test)]
async fn test_misaligned_allocation() {
    let mut test = FsckTest::new().await;

    {
        let fs = test.filesystem();
        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        // We need a discontiguous allocation, and some blocks will have been used up by other
        // things, so allocate the very last block.  Note that changing our allocation strategy
        // might break this test.
        let end =
            round_down(TEST_DEVICE_BLOCK_SIZE as u64 * TEST_DEVICE_BLOCK_COUNT, fs.block_size());
        fs.allocator()
            .mark_allocated(&mut transaction, end - fs.block_size() + 1..end)
            .await
            .expect("mark_allocated failed");
        transaction.commit().await.expect("commit failed");
    }

    test.remount().await.expect("Remount failed");
    test.run(true).await.expect_err("Fsck should fail");
    assert_matches!(test.errors()[..], [FsckIssue::Error(FsckError::MisalignedAllocation(..))]);
}

#[fasync::run_singlethreaded(test)]
async fn test_malformed_allocation() {
    let mut test = FsckTest::new().await;

    {
        let fs = test.filesystem();
        let root_store = fs.root_store();
        let device = fs.device();
        // We need to manually insert the record into the allocator's LSM tree directly, since the
        // allocator code checks range validity.

        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        let layer_handle = ObjectStore::create_object(
            &root_store,
            &mut transaction,
            HandleOptions::default(),
            Some(0),
        )
        .await
        .expect("create_object failed");
        transaction.commit().await.expect("commit failed");

        {
            let mut writer =
                SimplePersistentLayerWriter::new(Writer::new(&layer_handle), fs.block_size());
            // We also need a discontiguous allocation, and some blocks will have been used up by
            // other things, so allocate the very last block.  Note that changing our allocation
            // strategy might break this test.
            let end = round_down(
                TEST_DEVICE_BLOCK_SIZE as u64 * TEST_DEVICE_BLOCK_COUNT,
                fs.block_size(),
            );
            let item = Item::new(
                AllocatorKey { device_range: end..end - fs.block_size() },
                AllocatorValue { delta: 1 },
            );
            writer.write(item.as_item_ref()).await.expect("write failed");
            writer.flush().await.expect("flush failed");
        }
        let mut allocator_info = fs.allocator().info();
        allocator_info.layers.push(layer_handle.object_id());
        let mut allocator_info_vec = vec![];
        serialize_into(&mut allocator_info_vec, &allocator_info).expect("serialize failed");
        let mut buf = device.allocate_buffer(allocator_info_vec.len());
        buf.as_mut_slice().copy_from_slice(&allocator_info_vec[..]);

        let handle = ObjectStore::open_object(
            &root_store,
            fs.allocator().object_id(),
            HandleOptions::default(),
        )
        .await
        .expect("open allocator handle failed");
        let mut transaction = handle.new_transaction().await.expect("new_transaction failed");
        handle.txn_write(&mut transaction, 0, buf.as_ref()).await.expect("txn_write failed");
        transaction.commit().await.expect("commit failed");
    }

    test.remount().await.expect("Remount failed");
    test.run(true).await.expect_err("Fsck should fail");
    assert_matches!(test.errors()[..], [FsckIssue::Error(FsckError::MalformedAllocation(..))]);
}

#[fasync::run_singlethreaded(test)]
async fn test_misaligned_extent_in_root_store() {
    let mut test = FsckTest::new().await;

    {
        let fs = test.filesystem();
        let root_store = fs.root_store();

        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        transaction.add(
            root_store.store_object_id(),
            Mutation::extent(ExtentKey::new(555, 0, 1..fs.block_size()), ExtentValue::new(1)),
        );
        transaction.commit().await.expect("commit failed");
    }

    test.remount().await.expect("Remount failed");
    test.run(true).await.expect_err("Fsck should fail");
    assert_matches!(test.errors()[..], [FsckIssue::Error(FsckError::MisalignedExtent(..))]);
}

#[fasync::run_singlethreaded(test)]
async fn test_malformed_extent_in_root_store() {
    let mut test = FsckTest::new().await;

    {
        let fs = test.filesystem();
        let root_store = fs.root_store();

        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        transaction.add(
            root_store.store_object_id(),
            Mutation::extent(ExtentKey::new(555, 0, fs.block_size()..0), ExtentValue::new(1)),
        );
        transaction.commit().await.expect("commit failed");
    }

    test.remount().await.expect("Remount failed");
    test.run(true).await.expect_err("Fsck should fail");
    assert_matches!(test.errors()[..], [FsckIssue::Error(FsckError::MalformedExtent(..))]);
}

#[fasync::run_singlethreaded(test)]
async fn test_misaligned_extent_in_child_store() {
    let mut test = FsckTest::new().await;

    {
        let fs = test.filesystem();
        let root_volume = create_root_volume(&fs).await.unwrap();
        let volume = root_volume.new_volume("vol").await.unwrap();

        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        transaction.add(
            volume.store_object_id(),
            Mutation::extent(ExtentKey::new(555, 0, 1..fs.block_size()), ExtentValue::new(1)),
        );
        transaction.commit().await.expect("commit failed");
    }

    test.remount().await.expect("Remount failed");
    test.run(true).await.expect_err("Fsck should fail");
    assert_matches!(test.errors()[..], [FsckIssue::Error(FsckError::MisalignedExtent(..))]);
}

#[fasync::run_singlethreaded(test)]
async fn test_malformed_extent_in_child_store() {
    let mut test = FsckTest::new().await;

    {
        let fs = test.filesystem();
        let root_volume = create_root_volume(&fs).await.unwrap();
        let volume = root_volume.new_volume("vol").await.unwrap();

        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        transaction.add(
            volume.store_object_id(),
            Mutation::extent(ExtentKey::new(555, 0, fs.block_size()..0), ExtentValue::new(1)),
        );
        transaction.commit().await.expect("commit failed");
    }

    test.remount().await.expect("Remount failed");
    test.run(true).await.expect_err("Fsck should fail");
    assert_matches!(test.errors()[..], [FsckIssue::Error(FsckError::MalformedExtent(..))]);
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
    assert_matches!(
        test.errors()[..],
        [
            FsckIssue::Error(FsckError::AllocationMismatch(..)),
            FsckIssue::Error(FsckError::ExtraAllocations(..))
        ]
    );
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
    assert_matches!(
        test.errors()[..],
        [
            FsckIssue::Error(FsckError::MissingAllocation(..)),
            FsckIssue::Error(FsckError::AllocatedBytesMismatch(..))
        ]
    );
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
    assert_matches!(
        test.errors()[..],
        [
            FsckIssue::Error(FsckError::RefCountMismatch(..)),
            FsckIssue::Error(FsckError::ObjectCountMismatch(..))
        ]
    );
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
    assert_matches!(test.errors()[..], [FsckIssue::Warning(FsckWarning::OrphanedObject(..))]);
}

#[fasync::run_singlethreaded(test)]
async fn test_missing_extent_tree_layer_file() {
    let mut test = FsckTest::new().await;

    {
        let fs = test.filesystem();
        let root_volume = create_root_volume(&fs).await.unwrap();
        let volume = root_volume.new_volume("vol").await.unwrap();
        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        ObjectStore::create_object(&volume, &mut transaction, HandleOptions::default(), Some(0))
            .await
            .expect("create_object failed");
        transaction.commit().await.expect("commit failed");
        volume.flush().await.expect("Flush store failed");
        let id = {
            let layers = volume.extent_tree().immutable_layer_set();
            assert!(!layers.layers.is_empty());
            layers.layers[0].handle().unwrap().object_id()
        };
        fs.root_store()
            .tombstone(id, transaction::Options::default())
            .await
            .expect("tombstone failed");
    }

    test.remount().await.expect("Remount failed");
    test.run(false).await.expect_err("Fsck should fail");
    assert_matches!(test.errors()[..], [FsckIssue::Fatal(FsckFatal::MissingLayerFile(..))]);
}

#[fasync::run_singlethreaded(test)]
async fn test_missing_object_tree_layer_file() {
    let mut test = FsckTest::new().await;

    {
        let fs = test.filesystem();
        let root_volume = create_root_volume(&fs).await.unwrap();
        let volume = root_volume.new_volume("vol").await.unwrap();
        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        ObjectStore::create_object(&volume, &mut transaction, HandleOptions::default(), Some(0))
            .await
            .expect("create_object failed");
        transaction.commit().await.expect("commit failed");
        volume.flush().await.expect("Flush store failed");
        let id = {
            let layers = volume.tree().immutable_layer_set();
            assert!(!layers.layers.is_empty());
            layers.layers[0].handle().unwrap().object_id()
        };
        fs.root_store()
            .tombstone(id, transaction::Options::default())
            .await
            .expect("tombstone failed");
    }

    test.remount().await.expect("Remount failed");
    test.run(false).await.expect_err("Fsck should fail");
    assert_matches!(test.errors()[..], [FsckIssue::Fatal(FsckFatal::MissingLayerFile(..))]);
}

#[fasync::run_singlethreaded(test)]
async fn test_missing_object_store_handle() {
    let mut test = FsckTest::new().await;

    {
        let fs = test.filesystem();
        let root_volume = create_root_volume(&fs).await.unwrap();
        let store_id = {
            let volume = root_volume.new_volume("vol").await.unwrap();
            volume.store_object_id()
        };
        fs.root_store()
            .tombstone(store_id, transaction::Options::default())
            .await
            .expect("tombstone failed");
    }

    test.remount().await.expect("Remount failed");
    test.run(false).await.expect_err("Fsck should fail");
    assert_matches!(test.errors()[..], [FsckIssue::Fatal(FsckFatal::MissingStoreInfo(..))]);
}

#[fasync::run_singlethreaded(test)]
async fn test_misordered_layer_file() {
    let mut test = FsckTest::new().await;

    {
        let fs = test.filesystem();
        let device = fs.device();
        let root_store = fs.root_store();
        let root_volume = create_root_volume(&fs).await.unwrap();
        let volume = root_volume.new_volume("vol").await.unwrap();
        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        let layer_handle = ObjectStore::create_object(
            &root_store,
            &mut transaction,
            HandleOptions::default(),
            Some(0),
        )
        .await
        .expect("create_object failed");
        transaction.commit().await.expect("commit failed");

        {
            let mut writer =
                SimplePersistentLayerWriter::new(Writer::new(&layer_handle), fs.block_size());
            let item1 = Item::new(ExtentKey::new(5, 0, 10..20), ExtentValue::None);
            let item2 = Item::new(ExtentKey::new(0, 0, 0..5), ExtentValue::None);
            writer.write(item1.as_item_ref()).await.expect("write failed");
            writer.write(item2.as_item_ref()).await.expect("write failed");
            writer.flush().await.expect("flush failed");
        }
        let mut store_info = volume.store_info();
        store_info.extent_tree_layers = vec![layer_handle.object_id()];
        let mut store_info_vec = vec![];
        serialize_into(&mut store_info_vec, &store_info).expect("serialize failed");
        let mut buf = device.allocate_buffer(store_info_vec.len());
        buf.as_mut_slice().copy_from_slice(&store_info_vec[..]);

        let store_info_handle = ObjectStore::open_object(
            &root_store,
            volume.store_info_handle_object_id().unwrap(),
            HandleOptions::default(),
        )
        .await
        .expect("open store info handle failed");
        let mut transaction =
            store_info_handle.new_transaction().await.expect("new_transaction failed");
        store_info_handle
            .txn_write(&mut transaction, 0, buf.as_ref())
            .await
            .expect("txn_write failed");
        transaction.commit().await.expect("commit failed");
    }

    test.remount().await.expect("Remount failed");
    test.run(false).await.expect_err("Fsck should fail");
    assert_matches!(test.errors()[..], [FsckIssue::Fatal(FsckFatal::MisOrderedLayerFile(..))]);
}

#[fasync::run_singlethreaded(test)]
async fn test_overlapping_keys_in_layer_file() {
    let mut test = FsckTest::new().await;

    {
        let fs = test.filesystem();
        let device = fs.device();
        let root_store = fs.root_store();
        let root_volume = create_root_volume(&fs).await.unwrap();
        let volume = root_volume.new_volume("vol").await.unwrap();
        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        let layer_handle = ObjectStore::create_object(
            &root_store,
            &mut transaction,
            HandleOptions::default(),
            Some(0),
        )
        .await
        .expect("create_object failed");
        transaction.commit().await.expect("commit failed");

        {
            let mut writer =
                SimplePersistentLayerWriter::new(Writer::new(&layer_handle), fs.block_size());
            let item1 = Item::new(ExtentKey::new(0, 0, 0..20), ExtentValue::None);
            let item2 = Item::new(ExtentKey::new(0, 0, 10..30), ExtentValue::None);
            writer.write(item1.as_item_ref()).await.expect("write failed");
            writer.write(item2.as_item_ref()).await.expect("write failed");
            writer.flush().await.expect("flush failed");
        }
        let mut store_info = volume.store_info();
        store_info.extent_tree_layers = vec![layer_handle.object_id()];
        let mut store_info_vec = vec![];
        serialize_into(&mut store_info_vec, &store_info).expect("serialize failed");
        let mut buf = device.allocate_buffer(store_info_vec.len());
        buf.as_mut_slice().copy_from_slice(&store_info_vec[..]);

        let store_info_handle = ObjectStore::open_object(
            &root_store,
            volume.store_info_handle_object_id().unwrap(),
            HandleOptions::default(),
        )
        .await
        .expect("open store info handle failed");
        let mut transaction =
            store_info_handle.new_transaction().await.expect("new_transaction failed");
        store_info_handle
            .txn_write(&mut transaction, 0, buf.as_ref())
            .await
            .expect("txn_write failed");
        transaction.commit().await.expect("commit failed");
    }

    test.remount().await.expect("Remount failed");
    test.run(false).await.expect_err("Fsck should fail");
    assert_matches!(
        test.errors()[..],
        [FsckIssue::Fatal(FsckFatal::OverlappingKeysInLayerFile(..))]
    );
}

#[fasync::run_singlethreaded(test)]
async fn test_unexpected_record_in_layer_file() {
    let mut test = FsckTest::new().await;

    {
        let fs = test.filesystem();
        let device = fs.device();
        let root_store = fs.root_store();
        let root_volume = create_root_volume(&fs).await.unwrap();
        let volume = root_volume.new_volume("vol").await.unwrap();
        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        let layer_handle = ObjectStore::create_object(
            &root_store,
            &mut transaction,
            HandleOptions::default(),
            Some(0),
        )
        .await
        .expect("create_object failed");
        transaction.commit().await.expect("commit failed");

        {
            // Write an ObjectKey/ObjectValue into a tree that expects an ExtentKey/ExtentValue.
            let mut writer =
                SimplePersistentLayerWriter::new(Writer::new(&layer_handle), fs.block_size());
            let item = Item::new(ObjectKey::object(0), ObjectValue::None);
            writer.write(item.as_item_ref()).await.expect("write failed");
            writer.flush().await.expect("flush failed");
        }
        let mut store_info = volume.store_info();
        store_info.extent_tree_layers = vec![layer_handle.object_id()];
        let mut store_info_vec = vec![];
        serialize_into(&mut store_info_vec, &store_info).expect("serialize failed");
        let mut buf = device.allocate_buffer(store_info_vec.len());
        buf.as_mut_slice().copy_from_slice(&store_info_vec[..]);

        let store_info_handle = ObjectStore::open_object(
            &root_store,
            volume.store_info_handle_object_id().unwrap(),
            HandleOptions::default(),
        )
        .await
        .expect("open store info handle failed");
        let mut transaction =
            store_info_handle.new_transaction().await.expect("new_transaction failed");
        store_info_handle
            .txn_write(&mut transaction, 0, buf.as_ref())
            .await
            .expect("txn_write failed");
        transaction.commit().await.expect("commit failed");
    }

    test.remount().await.expect("Remount failed");
    test.run(false).await.expect_err("Fsck should fail");
    assert_matches!(test.errors()[..], [FsckIssue::Fatal(FsckFatal::MalformedLayerFile(..))]);
}
