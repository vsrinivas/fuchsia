// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        crypt::{insecure::InsecureCrypt, Crypt},
        filesystem::{Filesystem, FxFilesystem, JournalingObject, OpenFxFilesystem, OpenOptions},
        fsck::{
            errors::{FsckError, FsckFatal, FsckIssue, FsckWarning},
            fsck_volume_with_options, fsck_with_options, FsckOptions,
        },
        lsm_tree::{
            simple_persistent_layer::SimplePersistentLayerWriter,
            types::{Item, ItemRef, Key, LayerIterator, LayerWriter, Value},
        },
        object_handle::{ObjectHandle, ObjectHandleExt, Writer, INVALID_OBJECT_ID},
        object_store::{
            allocator::{Allocator, AllocatorKey, AllocatorValue, CoalescingIterator},
            directory::Directory,
            transaction::{self, Options},
            volume::root_volume,
            AttributeKey, ExtentValue, HandleOptions, Mutation, ObjectAttributes, ObjectDescriptor,
            ObjectKey, ObjectKind, ObjectStore, ObjectValue, StoreInfo, Timestamp,
        },
        round::round_down,
        serialized_types::VersionedLatest,
    },
    anyhow::{Context, Error},
    assert_matches::assert_matches,
    fuchsia_async as fasync,
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
    crypt: Option<Arc<dyn Crypt>>,
}

#[derive(Default)]
struct TestOptions {
    halt_on_error: bool,
    skip_system_fsck: bool,
    volume_store_id: Option<u64>,
}

impl FsckTest {
    async fn new() -> Self {
        let filesystem = FxFilesystem::new_empty(DeviceHolder::new(FakeDevice::new(
            TEST_DEVICE_BLOCK_COUNT,
            TEST_DEVICE_BLOCK_SIZE,
        )))
        .await
        .expect("new_empty failed");

        Self { filesystem: Some(filesystem), errors: Mutex::new(vec![]), crypt: None }
    }
    async fn remount(&mut self) -> Result<(), Error> {
        let fs = self.filesystem.take().unwrap();
        fs.close().await.expect("Failed to close FS");
        let device = fs.take_device().await;
        device.reopen(true);
        self.filesystem = Some(
            FxFilesystem::open_with_options(device, OpenOptions::read_only(true))
                .await
                .context("Failed to open FS")?,
        );
        Ok(())
    }
    async fn run(&self, test_options: TestOptions) -> Result<(), Error> {
        let options = FsckOptions {
            fail_on_warning: true,
            halt_on_error: test_options.halt_on_error,
            do_slow_passes: true,
            verbose: false,
            on_error: |err| {
                if err.is_error() {
                    eprintln!("Fsck error: {:?}", &err);
                } else {
                    println!("Fsck warning: {:?}", &err);
                }
                self.errors.lock().unwrap().push(err.clone());
            },
        };
        if !test_options.skip_system_fsck {
            fsck_with_options(self.filesystem(), &options).await?;
        }
        if let Some(store_id) = test_options.volume_store_id {
            fsck_volume_with_options(
                self.filesystem().as_ref(),
                &options,
                store_id,
                self.crypt.clone(),
            )
            .await?;
        }
        Ok(())
    }
    fn filesystem(&self) -> Arc<dyn Filesystem> {
        self.filesystem.as_ref().unwrap().deref().clone()
    }
    fn errors(&self) -> Vec<FsckIssue> {
        self.errors.lock().unwrap().clone()
    }
    fn get_crypt(&mut self) -> Arc<dyn Crypt> {
        self.crypt.get_or_insert_with(|| Arc::new(InsecureCrypt::new())).clone()
    }
}

