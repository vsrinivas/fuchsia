// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        crypt::Crypt,
        lsm_tree::{
            simple_persistent_layer::SimplePersistentLayer,
            skip_list_layer::SkipListLayer,
            types::{
                BoxedLayerIterator, Item, Key, Layer, LayerIterator, OrdUpperBound, RangeKey, Value,
            },
        },
        object_handle::{ObjectHandle, ObjectHandleExt, INVALID_OBJECT_ID},
        object_store::{
            allocator::{
                Allocator, AllocatorKey, AllocatorValue, CoalescingIterator, SimpleAllocator,
            },
            constants::{SUPER_BLOCK_A_OBJECT_ID, SUPER_BLOCK_B_OBJECT_ID},
            extent_record::{ExtentKey, ExtentValue},
            filesystem::{Filesystem, FxFilesystem},
            fsck::errors::{FsckError, FsckFatal, FsckIssue, FsckWarning},
            object_record::{ObjectKey, ObjectValue},
            transaction::{LockKey, TransactionHandler},
            volume::root_volume,
            HandleOptions, ObjectStore, StoreInfo, MAX_STORE_INFO_SERIALIZED_SIZE,
        },
        serialized_types::VersionedLatest,
    },
    anyhow::{anyhow, Context, Error},
    futures::try_join,
    std::{
        ops::Bound,
        sync::{
            atomic::{AtomicU64, Ordering},
            Arc,
        },
    },
};

pub mod errors;

mod store_scanner;

#[cfg(test)]
mod tests;

pub struct FsckOptions<F: Fn(&FsckIssue)> {
    /// Whether to fail fsck if any warnings are encountered.
    pub fail_on_warning: bool,
    // Whether to halt after the first error encountered (fatal or not).
    pub halt_on_error: bool,
    /// Whether to perform slower, more complete checks.
    pub do_slow_passes: bool,
    /// A callback to be invoked for each detected error, e.g. to log the error.
    pub on_error: F,
    /// Whether to be noisy as we do checks.
    pub verbose: bool,
}

pub fn default_options() -> FsckOptions<impl Fn(&FsckIssue)> {
    FsckOptions {
        fail_on_warning: false,
        halt_on_error: false,
        do_slow_passes: true,
        on_error: |err: &FsckIssue| {
            if err.is_error() {
                log::error!("{:?}", err.to_string())
            } else {
                log::warn!("{:?}", err.to_string())
            }
        },
        verbose: false,
    }
}

/// Verifies the integrity of Fxfs.  See errors.rs for a list of checks performed.
// TODO(fxbug.dev/87381): add checks for:
//  + The root parent object store ID and root object store ID must not conflict with any other
//    stores or the allocator.
// TODO(csuter): This currently takes a write lock on the filesystem.  It would be nice if we could
// take a snapshot.
pub async fn fsck(
    filesystem: &Arc<FxFilesystem>,
    crypt: Option<Arc<dyn Crypt>>,
) -> Result<(), Error> {
    fsck_with_options(filesystem, crypt, default_options()).await
}