// Creates a new layer file containing |items| and writes them in order into |store|, skipping all
// normal validation.  This allows bad records to be inserted into the object store (although they
// will still be subject to merging).
// Doing this in the root store might cause a variety of unrelated failures.
async fn install_items_in_store<K: Key, V: Value>(
    filesystem: &Arc<dyn Filesystem>,
    store: &ObjectStore,
    items: impl AsRef<[Item<K, V>]>,
) {
    let device = filesystem.device();
    let root_store = filesystem.root_store();
    let mut transaction = filesystem
        .clone()
        .new_transaction(&[], Options::default())
        .await
        .expect("new_transaction failed");
    let layer_handle = ObjectStore::create_object(
        &root_store,
        &mut transaction,
        HandleOptions::default(),
        store.crypt().as_deref(),
    )
    .await
    .expect("create_object failed");
    transaction.commit().await.expect("commit failed");

    {
        let mut writer = SimplePersistentLayerWriter::<Writer<'_>, K, V>::new(
            Writer::new(&layer_handle),
            filesystem.block_size(),
        )
        .await
        .expect("writer new");
        for item in items.as_ref() {
            writer.write(item.as_item_ref()).await.expect("write failed");
        }
        writer.flush().await.expect("flush failed");
    }

    // store.store_info() holds the current state of the store including unflushed mods.
    // The on-disk version should represent the state of the store at the time the layer files
    // were written (i.e. excluding any entries pending in the journal) so we read it and modify
    // it's layer files.
    let store_info_handle = ObjectStore::open_object(
        &root_store,
        store.store_info_handle_object_id().unwrap(),
        HandleOptions::default(),
        None,
    )
    .await
    .expect("open store info handle failed");

    let mut store_info = if store_info_handle.get_size() == 0 {
        StoreInfo::default()
    } else {
        let mut cursor = std::io::Cursor::new(
            store_info_handle.contents(1000).await.expect("error reading content"),
        );
        StoreInfo::deserialize_with_version(&mut cursor).expect("deserialize_error").0
    };
    store_info.layers.push(layer_handle.object_id());
    let mut store_info_vec = vec![];
    store_info.serialize_with_version(&mut store_info_vec).expect("serialize failed");
    let mut buf = device.allocate_buffer(store_info_vec.len());
    buf.as_mut_slice().copy_from_slice(&store_info_vec[..]);

    let mut transaction =
        store_info_handle.new_transaction().await.expect("new_transaction failed");
    store_info_handle.txn_write(&mut transaction, 0, buf.as_ref()).await.expect("txn_write failed");
    transaction.commit().await.expect("commit failed");
}

/* TODO(fxbug.dev/92054): Fix this test
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
        transaction.add(root_store.store_object_id, Mutation::graveyard_directory(u64::MAX - 1));
        transaction.commit().await.expect("Commit transaction failed");
    }

    test.remount().await.expect("Remount failed");
    test.run(TestOptions::default()).await.expect_err("Fsck should fail");
    assert_matches!(
        test.errors()[..],
        [
            FsckIssue::Error(FsckError::ExtraAllocations(_)),
            FsckIssue::Error(FsckError::AllocatedBytesMismatch(..))
        ]
    );
}
*/

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
            .mark_allocated(&mut transaction, 4, end - fs.block_size()..end)
            .await
            .expect("mark_allocated failed");
        transaction.commit().await.expect("commit failed");
    }

    test.remount().await.expect("Remount failed");
    test.run(TestOptions::default()).await.expect_err("Fsck should fail");
    assert_matches!(test.errors()[..], [FsckIssue::Error(FsckError::ExtraAllocations(_)), ..]);
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
            .mark_allocated(&mut transaction, 99, end - fs.block_size() + 1..end)
            .await
            .expect("mark_allocated failed");
        transaction.commit().await.expect("commit failed");
    }

    test.remount().await.expect("Remount failed");
    test.run(TestOptions { halt_on_error: true, ..Default::default() })
        .await
        .expect_err("Fsck should fail");
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
            None,
        )
        .await
        .expect("create_object failed");
        transaction.commit().await.expect("commit failed");

        {
            let mut writer =
                SimplePersistentLayerWriter::<Writer<'_>, AllocatorKey, AllocatorValue>::new(
                    Writer::new(&layer_handle),
                    fs.block_size(),
                )
                .await
                .expect("writer new");
            // We also need a discontiguous allocation, and some blocks will have been used up by
            // other things, so allocate the very last block.  Note that changing our allocation
            // strategy might break this test.
            let end = round_down(
                TEST_DEVICE_BLOCK_SIZE as u64 * TEST_DEVICE_BLOCK_COUNT,
                fs.block_size(),
            );
            let item = Item::new(
                AllocatorKey { device_range: end..end - fs.block_size() },
                AllocatorValue::Abs { count: 2, owner_object_id: 9 },
            );
            writer.write(item.as_item_ref()).await.expect("write failed");
            writer.flush().await.expect("flush failed");
        }
        let mut allocator_info = fs.allocator().info();
        allocator_info.layers.push(layer_handle.object_id());
        let mut allocator_info_vec = vec![];
        allocator_info.serialize_with_version(&mut allocator_info_vec).expect("serialize failed");
        let mut buf = device.allocate_buffer(allocator_info_vec.len());
        buf.as_mut_slice().copy_from_slice(&allocator_info_vec[..]);

        let handle = ObjectStore::open_object(
            &root_store,
            fs.allocator().object_id(),
            HandleOptions::default(),
            None,
        )
        .await
        .expect("open allocator handle failed");
        let mut transaction = handle.new_transaction().await.expect("new_transaction failed");
        handle.txn_write(&mut transaction, 0, buf.as_ref()).await.expect("txn_write failed");
        transaction.commit().await.expect("commit failed");
    }

    test.remount().await.expect("Remount failed");
    test.run(TestOptions { halt_on_error: true, ..Default::default() })
        .await
        .expect_err("Fsck should fail");
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
            Mutation::insert_object(
                ObjectKey::extent(555, 0, 1..fs.block_size()),
                ObjectValue::Extent(ExtentValue::new(1)),
            ),
        );
        transaction.commit().await.expect("commit failed");
    }

    test.remount().await.expect("Remount failed");
    test.run(TestOptions { halt_on_error: true, ..Default::default() })
        .await
        .expect_err("Fsck should fail");
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
            Mutation::insert_object(
                ObjectKey::extent(555, 0, fs.block_size()..0),
                ObjectValue::Extent(ExtentValue::new(1)),
            ),
        );
        transaction.commit().await.expect("commit failed");
    }

    test.remount().await.expect("Remount failed");
    test.run(TestOptions { halt_on_error: true, ..Default::default() })
        .await
        .expect_err("Fsck should fail");
    assert_matches!(test.errors()[..], [FsckIssue::Error(FsckError::MalformedExtent(..))]);
}