pub async fn fsck_with_options<F: Fn(&FsckIssue)>(
    filesystem: &Arc<FxFilesystem>,
    crypt: Option<Arc<dyn Crypt>>,
    options: FsckOptions<F>,
) -> Result<(), Error> {
    log::info!("Starting fsck");
    let _guard = filesystem.write_lock(&[LockKey::Filesystem]).await;

    let mut fsck = Fsck::new(options);

    let object_manager = filesystem.object_manager();
    let super_block = filesystem.super_block();

    // Scan the root parent object store.
    let mut root_objects = vec![super_block.root_store_object_id, super_block.journal_object_id];
    root_objects.append(&mut object_manager.root_store().parent_objects());
    fsck.verbose("Scanning root parent store...");
    store_scanner::scan_store(&fsck, object_manager.root_parent_store().as_ref(), &root_objects)
        .await?;
    fsck.verbose("Scanning root parent store done");

    let root_store = &object_manager.root_store();
    let mut root_store_root_objects = Vec::new();
    root_store_root_objects.append(&mut vec![
        super_block.allocator_object_id,
        SUPER_BLOCK_A_OBJECT_ID,
        SUPER_BLOCK_B_OBJECT_ID,
    ]);
    root_store_root_objects.append(&mut root_store.root_objects());

    let root_volume = root_volume(filesystem).await?;
    let volume_directory = root_volume.volume_directory();
    let layer_set = volume_directory.store().tree().layer_set();
    let mut merger = layer_set.merger();
    let mut iter = volume_directory.iter(&mut merger).await?;

    // TODO(csuter): We could maybe iterate over stores concurrently.
    while let Some((name, store_id, _)) = iter.get() {
        fsck.verbose(format!("Scanning volume \"{}\" (id {})...", name, store_id));
        fsck.check_child_store(&filesystem, store_id, &mut root_store_root_objects, crypt.clone())
            .await?;
        iter.advance().await?;
        fsck.verbose("Scanning volume done");
    }

    // TODO(csuter): It's a bit crude how details of SimpleAllocator are leaking here. Is there
    // a better way?
    let allocator = filesystem.allocator().as_any().downcast::<SimpleAllocator>().unwrap();
    root_store_root_objects.append(&mut allocator.parent_objects());

    if fsck.options.do_slow_passes {
        // Scan each layer file for the allocator.
        let layer_set = allocator.tree().immutable_layer_set();
        fsck.verbose(format!("Checking {} layers for allocator...", layer_set.layers.len()));
        for layer in layer_set.layers {
            if let Some(handle) = layer.handle() {
                fsck.verbose(format!(
                    "Layer file {} for allocator is {} bytes",
                    handle.object_id(),
                    handle.get_size()
                ));
            }
            fsck.check_layer_file_contents(
                allocator.object_id(),
                layer.handle().map(|h| h.object_id()).unwrap_or(INVALID_OBJECT_ID),
                layer.clone(),
            )
            .await?;
        }
        fsck.verbose("Checking layers done");
    }

    // Finally scan the root object store.
    fsck.verbose("Scanning root object store...");
    store_scanner::scan_store(&fsck, root_store.as_ref(), &root_store_root_objects).await?;
    fsck.verbose("Scanning root object store done");

    // Now compare our regenerated allocation map with what we actually have.
    fsck.verbose("Verifying allocations...");
    let layer_set = allocator.tree().layer_set();
    let mut merger = layer_set.merger();
    let iter = merger.seek(Bound::Unbounded).await?;
    let mut actual = CoalescingIterator::new(Box::new(iter)).await?;
    let mut expected =
        CoalescingIterator::new(fsck.allocations.seek(Bound::Unbounded).await?).await?;
    let mut allocated_bytes = 0;
    let mut extra_allocations: Vec<errors::Allocation> = vec![];
    let bs = filesystem.block_size();
    while let Some(actual_item) = actual.get() {
        if actual_item.key.device_range.start % bs > 0 || actual_item.key.device_range.end % bs > 0
        {
            fsck.error(FsckError::MisalignedAllocation(actual_item.into()))?;
        } else if actual_item.key.device_range.start >= actual_item.key.device_range.end {
            fsck.error(FsckError::MalformedAllocation(actual_item.into()))?;
        }
        match expected.get() {
            None => extra_allocations.push(actual_item.into()),
            Some(expected_item) => {
                let r = &expected_item.key.device_range;
                allocated_bytes += (r.end - r.start) as i64;
                if actual_item != expected_item {
                    fsck.error(FsckError::AllocationMismatch(
                        expected_item.into(),
                        actual_item.into(),
                    ))?;
                }
            }
        }
        try_join!(actual.advance(), expected.advance())?;
    }
    fsck.verbose(format!(
        "{}/{} bytes allocated",
        allocated_bytes,
        filesystem.device().block_count() * filesystem.device().block_size() as u64
    ));
    if !extra_allocations.is_empty() {
        fsck.error(FsckError::ExtraAllocations(extra_allocations))?;
    }
    if let Some(item) = expected.get() {
        fsck.error(FsckError::MissingAllocation(item.into()))?;
    }
    if allocated_bytes as u64 != allocator.get_allocated_bytes() {
        fsck.error(FsckError::AllocatedBytesMismatch(
            allocated_bytes as u64,
            allocator.get_allocated_bytes(),
        ))?;
    }
    let errors = fsck.errors();
    let warnings = fsck.warnings();
    if errors > 0 || (fsck.options.fail_on_warning && warnings > 0) {
        Err(anyhow!("Fsck encountered {} errors, {} warnings", errors, warnings))
    } else {
        if warnings > 0 {
            log::warn!("Fsck encountered {} warnings", warnings);
        }
        Ok(())
    }
}