#[fasync::run_singlethreaded(test)]
async fn test_misaligned_extent_in_child_store() {
    let mut test = FsckTest::new().await;

    let store_id = {
        let fs = test.filesystem();
        let root_volume = root_volume(fs.clone()).await.unwrap();
        let store = root_volume.new_volume("vol", Some(test.get_crypt())).await.unwrap();

        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        transaction.add(
            store.store_object_id(),
            Mutation::insert_object(
                ObjectKey::extent(555, 0, 1..fs.block_size()),
                ObjectValue::Extent(ExtentValue::new(1)),
            ),
        );
        transaction.commit().await.expect("commit failed");
        store.store_object_id()
    };

    test.remount().await.expect("Remount failed");
    test.run(TestOptions {
        halt_on_error: true,
        volume_store_id: Some(store_id),
        ..Default::default()
    })
    .await
    .expect_err("Fsck should fail");
    assert_matches!(test.errors()[..], [FsckIssue::Error(FsckError::MisalignedExtent(..))]);
}

#[fasync::run_singlethreaded(test)]
async fn test_malformed_extent_in_child_store() {
    let mut test = FsckTest::new().await;

    let store_id = {
        let fs = test.filesystem();
        let root_volume = root_volume(fs.clone()).await.unwrap();
        let store = root_volume.new_volume("vol", Some(test.get_crypt())).await.unwrap();

        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        transaction.add(
            store.store_object_id(),
            Mutation::insert_object(
                ObjectKey::extent(555, 0, fs.block_size()..0),
                ObjectValue::Extent(ExtentValue::new(1)),
            ),
        );
        transaction.commit().await.expect("commit failed");
        store.store_object_id()
    };

    test.remount().await.expect("Remount failed");
    test.run(TestOptions {
        halt_on_error: true,
        volume_store_id: Some(store_id),
        ..Default::default()
    })
    .await
    .expect_err("Fsck should fail");
    assert_matches!(test.errors()[..], [FsckIssue::Error(FsckError::MalformedExtent(..))]);
}

#[fasync::run_singlethreaded(test)]
async fn test_allocation_mismatch() {
    let mut test = FsckTest::new().await;

    {
        let fs = test.filesystem();
        let allocator = fs.allocator();
        let range = {
            let layer_set = allocator.tree().layer_set();
            let mut merger = layer_set.merger();
            let iter = allocator.iter(&mut merger, Bound::Unbounded).await.expect("iter failed");
            let ItemRef { key: AllocatorKey { device_range }, .. } =
                iter.get().expect("missing item");
            device_range.clone()
        };
        // Replace owner_object_id with a different owner and bump count to something impossible.
        allocator
            .tree()
            .replace_or_insert(Item::new(
                AllocatorKey { device_range: range.clone() },
                AllocatorValue::Abs { count: 2, owner_object_id: 10 },
            ))
            .await;
        allocator.flush().await.expect("flush failed");
    }

    test.remount().await.expect("Remount failed");
    test.run(TestOptions::default()).await.expect_err("Fsck should fail");
    assert_matches!(
        test.errors()[..],
        [
            FsckIssue::Error(FsckError::AllocationForNonexistentOwner(..)),
            FsckIssue::Error(FsckError::MissingAllocation(..)),
            FsckIssue::Error(FsckError::AllocatedBytesMismatch(..)),
        ]
    );
}

#[fasync::run_singlethreaded(test)]
async fn test_volume_allocation_mismatch() {
    let mut test = FsckTest::new().await;
    let store_id = {
        let fs = test.filesystem();
        let device = fs.device();
        let store_id = {
            let root_volume = root_volume(fs.clone()).await.unwrap();
            let volume = root_volume.new_volume("vol", Some(test.get_crypt())).await.unwrap();
            let root_directory = Directory::open(&volume, volume.root_directory_object_id())
                .await
                .expect("open failed");

            let mut transaction = fs
                .clone()
                .new_transaction(&[], Options::default())
                .await
                .expect("new_transaction failed");
            let handle = root_directory
                .create_child_file(&mut transaction, "child_file")
                .await
                .expect("create_child_file failed");
            transaction.commit().await.expect("commit failed");
            let mut transaction = fs
                .clone()
                .new_transaction(&[], Options::default())
                .await
                .expect("new_transaction failed");
            let buf = device.allocate_buffer(1);
            handle
                .txn_write(&mut transaction, 1_048_576, buf.as_ref())
                .await
                .expect("write failed");
            transaction.commit().await.expect("commit failed");
            volume.flush().await.expect("Flush store failed");
            volume.store_object_id()
        };

        // Find and break first allocation record for the child store.
        let allocator = fs.allocator();
        let range = {
            let layer_set = allocator.tree().layer_set();
            let mut merger = layer_set.merger();
            let mut iter =
                allocator.iter(&mut merger, Bound::Unbounded).await.expect("iter failed");
            loop {
                if let ItemRef {
                    key: AllocatorKey { device_range },
                    value: AllocatorValue::Abs { owner_object_id, .. },
                    ..
                } = iter.get().expect("no allocations found")
                {
                    if *owner_object_id == store_id {
                        break device_range.clone();
                    }
                }
                iter.advance().await.expect("advance failed");
            }
        };
        allocator
            .tree()
            .replace_or_insert(Item::new(
                AllocatorKey { device_range: range },
                AllocatorValue::Abs { count: 2, owner_object_id: 42 },
            ))
            .await;
        allocator.flush().await.expect("flush failed");
        store_id
    };

    test.remount().await.expect("Remount failed");
    test.run(TestOptions {
        skip_system_fsck: true,
        volume_store_id: Some(store_id),
        ..Default::default()
    })
    .await
    .expect_err("Fsck should fail");
    assert_matches!(
        test.errors()[..],
        [
            FsckIssue::Error(FsckError::AllocationForNonexistentOwner(..)),
            FsckIssue::Error(FsckError::MissingAllocation(..)),
            FsckIssue::Error(FsckError::AllocatedBytesMismatch(..)),
        ]
    );
}