trait KeyExt: PartialEq {
    fn overlaps(&self, other: &Self) -> bool {
        self == other
    }
}

// Without https://rust-lang.github.io/rfcs/1210-impl-specialization.html, we can't have one
// behaviour for RangeKey and another for Key, unless we specify all possible Keys.
impl KeyExt for ObjectKey {}

impl<K: RangeKey + PartialEq> KeyExt for K {
    fn overlaps(&self, other: &Self) -> bool {
        RangeKey::overlaps(self, other)
    }
}

struct Fsck<F: Fn(&FsckIssue)> {
    options: FsckOptions<F>,
    // A list of allocations generated based on all extents found across all scanned object stores.
    allocations: Arc<SkipListLayer<AllocatorKey, AllocatorValue>>,
    errors: AtomicU64,
    warnings: AtomicU64,
}

impl<F: Fn(&FsckIssue)> Fsck<F> {
    fn new(options: FsckOptions<F>) -> Self {
        Fsck {
            options,
            // TODO(csuter): fix magic number
            allocations: SkipListLayer::new(2048),
            errors: AtomicU64::new(0),
            warnings: AtomicU64::new(0),
        }
    }

    // Log if in verbose mode.
    fn verbose(&self, message: impl AsRef<str>) {
        if self.options.verbose {
            log::info!("fsck: {}", message.as_ref());
        }
    }

    fn errors(&self) -> u64 {
        self.errors.load(Ordering::Relaxed)
    }

    fn warnings(&self) -> u64 {
        self.warnings.load(Ordering::Relaxed)
    }

    fn assert<V>(&self, res: Result<V, Error>, error: FsckFatal) -> Result<V, Error> {
        if res.is_err() {
            (self.options.on_error)(&FsckIssue::Fatal(error.clone()));
            return Err(anyhow!(format!("{:?}", error)).context(res.err().unwrap()));
        }
        res
    }

    fn warning(&self, error: FsckWarning) -> Result<(), Error> {
        (self.options.on_error)(&FsckIssue::Warning(error.clone()));
        self.warnings.fetch_add(1, Ordering::Relaxed);
        Ok(())
    }

    fn error(&self, error: FsckError) -> Result<(), Error> {
        (self.options.on_error)(&FsckIssue::Error(error.clone()));
        self.errors.fetch_add(1, Ordering::Relaxed);
        if self.options.halt_on_error {
            Err(anyhow!(format!("{:?}", error)))
        } else {
            Ok(())
        }
    }

    fn fatal(&self, error: FsckFatal) -> Result<(), Error> {
        (self.options.on_error)(&FsckIssue::Fatal(error.clone()));
        Err(anyhow!(format!("{:?}", error)))
    }

    async fn check_child_store(
        &mut self,
        filesystem: &FxFilesystem,
        store_id: u64,
        root_store_root_objects: &mut Vec<u64>,
        crypt: Option<Arc<dyn Crypt>>,
    ) -> Result<(), Error> {
        // TODO(fxbug.dev/92275): Support checking the extents of a child store when we don't have
        // the crypt object.
        let crypt = crypt.expect("fsck without crypt is not yet supported");

        let root_store = filesystem.root_store();

        // Manually open the store so we can do our own validation.  Later, we will call open_store
        // to get a regular ObjectStore wrapper.
        let handle = self.assert(
            ObjectStore::open_object(&root_store, store_id, HandleOptions::default(), None).await,
            FsckFatal::MissingStoreInfo(store_id),
        )?;
        let (object_layer_file_object_ids, extent_layer_file_object_ids) = {
            let info = if handle.get_size() > 0 {
                let serialized_info = handle.contents(MAX_STORE_INFO_SERIALIZED_SIZE).await?;
                let mut cursor = std::io::Cursor::new(&serialized_info[..]);
                let (store_info, _version) = self.assert(
                    StoreInfo::deserialize_with_version(&mut cursor)
                        .context("Failed to deserialize StoreInfo"),
                    FsckFatal::MalformedStore(store_id),
                )?;
                store_info
            } else {
                // The store_info will be absent for a newly created and empty object store.
                StoreInfo::default()
            };
            // We don't replay the store ReplayInfo here, since it doesn't affect what we
            // want to check (mainly the existence of the layer files).  If that changes,
            // we'll need to update this.
            (info.object_tree_layers.clone(), info.extent_tree_layers.clone())
        };
        self.verbose(format!(
            "Store {} has {} object tree layers, {} extent tree layers",
            store_id,
            object_layer_file_object_ids.len(),
            extent_layer_file_object_ids.len()
        ));
        for layer_file_object_id in object_layer_file_object_ids {
            self.check_layer_file::<ObjectKey, ObjectValue>(
                &root_store,
                store_id,
                layer_file_object_id,
                crypt.as_ref(),
            )
            .await?;
        }
        for layer_file_object_id in extent_layer_file_object_ids {
            self.check_layer_file::<ExtentKey, ExtentValue>(
                &root_store,
                store_id,
                layer_file_object_id,
                crypt.as_ref(),
            )
            .await?;
        }

        // TODO(fxbug.dev/92275): This will panic if the store is already unlocked.
        let store = filesystem.object_manager().open_store(store_id, crypt).await?;

        store_scanner::scan_store(self, store.as_ref(), &store.root_objects()).await?;
        let mut parent_objects = store.parent_objects();
        root_store_root_objects.append(&mut parent_objects);
        Ok(())
    }

    async fn check_layer_file<
        K: Key + KeyExt + OrdUpperBound + std::fmt::Debug,
        V: Value + std::fmt::Debug,
    >(
        &self,
        root_store: &Arc<ObjectStore>,
        store_object_id: u64,
        layer_file_object_id: u64,
        crypt: &dyn Crypt,
    ) -> Result<(), Error> {
        let layer_file = self.assert(
            ObjectStore::open_object(
                root_store,
                layer_file_object_id,
                HandleOptions::default(),
                Some(crypt),
            )
            .await,
            FsckFatal::MissingLayerFile(store_object_id, layer_file_object_id),
        )?;
        if self.options.do_slow_passes {
            self.verbose(format!(
                "Layer file {} for store {} is {} bytes",
                layer_file_object_id,
                store_object_id,
                layer_file.get_size()
            ));
            // TODO(ripper): When we have multiple layer file formats, we'll need some way of
            // detecting which format we are dealing with.  For now, we just assume it's
            // SimplePersistentLayer.
            let layer = SimplePersistentLayer::open(layer_file).await?;
            self.check_layer_file_contents(
                store_object_id,
                layer_file_object_id,
                layer as Arc<dyn Layer<K, V>>,
            )
            .await?;
        }
        Ok(())
    }

    async fn check_layer_file_contents<
        K: Key + KeyExt + OrdUpperBound + std::fmt::Debug,
        V: Value + std::fmt::Debug,
    >(
        &self,
        store_object_id: u64,
        layer_file_object_id: u64,
        layer: Arc<dyn Layer<K, V>>,
    ) -> Result<(), Error> {
        let mut iter: BoxedLayerIterator<'_, K, V> = self.assert(
            layer.seek(Bound::Unbounded).await,
            FsckFatal::MalformedLayerFile(store_object_id, layer_file_object_id),
        )?;

        let mut last_item: Option<Item<K, V>> = None;
        while let Some(item) = iter.get() {
            if let Some(last) = last_item {
                if !last.key.cmp_upper_bound(&item.key).is_le() {
                    self.fatal(FsckFatal::MisOrderedLayerFile(
                        store_object_id,
                        layer_file_object_id,
                    ))?;
                }
                if last.key.overlaps(&item.key) {
                    self.fatal(FsckFatal::OverlappingKeysInLayerFile(
                        store_object_id,
                        layer_file_object_id,
                        item.into(),
                        last.as_item_ref().into(),
                    ))?;
                }
            }
            last_item = Some(item.cloned());
            self.assert(
                iter.advance().await,
                FsckFatal::MalformedLayerFile(store_object_id, layer_file_object_id),
            )?;
        }
        Ok(())
    }
}