#[fasync::run_singlethreaded(test)]
async fn test_missing_allocation() {
    let test = FsckTest::new().await;

    {
        let fs = test.filesystem();
        let allocator = fs.allocator();
        let key = {
            let layer_set = allocator.tree().layer_set();
            let mut merger = layer_set.merger();
            let iter = allocator.iter(&mut merger, Bound::Unbounded).await.expect("iter failed");
            let iter = CoalescingIterator::new(iter).await.expect("filter failed");
            let ItemRef { key, .. } = iter.get().expect("missing item");
            // 'key' points at the first allocation record, which will be for the super blocks.
            key.clone()
        };
        let lower_bound = key.lower_bound_for_merge_into();
        allocator.tree().merge_into(Item::new(key, AllocatorValue::None), &lower_bound).await;
    }
    // We intentionally don't remount here, since the above tree mutation wouldn't persist
    // otherwise.
    // Structuring this test to actually persist a bad allocation layer file is possible but tricky
    // since flushing or committing transactions might itself perform allocations, and it isn't that
    // important.
    test.run(TestOptions::default()).await.expect_err("Fsck should fail");
    assert_matches!(
        test.errors()[..],
        [
            FsckIssue::Error(FsckError::MissingAllocation(..)),
            FsckIssue::Error(FsckError::AllocatedBytesMismatch(..)),
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
    test.run(TestOptions::default()).await.expect_err("Fsck should fail");
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
        ObjectStore::create_object(&root_store, &mut transaction, HandleOptions::default(), None)
            .await
            .expect("create_object failed");
        transaction.commit().await.expect("commit failed");
    }

    test.remount().await.expect("Remount failed");
    test.run(TestOptions::default()).await.expect_err("Fsck should fail");
    assert_matches!(test.errors()[..], [FsckIssue::Warning(FsckWarning::OrphanedObject(..))]);
}

#[fasync::run_singlethreaded(test)]
async fn test_missing_object_tree_layer_file() {
    let mut test = FsckTest::new().await;

    {
        let fs = test.filesystem();
        let root_volume = root_volume(fs.clone()).await.unwrap();
        let volume = root_volume.new_volume("vol", Some(test.get_crypt())).await.unwrap();
        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        ObjectStore::create_object(&volume, &mut transaction, HandleOptions::default(), None)
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

    test.remount().await.expect_err("Remount succeeded");
}

#[fasync::run_singlethreaded(test)]
async fn test_missing_object_store_handle() {
    let mut test = FsckTest::new().await;

    {
        let fs = test.filesystem();
        let root_volume = root_volume(fs.clone()).await.unwrap();
        let store_id = {
            let volume = root_volume.new_volume("vol", Some(test.get_crypt())).await.unwrap();
            volume.store_object_id()
        };
        fs.root_store()
            .tombstone(store_id, transaction::Options::default())
            .await
            .expect("tombstone failed");
    }

    test.remount().await.expect_err("Remount succeeded");
}

#[fasync::run_singlethreaded(test)]
async fn test_misordered_layer_file() {
    let mut test = FsckTest::new().await;

    let store_id = {
        let fs = test.filesystem();
        let root_volume = root_volume(fs.clone()).await.unwrap();
        let store = root_volume.new_volume("vol", Some(test.get_crypt())).await.unwrap();
        install_items_in_store(
            &fs,
            store.as_ref(),
            vec![
                Item::new(ObjectKey::extent(5, 0, 10..20), ObjectValue::deleted_extent()),
                Item::new(ObjectKey::extent(1, 0, 0..5), ObjectValue::deleted_extent()),
            ],
        )
        .await;
        store.store_object_id()
    };

    test.remount().await.expect("Remount failed");
    test.run(TestOptions { volume_store_id: Some(store_id), ..Default::default() })
        .await
        .expect_err("Fsck should fail");
    assert_matches!(test.errors()[..], [FsckIssue::Fatal(FsckFatal::MisOrderedLayerFile(..))]);
}

#[fasync::run_singlethreaded(test)]
async fn test_overlapping_keys_in_layer_file() {
    let mut test = FsckTest::new().await;

    let store_id = {
        let fs = test.filesystem();
        let root_volume = root_volume(fs.clone()).await.unwrap();
        let store = root_volume.new_volume("vol", Some(test.get_crypt())).await.unwrap();
        install_items_in_store(
            &fs,
            store.as_ref(),
            vec![
                Item::new(ObjectKey::extent(1, 0, 0..20), ObjectValue::deleted_extent()),
                Item::new(ObjectKey::extent(1, 0, 10..30), ObjectValue::deleted_extent()),
                Item::new(ObjectKey::extent(1, 0, 15..40), ObjectValue::deleted_extent()),
            ],
        )
        .await;
        store.store_object_id()
    };

    test.remount().await.expect("Remount failed");
    test.run(TestOptions { volume_store_id: Some(store_id), ..Default::default() })
        .await
        .expect_err("Fsck should fail");
    assert_matches!(
        test.errors()[..],
        [FsckIssue::Fatal(FsckFatal::OverlappingKeysInLayerFile(..))]
    );
}

#[fasync::run_singlethreaded(test)]
async fn test_unexpected_record_in_layer_file() {
    let mut test = FsckTest::new().await;
    // This test relies on the value below being something that doesn't deserialize to a valid ObjectValue.
    let store_id = {
        let fs = test.filesystem();
        let root_volume = root_volume(fs.clone()).await.unwrap();
        let store = root_volume.new_volume("vol", Some(test.get_crypt())).await.unwrap();
        install_items_in_store(
            &fs,
            store.as_ref(),
            vec![Item::new(ObjectKey::object(0), 0xffffffffu32)],
        )
        .await;
        store.store_object_id()
    };

    test.remount().await.expect("Remount failed");
    test.run(TestOptions { volume_store_id: Some(store_id), ..Default::default() })
        .await
        .expect_err("Fsck should fail");
    assert_matches!(test.errors()[..], [FsckIssue::Fatal(FsckFatal::MalformedLayerFile(..))]);
}

#[fasync::run_singlethreaded(test)]
async fn test_mismatched_key_and_value() {
    let mut test = FsckTest::new().await;

    let store_id = {
        let fs = test.filesystem();
        let root_volume = root_volume(fs.clone()).await.unwrap();
        let store = root_volume.new_volume("vol", Some(test.get_crypt())).await.unwrap();
        install_items_in_store(
            &fs,
            store.as_ref(),
            vec![Item::new(ObjectKey::object(10), ObjectValue::Attribute { size: 100 })],
        )
        .await;
        store.store_object_id()
    };

    test.remount().await.expect("Remount failed");
    test.run(TestOptions { volume_store_id: Some(store_id), ..Default::default() })
        .await
        .expect_err("Fsck should fail");
    assert_matches!(
        test.errors()[..],
        [FsckIssue::Error(FsckError::MalformedObjectRecord(..)), ..]
    );
}

#[fasync::run_singlethreaded(test)]
async fn test_link_to_root_directory() {
    let mut test = FsckTest::new().await;

    {
        let fs = test.filesystem();
        let store = fs.root_store();
        let root_directory =
            Directory::open(&store, store.root_directory_object_id()).await.expect("open failed");

        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        root_directory
            .insert_child(
                &mut transaction,
                "a",
                store.root_directory_object_id(),
                ObjectDescriptor::Directory,
            )
            .await
            .expect("insert_child failed");
        transaction.commit().await.expect("commit transaction failed");
    }

    test.remount().await.expect("Remount failed");
    test.run(TestOptions::default()).await.expect_err("Fsck should fail");
    assert_matches!(test.errors()[..], [FsckIssue::Error(FsckError::RootObjectHasParent(..)), ..]);
}

#[fasync::run_singlethreaded(test)]
async fn test_multiple_links_to_directory() {
    let mut test = FsckTest::new().await;

    let store_id = {
        let fs = test.filesystem();
        let root_volume = root_volume(fs.clone()).await.unwrap();
        let store = root_volume.new_volume("vol", Some(test.get_crypt())).await.unwrap();
        let root_directory =
            Directory::open(&store, store.root_directory_object_id()).await.expect("open failed");

        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        root_directory
            .insert_child(&mut transaction, "a", 10, ObjectDescriptor::Directory)
            .await
            .expect("insert_child failed");
        root_directory
            .insert_child(&mut transaction, "b", 10, ObjectDescriptor::Directory)
            .await
            .expect("insert_child failed");
        transaction.commit().await.expect("commit transaction failed");
        store.store_object_id()
    };

    test.remount().await.expect("Remount failed");
    test.run(TestOptions { volume_store_id: Some(store_id), ..Default::default() })
        .await
        .expect_err("Fsck should fail");
    assert_matches!(
        test.errors()[..],
        [FsckIssue::Error(FsckError::MultipleLinksToDirectory(..)), ..]
    );
}

#[fasync::run_singlethreaded(test)]
async fn test_conflicting_link_types() {
    let mut test = FsckTest::new().await;

    {
        let fs = test.filesystem();
        let store = fs.root_store();
        let root_directory =
            Directory::open(&store, store.root_directory_object_id()).await.expect("open failed");

        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        root_directory
            .insert_child(&mut transaction, "a", 10, ObjectDescriptor::Directory)
            .await
            .expect("insert_child failed");
        root_directory
            .insert_child(&mut transaction, "b", 10, ObjectDescriptor::File)
            .await
            .expect("insert_child failed");
        transaction.commit().await.expect("commit transaction failed");
    }

    test.remount().await.expect("Remount failed");
    test.run(TestOptions::default()).await.expect_err("Fsck should fail");
    assert_matches!(
        test.errors()[..],
        [FsckIssue::Error(FsckError::ConflictingTypeForLink(..)), ..]
    );
}

#[fasync::run_singlethreaded(test)]
async fn test_volume_in_child_store() {
    let mut test = FsckTest::new().await;

    let store_id = {
        let fs = test.filesystem();
        let root_volume = root_volume(fs.clone()).await.unwrap();
        let store = root_volume.new_volume("vol", Some(test.get_crypt())).await.unwrap();
        let root_directory =
            Directory::open(&store, store.root_directory_object_id()).await.expect("open failed");

        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        root_directory
            .insert_child(&mut transaction, "a", 10, ObjectDescriptor::Volume)
            .await
            .expect("Create child failed");
        transaction.commit().await.expect("commit transaction failed");
        store.store_object_id()
    };

    test.remount().await.expect("Remount failed");
    test.run(TestOptions { volume_store_id: Some(store_id), ..Default::default() })
        .await
        .expect_err("Fsck should fail");
    assert_matches!(test.errors()[..], [FsckIssue::Error(FsckError::VolumeInChildStore(..)), ..]);
}

#[fasync::run_singlethreaded(test)]
async fn test_children_on_file() {
    let mut test = FsckTest::new().await;

    let store_id = {
        let fs = test.filesystem();
        let root_volume = root_volume(fs.clone()).await.unwrap();
        let store = root_volume.new_volume("vol", Some(test.get_crypt())).await.unwrap();
        let root_directory =
            Directory::open(&store, store.root_directory_object_id()).await.expect("open failed");

        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        let object_id = root_directory
            .create_child_file(&mut transaction, "a'")
            .await
            .expect("Create child failed")
            .object_id();
        transaction.commit().await.expect("commit transaction failed");

        install_items_in_store(
            &fs,
            store.as_ref(),
            vec![Item::new(
                ObjectKey::child(object_id, "foo"),
                ObjectValue::Child { object_id: 11, object_descriptor: ObjectDescriptor::File },
            )],
        )
        .await;
        store.store_object_id()
    };

    test.remount().await.expect("Remount failed");
    test.run(TestOptions { volume_store_id: Some(store_id), ..Default::default() })
        .await
        .expect_err("Fsck should fail");
    assert_matches!(test.errors()[..], [FsckIssue::Error(FsckError::FileHasChildren(..)), ..]);
}

#[fasync::run_singlethreaded(test)]
async fn test_attribute_on_directory() {
    let mut test = FsckTest::new().await;

    let store_id = {
        let fs = test.filesystem();
        let root_volume = root_volume(fs.clone()).await.unwrap();
        let store = root_volume.new_volume("vol", Some(test.get_crypt())).await.unwrap();

        install_items_in_store(
            &fs,
            store.as_ref(),
            vec![Item::new(
                ObjectKey::attribute(store.root_directory_object_id(), 1, AttributeKey::Size),
                ObjectValue::attribute(100),
            )],
        )
        .await;
        store.store_object_id()
    };

    test.remount().await.expect("Remount failed");
    test.run(TestOptions { volume_store_id: Some(store_id), ..Default::default() })
        .await
        .expect_err("Fsck should fail");
    assert_matches!(test.errors()[..], [FsckIssue::Error(FsckError::AttributeOnDirectory(..)), ..]);
}

#[fasync::run_singlethreaded(test)]
async fn test_orphaned_attribute() {
    let mut test = FsckTest::new().await;

    let store_id = {
        let fs = test.filesystem();
        let root_volume = root_volume(fs.clone()).await.unwrap();
        let store = root_volume.new_volume("vol", Some(test.get_crypt())).await.unwrap();

        install_items_in_store(
            &fs,
            store.as_ref(),
            vec![Item::new(
                ObjectKey::attribute(10, 1, AttributeKey::Size),
                ObjectValue::attribute(100),
            )],
        )
        .await;
        store.store_object_id()
    };

    test.remount().await.expect("Remount failed");
    test.run(TestOptions { volume_store_id: Some(store_id), ..Default::default() })
        .await
        .expect_err("Fsck should fail");
    assert_matches!(
        test.errors()[..],
        [FsckIssue::Warning(FsckWarning::OrphanedAttribute(..)), ..]
    );
}

#[fasync::run_singlethreaded(test)]
async fn test_records_for_tombstoned_object() {
    let mut test = FsckTest::new().await;

    let store_id = {
        let fs = test.filesystem();
        let root_volume = root_volume(fs.clone()).await.unwrap();
        let store = root_volume.new_volume("vol", Some(test.get_crypt())).await.unwrap();

        install_items_in_store(
            &fs,
            store.as_ref(),
            vec![
                Item::new(ObjectKey::object(10), ObjectValue::None),
                Item::new(
                    ObjectKey::attribute(10, 1, AttributeKey::Size),
                    ObjectValue::attribute(100),
                ),
            ],
        )
        .await;
        store.store_object_id()
    };

    test.remount().await.expect("Remount failed");
    test.run(TestOptions { volume_store_id: Some(store_id), ..Default::default() })
        .await
        .expect_err("Fsck should fail");
    assert_matches!(
        test.errors()[..],
        [FsckIssue::Error(FsckError::TombstonedObjectHasRecords(..)), ..]
    );
}

#[fasync::run_singlethreaded(test)]
async fn test_invalid_object_in_store() {
    let mut test = FsckTest::new().await;

    let store_id = {
        let fs = test.filesystem();
        let root_volume = root_volume(fs.clone()).await.unwrap();
        let store = root_volume.new_volume("vol", Some(test.get_crypt())).await.unwrap();

        install_items_in_store(
            &fs,
            store.as_ref(),
            vec![Item::new(ObjectKey::object(INVALID_OBJECT_ID), ObjectValue::Some)],
        )
        .await;
        store.store_object_id()
    };

    test.remount().await.expect("Remount failed");
    test.run(TestOptions { volume_store_id: Some(store_id), ..Default::default() })
        .await
        .expect_err("Fsck should fail");
    assert_matches!(
        test.errors()[..],
        [FsckIssue::Warning(FsckWarning::InvalidObjectIdInStore(..)), ..]
    );
}

#[fasync::run_singlethreaded(test)]
async fn test_invalid_child_in_store() {
    let mut test = FsckTest::new().await;

    {
        let fs = test.filesystem();
        let store = fs.root_store();
        let root_directory =
            Directory::open(&store, store.root_directory_object_id()).await.expect("open failed");

        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        root_directory
            .insert_child(&mut transaction, "a", INVALID_OBJECT_ID, ObjectDescriptor::File)
            .await
            .expect("Insert child failed");
        transaction.commit().await.expect("commit transaction failed");
    }

    test.remount().await.expect("Remount failed");
    test.run(TestOptions::default()).await.expect_err("Fsck should fail");
    assert_matches!(
        test.errors()[..],
        [FsckIssue::Warning(FsckWarning::InvalidObjectIdInStore(..)), ..]
    );
}

#[fasync::run_singlethreaded(test)]
async fn test_link_cycle() {
    let mut test = FsckTest::new().await;

    {
        let fs = test.filesystem();
        let store = fs.root_store();
        let root_directory =
            Directory::open(&store, store.root_directory_object_id()).await.expect("open failed");

        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        let parent = root_directory
            .create_child_dir(&mut transaction, "a")
            .await
            .expect("Create child failed");
        let child =
            parent.create_child_dir(&mut transaction, "b").await.expect("Create child failed");
        child
            .insert_child(&mut transaction, "c", parent.object_id(), ObjectDescriptor::Directory)
            .await
            .expect("Insert child failed");
        transaction.commit().await.expect("commit transaction failed");
    }

    test.remount().await.expect("Remount failed");
    test.run(TestOptions::default()).await.expect_err("Fsck should fail");
    assert_matches!(
        test.errors()[..],
        [
            FsckIssue::Error(FsckError::MultipleLinksToDirectory(..)),
            FsckIssue::Error(FsckError::SubDirCountMismatch(..)),
            FsckIssue::Error(FsckError::ObjectCountMismatch(..)),
            FsckIssue::Error(FsckError::LinkCycle(..)),
            ..
        ]
    );
}

#[fasync::run_singlethreaded(test)]
async fn test_file_length_mismatch() {
    let mut test = FsckTest::new().await;

    {
        let fs = test.filesystem();
        let device = fs.device();
        let store = fs.root_store();

        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        let handle =
            ObjectStore::create_object(&store, &mut transaction, HandleOptions::default(), None)
                .await
                .expect("create object failed");
        transaction.commit().await.expect("commit transaction failed");

        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        let buf = device.allocate_buffer(1);
        handle.txn_write(&mut transaction, 1_048_576, buf.as_ref()).await.expect("write failed");
        transaction.commit().await.expect("commit transaction failed");

        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        transaction.add(
            store.store_object_id(),
            Mutation::replace_or_insert_object(
                ObjectKey::attribute(handle.object_id(), handle.attribute_id(), AttributeKey::Size),
                ObjectValue::attribute(123),
            ),
        );
        transaction.add(
            store.store_object_id(),
            Mutation::replace_or_insert_object(
                ObjectKey::object(handle.object_id()),
                ObjectValue::Object {
                    kind: ObjectKind::File { refs: 1, allocated_size: 123 },
                    attributes: ObjectAttributes {
                        creation_time: Timestamp::now(),
                        modification_time: Timestamp::now(),
                    },
                },
            ),
        );
        transaction.commit().await.expect("commit transaction failed");
    }

    test.remount().await.expect("Remount failed");
    test.run(TestOptions::default()).await.expect_err("Fsck should fail");
    assert_matches!(
        test.errors()[..],
        [
            FsckIssue::Error(FsckError::ExtentExceedsLength(..)),
            FsckIssue::Error(FsckError::AllocatedSizeMismatch(..)),
            ..
        ]
    );
}

#[fasync::run_singlethreaded(test)]
async fn test_spurious_extents() {
    let mut test = FsckTest::new().await;
    const SPURIOUS_OFFSET: u64 = 100 << 20;

    let store_id = {
        let fs = test.filesystem();
        let root_volume = root_volume(fs.clone()).await.unwrap();
        let store = root_volume.new_volume("vol", Some(test.get_crypt())).await.unwrap();

        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        transaction.add(
            store.store_object_id(),
            Mutation::insert_object(
                ObjectKey::extent(555, 0, 0..4096),
                ObjectValue::Extent(ExtentValue::new(SPURIOUS_OFFSET)),
            ),
        );
        transaction.add(
            store.store_object_id(),
            Mutation::insert_object(
                ObjectKey::extent(store.root_directory_object_id(), 0, 0..4096),
                ObjectValue::Extent(ExtentValue::new(SPURIOUS_OFFSET)),
            ),
        );
        transaction.commit().await.expect("commit failed");
        store.store_object_id()
    };

    test.remount().await.expect("Remount failed");
    test.run(TestOptions { volume_store_id: Some(store_id), ..Default::default() })
        .await
        .expect_err("Fsck should fail");
    let mut found = 0;
    for e in test.errors() {
        match e {
            FsckIssue::Warning(FsckWarning::ExtentForDirectory(..)) => found |= 1,
            FsckIssue::Warning(FsckWarning::ExtentForNonexistentObject(..)) => found |= 2,
            _ => {}
        }
    }
    assert_eq!(found, 3, "Missing expected errors: {:?}", test.errors());
}
