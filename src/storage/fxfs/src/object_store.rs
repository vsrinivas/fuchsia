// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod allocator;
mod constants;
pub mod directory;
pub mod filesystem;
pub mod fsck;
mod graveyard;
mod journal;
mod merge;
pub mod object_manager;
mod record;
#[cfg(test)]
mod testing;
pub mod transaction;

pub use directory::Directory;
pub use filesystem::FxFilesystem;
pub use record::{ObjectDescriptor, Timestamp};

use {
    crate::{
        errors::FxfsError,
        lsm_tree::{layers_from_handles, types::LayerIterator, LSMTree},
        object_handle::{
            ObjectHandle, ObjectHandleExt, ObjectProperties, Writer, INVALID_OBJECT_ID,
        },
        object_store::{
            filesystem::{Filesystem, Mutations},
            journal::{checksum_list::ChecksumList, fletcher64},
            object_manager::ObjectFlush,
            record::{
                Checksums, ExtentKey, ExtentValue, ObjectAttributes, ObjectItem, ObjectKey,
                ObjectKind, ObjectValue, DEFAULT_DATA_ATTRIBUTE_ID,
            },
            transaction::{
                AssocObj, AssociatedObject, LockKey, Mutation, ObjectStoreMutation, Operation,
                Options, StoreInfoMutation, Transaction,
            },
        },
    },
    allocator::Allocator,
    anyhow::{anyhow, bail, Context, Error},
    async_trait::async_trait,
    bincode::{deserialize_from, serialize_into},
    futures::{future::BoxFuture, stream::FuturesUnordered, try_join, FutureExt, TryStreamExt},
    interval_tree::utils::RangeOps,
    once_cell::sync::OnceCell,
    serde::{Deserialize, Serialize},
    std::{
        cmp::min,
        convert::TryFrom,
        ops::{Bound, Range},
        sync::{
            atomic::{self, AtomicBool, AtomicU64},
            Arc, Mutex, Weak,
        },
        time::{Duration, SystemTime, UNIX_EPOCH},
    },
    storage_device::{
        buffer::{Buffer, BufferRef, MutableBufferRef},
        Device,
    },
};

// TODO(jfsulliv): This probably could have a better home.
pub fn current_time() -> Timestamp {
    SystemTime::now().duration_since(UNIX_EPOCH).unwrap_or(Duration::ZERO).into()
}

// StoreInfo stores information about the object store.  This is stored within the parent object
// store, and is used, for example, to get the persistent layer objects.
#[derive(Clone, Debug, Default, Serialize, Deserialize)]
pub struct StoreInfo {
    // The last used object ID.  Note that this field is not accurate in memory; ObjectStore's
    // last_object_id field is the one to use in that case.
    last_object_id: u64,

    // Object ids for layers.  TODO(csuter): need a layer of indirection here so we can
    // support snapshots.
    layers: Vec<u64>,

    // The object ID for the root directory.
    root_directory_object_id: u64,

    // The object ID for the graveyard.
    // TODO(csuter): Move this out of here.  This can probably be a child of the root directory.
    graveyard_directory_object_id: u64,
}

// TODO(csuter): We should test or put checks in place to ensure this limit isn't exceeded.  It
// will likely involve placing limits on the maximum number of layers.
const MAX_STORE_INFO_SERIALIZED_SIZE: usize = 131072;

#[derive(Default)]
pub struct HandleOptions {
    /// If true, don't COW, write to blocks that are already allocated.
    pub overwrite: bool,

    /// If true, transactions used by this handle will skip journal space checks.
    pub skip_journal_checks: bool,
}

/// An object store supports a file like interface for objects.  Objects are keyed by a 64 bit
/// identifier.  And object store has to be backed by a parent object store (which stores metadata
/// for the object store).  The top-level object store (a.k.a. the root parent object store) is
/// in-memory only.
pub struct ObjectStore {
    parent_store: Option<Arc<ObjectStore>>,
    store_object_id: u64,
    device: Arc<dyn Device>,
    block_size: u32,
    filesystem: Weak<dyn Filesystem>,
    last_object_id: AtomicU64,
    store_info: Mutex<Option<StoreInfo>>,
    tree: LSMTree<ObjectKey, ObjectValue>,

    // When replaying the journal, the store cannot read StoreInfo until the whole journal
    // has been replayed, so during that time, store_info_handle will be None and records
    // just get sent to the tree. Once the journal has been replayed, we can open the store
    // and load all the other layer information.
    store_info_handle: OnceCell<StoreObjectHandle<ObjectStore>>,
}

impl ObjectStore {
    fn new(
        parent_store: Option<Arc<ObjectStore>>,
        store_object_id: u64,
        filesystem: Arc<dyn Filesystem>,
        store_info: Option<StoreInfo>,
        tree: LSMTree<ObjectKey, ObjectValue>,
    ) -> Arc<ObjectStore> {
        let device = filesystem.device();
        let block_size = device.block_size();
        let store = Arc::new(ObjectStore {
            parent_store,
            store_object_id,
            device,
            block_size,
            filesystem: Arc::downgrade(&filesystem),
            last_object_id: AtomicU64::new(0),
            store_info: Mutex::new(store_info),
            tree,
            store_info_handle: OnceCell::new(),
        });
        store
    }

    pub fn new_empty(
        parent_store: Option<Arc<ObjectStore>>,
        store_object_id: u64,
        filesystem: Arc<dyn Filesystem>,
    ) -> Arc<Self> {
        Self::new(
            parent_store,
            store_object_id,
            filesystem,
            Some(StoreInfo::default()),
            LSMTree::new(merge::merge),
        )
    }

    pub fn device(&self) -> &Arc<dyn Device> {
        &self.device
    }

    pub fn block_size(&self) -> u32 {
        self.device.block_size()
    }

    pub fn filesystem(&self) -> Arc<dyn Filesystem> {
        self.filesystem.upgrade().unwrap()
    }

    pub fn store_object_id(&self) -> u64 {
        self.store_object_id
    }

    pub fn tree(&self) -> &LSMTree<ObjectKey, ObjectValue> {
        &self.tree
    }

    pub fn root_directory_object_id(&self) -> u64 {
        self.store_info.lock().unwrap().as_ref().unwrap().root_directory_object_id
    }

    pub fn set_root_directory_object_id<'a>(&'a self, transaction: &mut Transaction<'a>, oid: u64) {
        let mut store_info = self.txn_get_store_info(transaction);
        store_info.root_directory_object_id = oid;
        transaction.add_with_object(
            self.store_object_id,
            Mutation::store_info(store_info),
            AssocObj::Borrowed(self),
        );
    }

    pub fn graveyard_directory_object_id(&self) -> u64 {
        self.store_info.lock().unwrap().as_ref().unwrap().graveyard_directory_object_id
    }

    pub fn set_graveyard_directory_object_id<'a>(
        &'a self,
        transaction: &mut Transaction<'a>,
        oid: u64,
    ) {
        let mut store_info = self.txn_get_store_info(transaction);
        store_info.graveyard_directory_object_id = oid;
        transaction.add_with_object(
            self.store_object_id,
            Mutation::store_info(store_info),
            AssocObj::Borrowed(self),
        );
    }

    pub async fn create_child_store<'a>(
        self: &'a Arc<ObjectStore>,
        transaction: &mut Transaction<'a>,
    ) -> Result<Arc<ObjectStore>, Error> {
        let object_id = self.get_next_object_id();
        self.create_child_store_with_id(transaction, object_id).await
    }

    async fn create_child_store_with_id<'a>(
        self: &'a Arc<Self>,
        transaction: &mut Transaction<'a>,
        object_id: u64,
    ) -> Result<Arc<ObjectStore>, Error> {
        self.ensure_open().await?;
        // TODO(csuter): if the transaction rolls back, we need to delete the store.
        let handle = ObjectStore::create_object_with_id(
            self,
            transaction,
            object_id,
            HandleOptions::default(),
        )
        .await?;
        let fs = self.filesystem.upgrade().unwrap();
        let store = Self::new_empty(Some(self.clone()), handle.object_id(), fs.clone());
        assert!(store.store_info_handle.set(handle).is_ok());
        fs.object_manager().add_store(store.clone());
        Ok(store)
    }

    pub async fn open_object<S: AsRef<ObjectStore>>(
        owner: &Arc<S>,
        object_id: u64,
        options: HandleOptions,
    ) -> Result<StoreObjectHandle<S>, Error> {
        let store = owner.as_ref().as_ref();
        store.ensure_open().await?;
        let item = store
            .tree
            .find(&ObjectKey::attribute(object_id, DEFAULT_DATA_ATTRIBUTE_ID))
            .await?
            .ok_or(FxfsError::NotFound)?;
        if let ObjectValue::Attribute { size } = item.value {
            Ok(StoreObjectHandle {
                owner: owner.clone(),
                object_id,
                attribute_id: DEFAULT_DATA_ATTRIBUTE_ID,
                block_size: store.block_size.into(),
                size: Mutex::new(size),
                options,
                pending_properties: Mutex::new(PendingPropertyUpdates::default()),
                trace: AtomicBool::new(false),
            })
        } else {
            bail!(FxfsError::Inconsistent);
        }
    }

    async fn create_object_with_id<S: AsRef<ObjectStore>>(
        owner: &Arc<S>,
        transaction: &mut Transaction<'_>,
        object_id: u64,
        options: HandleOptions,
    ) -> Result<StoreObjectHandle<S>, Error> {
        let store = owner.as_ref().as_ref();
        store.ensure_open().await?;
        // If the object ID was specified i.e. this hasn't come via create_object, then we
        // should update last_object_id in case the caller wants to create more objects in
        // the same transaction.
        store.update_last_object_id(object_id);
        let now = current_time();
        transaction.add(
            store.store_object_id(),
            Mutation::insert_object(
                ObjectKey::object(object_id),
                ObjectValue::file(1, 0, now.clone(), now),
            ),
        );
        transaction.add(
            store.store_object_id(),
            Mutation::insert_object(
                ObjectKey::attribute(object_id, DEFAULT_DATA_ATTRIBUTE_ID),
                ObjectValue::attribute(0),
            ),
        );
        Ok(StoreObjectHandle {
            owner: owner.clone(),
            block_size: store.block_size.into(),
            object_id,
            attribute_id: DEFAULT_DATA_ATTRIBUTE_ID,
            size: Mutex::new(0),
            options,
            pending_properties: Mutex::new(PendingPropertyUpdates::default()),
            trace: AtomicBool::new(false),
        })
    }

    pub async fn create_object<S: AsRef<ObjectStore>>(
        owner: &Arc<S>,
        mut transaction: &mut Transaction<'_>,
        options: HandleOptions,
    ) -> Result<StoreObjectHandle<S>, Error> {
        let object_id = owner.as_ref().as_ref().get_next_object_id();
        ObjectStore::create_object_with_id(owner, &mut transaction, object_id, options).await
    }

    /// Adjusts the reference count for a given object.  If the reference count reaches zero, the
    /// object is moved into the graveyard and true is returned.
    pub async fn adjust_refs(
        &self,
        transaction: &mut Transaction<'_>,
        oid: u64,
        delta: i64,
    ) -> Result<bool, Error> {
        let mut item = self.txn_get_object(transaction, oid).await?;
        let refs =
            if let ObjectValue::Object { kind: ObjectKind::File { ref mut refs, .. }, .. } =
                item.value
            {
                *refs = if delta < 0 {
                    refs.checked_sub((-delta) as u64)
                } else {
                    refs.checked_add(delta as u64)
                }
                .ok_or(anyhow!("refs underflow/overflow"))?;
                refs
            } else {
                bail!(FxfsError::NotFile);
            };
        if *refs == 0 {
            // Move the object into the graveyard.
            self.filesystem().object_manager().graveyard().unwrap().add(
                transaction,
                self.store_object_id,
                oid,
            );
            // We might still need to adjust the reference count if delta was something other than
            // -1.
            if delta != -1 {
                *refs = 1;
                transaction.add(
                    self.store_object_id,
                    Mutation::replace_or_insert_object(item.key, item.value),
                );
            }
            Ok(true)
        } else {
            transaction.add(
                self.store_object_id,
                Mutation::replace_or_insert_object(item.key, item.value),
            );
            Ok(false)
        }
    }

    // Purges an object that is in the graveyard.  This has no locking, so it's not safe to call
    // this more than once simultaneously for a given object.
    pub async fn tombstone(&self, object_id: u64) -> Result<(), Error> {
        let fs = self.filesystem();
        let mut search_key = ObjectKey::attribute(object_id, 0);
        // TODO(csuter): There should be a test that runs fsck after each transaction.
        loop {
            let mut transaction = fs
                .clone()
                .new_transaction(&[], Options { skip_space_checks: true, ..Default::default() })
                .await?;
            let next_key = self.delete_some(&mut transaction, &search_key).await?;
            transaction.commit().await;
            search_key = if let Some(next_key) = next_key {
                next_key
            } else {
                break;
            };
        }
        Ok(())
    }

    // Makes progress on deleting part of a file but stops before a transaction gets too big.
    async fn delete_some(
        &self,
        transaction: &mut Transaction<'_>,
        search_key: &ObjectKey,
    ) -> Result<Option<ObjectKey>, Error> {
        let layer_set = self.tree.layer_set();
        let mut merger = layer_set.merger();
        let allocator = self.allocator();
        let mut iter = merger.seek(Bound::Included(search_key)).await?;
        // Loop over the extents and deallocate them.
        while let Some(item) = iter.get() {
            if item.key.object_id != search_key.object_id {
                break;
            }
            if let Some((
                _,
                attribute_id,
                ExtentKey { range },
                ExtentValue { device_offset: Some((device_offset, _)) },
            )) = item.into()
            {
                let device_range = *device_offset..*device_offset + (range.end - range.start);
                allocator.deallocate(transaction, device_range).await;
                // Stop if the transaction is getting too big.  At time of writing, this threshold
                // limits transactions to about 10,000 bytes.
                const TRANSACTION_MUTATION_THRESHOLD: usize = 200;
                if transaction.mutations.len() >= TRANSACTION_MUTATION_THRESHOLD {
                    transaction.add(
                        self.store_object_id,
                        Mutation::merge_object(
                            ObjectKey::extent(search_key.object_id, attribute_id, 0..range.end),
                            ObjectValue::deleted_extent(),
                        ),
                    );
                    return Ok(Some(ObjectKey::with_extent_key(
                        search_key.object_id,
                        attribute_id,
                        ExtentKey::search_key_from_offset(range.end),
                    )));
                }
            }
            iter.advance().await?;
        }
        transaction.add(
            self.store_object_id,
            Mutation::merge_object(ObjectKey::tombstone(search_key.object_id), ObjectValue::None),
        );
        // Remove the object from the graveyard.
        self.filesystem().object_manager().graveyard().unwrap().remove(
            transaction,
            self.store_object_id,
            search_key.object_id,
        );
        Ok(None)
    }

    /// Returns all objects that exist in the parent store that pertain to this object store.
    pub fn parent_objects(&self) -> Vec<u64> {
        assert!(self.store_info_handle.get().is_some());
        let mut objects = Vec::new();
        // We should not include the ID of the store itself, since that should be referred to in the
        // volume directory.
        objects.extend_from_slice(&self.store_info.lock().unwrap().as_ref().unwrap().layers);
        objects
    }

    /// Returns root objects for this store.
    pub fn root_objects(&self) -> Vec<u64> {
        let mut objects = Vec::new();
        let store_info = self.store_info.lock().unwrap();
        if store_info.as_ref().unwrap().root_directory_object_id != INVALID_OBJECT_ID {
            objects.push(store_info.as_ref().unwrap().root_directory_object_id);
        }
        if store_info.as_ref().unwrap().graveyard_directory_object_id != INVALID_OBJECT_ID {
            objects.push(store_info.as_ref().unwrap().graveyard_directory_object_id);
        }
        objects
    }

    pub fn store_info(&self) -> StoreInfo {
        self.store_info.lock().unwrap().as_ref().unwrap().clone()
    }

    pub async fn ensure_open(&self) -> Result<(), Error> {
        if self.parent_store.is_none() || self.store_info_handle.get().is_some() {
            return Ok(());
        }
        let fs = self.filesystem();
        let _guard = fs
            .write_lock(&[LockKey::object(
                self.parent_store.as_ref().unwrap().store_object_id(),
                self.store_object_id,
            )])
            .await;
        if self.store_info_handle.get().is_some() {
            // We lost the race.
            Ok(())
        } else {
            self.open_impl().await
        }
    }

    // This returns a BoxFuture because of the cycle: open_object -> ensure_open -> open_impl ->
    // open_object.
    fn open_impl<'a>(&'a self) -> BoxFuture<'a, Result<(), Error>> {
        async move {
            let parent_store = self.parent_store.as_ref().unwrap();
            let handle = ObjectStore::open_object(
                &parent_store,
                self.store_object_id,
                HandleOptions::default(),
            )
            .await?;
            let layer_object_ids = if handle.get_size() > 0 {
                let serialized_info = handle.contents(MAX_STORE_INFO_SERIALIZED_SIZE).await?;
                let store_info: StoreInfo = deserialize_from(&serialized_info[..])
                    .context("Failed to deserialize StoreInfo")?;
                let layer_object_ids = store_info.layers.clone();
                self.update_last_object_id(store_info.last_object_id);
                *self.store_info.lock().unwrap() = Some(store_info);
                layer_object_ids
            } else {
                let mut store_info = self.store_info.lock().unwrap();
                // The store_info might be absent for a newly created and empty object store, since
                // there have been no StoreInfoMutations applied to it.
                if store_info.is_none() {
                    *store_info = Some(StoreInfo::default());
                }
                store_info.as_ref().unwrap().layers.clone()
            };
            let mut handles = Vec::new();
            for object_id in layer_object_ids {
                handles.push(
                    ObjectStore::open_object(&parent_store, object_id, HandleOptions::default())
                        .await?,
                );
            }
            self.tree.append_layers(handles.into()).await?;
            let _ = self.store_info_handle.set(handle);
            Ok(())
        }
        .boxed()
    }

    fn get_next_object_id(&self) -> u64 {
        self.last_object_id.fetch_add(1, atomic::Ordering::Relaxed) + 1
    }

    fn allocator(&self) -> Arc<dyn Allocator> {
        self.filesystem().allocator()
    }

    fn txn_get_store_info(&self, transaction: &Transaction<'_>) -> StoreInfo {
        match transaction.get_store_info(self.store_object_id) {
            None => self.store_info(),
            Some(store_info) => store_info.clone(),
        }
    }

    // If |transaction| has an impending mutation for the underlying object, returns that.
    // Otherwise, looks up the object from the tree.
    async fn txn_get_object(
        &self,
        transaction: &Transaction<'_>,
        object_id: u64,
    ) -> Result<ObjectItem, Error> {
        if let Some(ObjectStoreMutation { item, .. }) =
            transaction.get_object_mutation(self.store_object_id, ObjectKey::object(object_id))
        {
            Ok(item.clone())
        } else {
            self.tree.find(&ObjectKey::object(object_id)).await?.ok_or(anyhow!(FxfsError::NotFound))
        }
    }

    fn update_last_object_id(&self, object_id: u64) {
        let _ = self.last_object_id.fetch_update(
            atomic::Ordering::Relaxed,
            atomic::Ordering::Relaxed,
            |oid| if object_id > oid { Some(object_id) } else { None },
        );
    }

    async fn validate_mutation(
        journal_offset: u64,
        mutation: &Mutation,
        checksum_list: &mut ChecksumList,
    ) -> Result<bool, Error> {
        if let Mutation::ObjectStore(ObjectStoreMutation { item, .. }) = mutation {
            if let Some((
                _,
                _,
                ExtentKey { range },
                ExtentValue {
                    device_offset: Some((device_offset, Checksums::Fletcher(checksums))),
                },
            )) = item.as_item_ref().into()
            {
                if checksums.len() == 0 {
                    return Ok(false);
                }
                let len = if let Ok(l) = usize::try_from(range.length()) {
                    l
                } else {
                    return Ok(false);
                };
                if len % checksums.len() != 0 {
                    return Ok(false);
                }
                if (len / checksums.len()) % 4 != 0 {
                    return Ok(false);
                }
                checksum_list.push(
                    journal_offset,
                    *device_offset..*device_offset + range.length(),
                    checksums,
                );
            }
        }
        Ok(true)
    }
}

#[async_trait]
impl Mutations for ObjectStore {
    async fn apply_mutation(
        &self,
        mutation: Mutation,
        transaction: Option<&Transaction<'_>>,
        log_offset: u64,
        _assoc_obj: AssocObj<'_>,
    ) {
        // It's not safe to fully open a store until replay is fully complete (because
        // subsequent mutations could render current records invalid). The exception to
        // this is the root parent object store which contains the extents for the journal
        // file: whilst we are replaying we need to be able to track new extents for the
        // journal file so that we can read from it whilst we are replaying.
        assert!(
            transaction.is_some()
                || self.store_info_handle.get().is_none()
                || self.parent_store.is_none()
        );

        match mutation {
            Mutation::ObjectStore(ObjectStoreMutation { mut item, op }) => {
                item.sequence = log_offset;
                self.update_last_object_id(item.key.object_id);
                match op {
                    Operation::Insert => self.tree.insert(item).await,
                    Operation::ReplaceOrInsert => self.tree.replace_or_insert(item).await,
                    Operation::Merge => {
                        let lower_bound = item.key.key_for_merge_into();
                        self.tree.merge_into(item, &lower_bound).await;
                    }
                }
            }
            Mutation::ObjectStoreInfo(StoreInfoMutation(store_info)) => {
                *self.store_info.lock().unwrap() = Some(store_info);
            }
            Mutation::TreeSeal => self.tree.seal().await,
            Mutation::TreeCompact => {
                if transaction.is_none() {
                    self.tree.reset_immutable_layers();
                }
            }
            _ => panic!("unexpected mutation: {:?}", mutation), // TODO(csuter): can't panic
        }
    }

    fn drop_mutation(&self, _mutation: Mutation, _transaction: &Transaction<'_>) {}

    /// Push all in-memory structures to the device. This is not necessary for sync since the
    /// journal will take care of it.  This is supposed to be called when there is either memory or
    /// space pressure (flushing the store will persist in-memory data and allow the journal file to
    /// be trimmed).
    async fn flush(&self) -> Result<(), Error> {
        if self.parent_store.is_none() {
            return Ok(());
        }
        self.ensure_open().await?;
        // TODO(csuter): This whole process needs to be within a transaction, or otherwise safe in
        // the event of power loss.
        let filesystem = self.filesystem();
        let object_manager = filesystem.object_manager();
        if !object_manager.needs_flush(self.store_object_id) {
            return Ok(());
        }

        let parent_store = self.parent_store.as_ref().unwrap();
        let graveyard = object_manager.graveyard().ok_or(anyhow!("Missing graveyard!"))?;

        let object_sync = ObjectFlush::new(object_manager, self.store_object_id);
        let reservation = filesystem.flush_reservation();
        let mut transaction = filesystem
            .clone()
            .new_transaction(
                &[],
                Options {
                    skip_journal_checks: true,
                    allocator_reservation: Some(reservation),
                    ..Default::default()
                },
            )
            .await?;
        let object_handle = ObjectStore::create_object(
            parent_store,
            &mut transaction,
            HandleOptions { skip_journal_checks: true, ..Default::default() },
        )
        .await?;
        let object_id = object_handle.object_id();
        graveyard.add(&mut transaction, parent_store.store_object_id(), object_id);
        transaction.add_with_object(
            self.store_object_id(),
            Mutation::TreeSeal,
            AssocObj::Borrowed(&object_sync),
        );
        transaction.commit().await;

        // This size can't be too big because we've been called to flush in-memory data in order to
        // relieve space in the journal, so if it's too large, we could end up exhausting the
        // journal before we've finished.  With that said, its current value might be too small;
        // it's something that will need to be tuned.
        const SIZE_THRESHOLD: u64 = 32768;
        let mut layer_set = self.tree.immutable_layer_set();
        let mut total_size = 0;
        let mut layers_to_keep = match layer_set.layers.iter().enumerate().find(|(_, layer)| {
            match layer.handle() {
                None => {}
                Some(handle) => {
                    let size = handle.get_size();
                    if total_size + size > SIZE_THRESHOLD {
                        return true;
                    }
                    total_size += size;
                }
            }
            false
        }) {
            Some((index, _)) => layer_set.layers.split_off(index),
            None => Vec::new(),
        };

        let mut merger = layer_set.merger();
        let iter = merger.seek(Bound::Unbounded).await?;
        self.tree
            .compact_with_iterator(
                iter,
                Writer::new(
                    &object_handle,
                    transaction::Options {
                        skip_journal_checks: true,
                        allocator_reservation: Some(reservation),
                        ..Default::default()
                    },
                ),
            )
            .await
            .context("ObjectStore::flush")?;

        let mut serialized_info = Vec::new();
        let mut new_store_info = self.store_info();

        let mut transaction = filesystem
            .clone()
            .new_transaction(
                &[],
                Options {
                    skip_journal_checks: true,
                    allocator_reservation: Some(reservation),
                    ..Default::default()
                },
            )
            .await?;

        // Move the existing layers we're compacting to the graveyard.
        // TODO(jfsulliv): these layer files need to be purged when no longer used.
        for layer in &layer_set.layers {
            if let Some(handle) = layer.handle() {
                graveyard.add(&mut transaction, parent_store.store_object_id(), handle.object_id());
            }
        }

        new_store_info.last_object_id = self.last_object_id.load(atomic::Ordering::Relaxed);

        let mut layers = layers_from_handles(Box::new([object_handle])).await?;
        layers.append(&mut layers_to_keep);

        new_store_info.layers = Vec::new();
        for layer in &layers {
            if let Some(handle) = layer.handle() {
                new_store_info.layers.push(handle.object_id());
            }
        }
        serialize_into(&mut serialized_info, &new_store_info)?;
        let mut buf = self.device.allocate_buffer(serialized_info.len());
        buf.as_mut_slice().copy_from_slice(&serialized_info[..]);

        self.store_info_handle
            .get()
            .unwrap()
            .txn_write(&mut transaction, 0u64, buf.as_ref())
            .await?;
        transaction.add(self.store_object_id(), Mutation::TreeCompact);
        graveyard.remove(&mut transaction, parent_store.store_object_id(), object_id);
        // TODO(csuter): This isn't thread-safe.
        *self.store_info.lock().unwrap() = Some(new_store_info);
        transaction.commit().await;

        self.tree.set_layers(layers);

        object_sync.commit();
        Ok(())
    }
}

impl AsRef<ObjectStore> for ObjectStore {
    fn as_ref(&self) -> &ObjectStore {
        self
    }
}

impl AssociatedObject for ObjectStore {}

// Property updates which are pending a flush.  These can be set by update_timestamps and are
// flushed along with any other updates on write.  While set, the object's properties
// (get_properties) will reflect the pending values.  This is useful for performance, e.g. to permit
// updating properties on a buffered write but deferring the flush until later.
// TODO(jfsulliv): We should flush these when we close the handle.
#[derive(Clone, Debug, Default)]
struct PendingPropertyUpdates {
    creation_time: Option<Timestamp>,
    modification_time: Option<Timestamp>,
}

// TODO(csuter): We should probably be a little more frugal about what we store here since there
// could be a lot of these structures. We could remove block_size and change the size to be atomic.
pub struct StoreObjectHandle<S> {
    owner: Arc<S>,
    object_id: u64,
    block_size: u64,
    attribute_id: u64,
    size: Mutex<u64>,
    options: HandleOptions,
    pending_properties: Mutex<PendingPropertyUpdates>,
    trace: AtomicBool,
}

impl<S: AsRef<ObjectStore> + Send + Sync + 'static> StoreObjectHandle<S> {
    pub fn owner(&self) -> &Arc<S> {
        &self.owner
    }

    pub fn store(&self) -> &ObjectStore {
        self.owner.as_ref().as_ref()
    }

    async fn write_timestamps<'a>(
        &'a self,
        transaction: &mut Transaction<'a>,
        crtime: Option<Timestamp>,
        mtime: Option<Timestamp>,
    ) -> Result<(), Error> {
        if let (None, None) = (crtime.as_ref(), mtime.as_ref()) {
            return Ok(());
        }
        let mut item = self.txn_get_object(transaction).await?;
        if let ObjectValue::Object { ref mut attributes, .. } = item.value {
            if let Some(time) = crtime {
                attributes.creation_time = time;
            }
            if let Some(time) = mtime {
                attributes.modification_time = time;
            }
        } else {
            bail!(FxfsError::Inconsistent);
        };
        transaction.add(
            self.store().store_object_id(),
            Mutation::replace_or_insert_object(item.key, item.value),
        );
        Ok(())
    }

    async fn apply_pending_properties<'a>(
        &'a self,
        transaction: &mut Transaction<'a>,
    ) -> Result<(), Error> {
        let pending = std::mem::take(&mut *self.pending_properties.lock().unwrap());
        self.write_timestamps(transaction, pending.creation_time, pending.modification_time).await
    }

    /// Extend the file with the given extent.  The only use case for this right now is for files
    /// that must exist at certain offsets on the device, such as super-blocks.
    pub async fn extend<'a>(
        &'a self,
        transaction: &mut Transaction<'a>,
        device_range: Range<u64>,
    ) -> Result<(), Error> {
        let old_end =
            round_up(self.txn_get_size(transaction), self.block_size).ok_or(FxfsError::TooBig)?;
        let new_size = old_end + device_range.end - device_range.start;
        self.store().allocator().mark_allocated(transaction, device_range.clone()).await?;
        transaction.add_with_object(
            self.store().store_object_id,
            Mutation::replace_or_insert_object(
                ObjectKey::attribute(self.object_id, self.attribute_id),
                ObjectValue::attribute(new_size),
            ),
            AssocObj::Borrowed(self),
        );
        transaction.add(
            self.store().store_object_id,
            Mutation::merge_object(
                ObjectKey::extent(self.object_id, self.attribute_id, old_end..new_size),
                ObjectValue::extent(device_range.start),
            ),
        );
        self.update_allocated_size(transaction, device_range.end - device_range.start, 0).await
    }

    async fn write_cow<'a>(
        &'a self,
        transaction: &mut Transaction<'a>,
        mut offset: u64,
        buf: BufferRef<'_>,
    ) -> Result<(), Error> {
        let aligned = round_down(offset, self.block_size)
            ..round_up(offset + buf.len() as u64, self.block_size).ok_or(FxfsError::TooBig)?;
        let mut buf_offset = 0;
        let store = self.store();
        let store_id = store.store_object_id;
        if offset + buf.len() as u64 > self.txn_get_size(transaction) {
            transaction.add_with_object(
                store_id,
                Mutation::replace_or_insert_object(
                    ObjectKey::attribute(self.object_id, self.attribute_id),
                    ObjectValue::attribute(offset + buf.len() as u64),
                ),
                AssocObj::Borrowed(self),
            );
        }
        let mut allocated = 0;
        let allocator = store.allocator();
        let trace = self.trace.load(atomic::Ordering::Relaxed);
        let futures = FuturesUnordered::new();
        let mut aligned_offset = aligned.start;
        while buf_offset < buf.len() {
            let device_range = allocator
                .allocate(transaction, aligned.end - aligned_offset)
                .await
                .context("allocation failed")?;
            if trace {
                log::info!("{}.{} A {:?}", store_id, self.object_id, device_range);
            }
            allocated += device_range.end - device_range.start;
            let end = aligned_offset + device_range.end - device_range.start;
            let len = min(buf.len() - buf_offset, (end - offset) as usize);
            assert!(len > 0);
            futures.push(async move {
                let checksum = self
                    .write_at(
                        offset,
                        buf.subslice(buf_offset..buf_offset + len),
                        device_range.start + offset % self.block_size,
                        true,
                    )
                    .await?;
                Ok(Mutation::merge_object(
                    ObjectKey::extent(self.object_id, self.attribute_id, aligned_offset..end),
                    ObjectValue::extent_with_checksum(device_range.start, checksum),
                ))
            });
            aligned_offset = end;
            buf_offset += len;
            offset += len as u64;
        }
        let (mutations, _): (Vec<_>, _) = try_join!(futures.try_collect(), async {
            let deallocated = self.deallocate_old_extents(transaction, aligned.clone()).await?;
            self.update_allocated_size(transaction, allocated, deallocated).await
        })?;
        for m in mutations {
            transaction.add(store_id, m);
        }
        Ok(())
    }

    // All the extents for the range must have been preallocated using preallocate_range or from
    // existing writes.
    async fn overwrite(&self, mut offset: u64, buf: BufferRef<'_>) -> Result<(), Error> {
        let tree = &self.store().tree;
        let layer_set = tree.layer_set();
        let mut merger = layer_set.merger();
        let end = offset + buf.len() as u64;
        let mut iter = merger
            .seek(Bound::Included(
                &ObjectKey::extent(self.object_id, self.attribute_id, offset..end).search_key(),
            ))
            .await?;
        let mut pos = 0;
        loop {
            let (device_offset, to_do) = match iter.get().and_then(Into::into) {
                Some((
                    object_id,
                    attribute_id,
                    ExtentKey { range },
                    ExtentValue { device_offset: Some((device_offset, _)) },
                )) if object_id == self.object_id
                    && attribute_id == self.attribute_id
                    && range.start <= offset =>
                {
                    (
                        device_offset + (offset - range.start),
                        min(buf.len() - pos, (range.end - offset) as usize),
                    )
                }
                _ => bail!("offset {} not allocated", offset),
            };
            self.write_at(offset, buf.subslice(pos..pos + to_do), device_offset, false).await?;
            pos += to_do;
            if pos == buf.len() {
                break;
            }
            offset += to_do as u64;
            iter.advance().await?;
        }
        Ok(())
    }

    async fn write_at(
        &self,
        offset: u64,
        buf: BufferRef<'_>,
        mut device_offset: u64,
        compute_checksum: bool,
    ) -> Result<Checksums, Error> {
        // Deal with alignment.
        let start_align = (offset % self.block_size) as usize;
        let start_offset = offset - start_align as u64;
        let trace = self.trace.load(atomic::Ordering::Relaxed);
        let mut checksums = Vec::new();
        let remainder = if start_align > 0 {
            let (head, remainder) =
                buf.split_at(min(self.block_size as usize - start_align, buf.len()));
            let mut align_buf = self.store().device.allocate_buffer(self.block_size as usize);
            let read = self.read(start_offset, align_buf.as_mut()).await?;
            align_buf.as_mut_slice()[read..].fill(0);
            align_buf.as_mut_slice()[start_align..(start_align + head.len())]
                .copy_from_slice(head.as_slice());
            device_offset -= start_align as u64;
            if trace {
                log::info!(
                    "{}.{} WH {:?}",
                    self.store().store_object_id(),
                    self.object_id,
                    device_offset..device_offset + align_buf.len() as u64
                );
            }
            try_join!(self.store().device.write(device_offset, align_buf.as_ref()), async {
                if compute_checksum {
                    checksums.push(fletcher64(align_buf.as_slice(), 0));
                }
                Ok(())
            })?;
            device_offset += self.block_size;
            remainder
        } else {
            buf
        };
        if remainder.len() > 0 {
            let end = offset + buf.len() as u64;
            let end_align = (end % self.block_size) as usize;
            let (whole_blocks, tail) = remainder.split_at(remainder.len() - end_align);
            if whole_blocks.len() > 0 {
                if trace {
                    log::info!(
                        "{}.{} W {:?}",
                        self.store().store_object_id(),
                        self.object_id,
                        device_offset..device_offset + whole_blocks.len() as u64
                    );
                }
                try_join!(self.store().device.write(device_offset, whole_blocks), async {
                    if compute_checksum {
                        for chunk in whole_blocks.as_slice().chunks_exact(self.block_size as usize)
                        {
                            checksums.push(fletcher64(chunk, 0));
                        }
                    }
                    Ok(())
                })?;
                device_offset += whole_blocks.len() as u64;
            }
            if tail.len() > 0 {
                let mut align_buf = self.store().device.allocate_buffer(self.block_size as usize);
                let read = self.read(end - end_align as u64, align_buf.as_mut()).await?;
                align_buf.as_mut_slice()[read..].fill(0);
                &align_buf.as_mut_slice()[..tail.len()].copy_from_slice(tail.as_slice());
                if trace {
                    log::info!(
                        "{}.{} WT {:?}",
                        self.store().store_object_id(),
                        self.object_id,
                        device_offset..device_offset + align_buf.len() as u64
                    );
                }
                try_join!(self.store().device.write(device_offset, align_buf.as_ref()), async {
                    if compute_checksum {
                        checksums.push(fletcher64(align_buf.as_slice(), 0));
                    }
                    Ok(())
                })?;
            }
        }
        Ok(if compute_checksum { Checksums::Fletcher(checksums) } else { Checksums::None })
    }

    // Returns the amount deallocated.
    async fn deallocate_old_extents(
        &self,
        transaction: &mut Transaction<'_>,
        range: Range<u64>,
    ) -> Result<u64, Error> {
        assert_eq!(range.start % self.block_size as u64, 0);
        assert_eq!(range.end % self.block_size as u64, 0);
        if range.start == range.end {
            return Ok(0);
        }
        let tree = &self.store().tree;
        let layer_set = tree.layer_set();
        let key = ExtentKey::new(range);
        let lower_bound =
            ObjectKey::with_extent_key(self.object_id, self.attribute_id, key.search_key());
        let mut merger = layer_set.merger();
        let mut iter = merger.seek(Bound::Included(&lower_bound)).await?;
        let allocator = self.store().allocator();
        let mut deallocated = 0;
        let trace = self.trace.load(atomic::Ordering::Relaxed);
        loop {
            let (extent_key, extent_value) = match iter.get().and_then(Into::into) {
                Some((oid, attribute_id, extent_key, extent_value))
                    if oid == self.object_id && attribute_id == self.attribute_id =>
                {
                    (extent_key, extent_value)
                }
                _ => break,
            };
            if let ExtentValue { device_offset: Some((device_offset, _)) } = extent_value {
                if let Some(overlap) = key.overlap(extent_key) {
                    let range = device_offset + overlap.start - extent_key.range.start
                        ..device_offset + overlap.end - extent_key.range.start;
                    if trace {
                        log::info!(
                            "{}.{} D {:?}",
                            self.store().store_object_id(),
                            self.object_id,
                            range
                        );
                    }
                    allocator.deallocate(transaction, range).await;
                    deallocated += overlap.end - overlap.start;
                } else {
                    break;
                }
            }
            iter.advance().await?;
        }
        Ok(deallocated)
    }

    /// Zeroes the given range.  The range must be aligned.  Returns the amount of data deallocated.
    pub async fn zero(
        &self,
        transaction: &mut Transaction<'_>,
        range: Range<u64>,
    ) -> Result<(), Error> {
        let deallocated = self.deallocate_old_extents(transaction, range.clone()).await?;
        if deallocated > 0 {
            self.update_allocated_size(transaction, 0, deallocated).await?;
            transaction.add(
                self.store().store_object_id,
                Mutation::merge_object(
                    ObjectKey::extent(self.object_id, self.attribute_id, range),
                    ObjectValue::deleted_extent(),
                ),
            );
        }
        Ok(())
    }

    // If |transaction| has an impending mutation for the underlying object, returns that.
    // Otherwise, looks up the object from the tree.
    async fn txn_get_object(&self, transaction: &Transaction<'_>) -> Result<ObjectItem, Error> {
        self.store().txn_get_object(transaction, self.object_id).await
    }

    // Within a transaction, the size of the object might have changed, so get the size from there
    // if it exists, otherwise, fall back on the cached size.
    fn txn_get_size(&self, transaction: &Transaction<'_>) -> u64 {
        transaction
            .get_object_mutation(
                self.store().store_object_id,
                ObjectKey::attribute(self.object_id, self.attribute_id),
            )
            .and_then(|m| {
                if let ObjectItem { value: ObjectValue::Attribute { size }, .. } = m.item {
                    Some(size)
                } else {
                    None
                }
            })
            .unwrap_or_else(|| self.get_size())
    }

    async fn get_allocated_size(&self) -> Result<u64, Error> {
        self.store().ensure_open().await?;
        if let ObjectItem {
            value: ObjectValue::Object { kind: ObjectKind::File { allocated_size, .. }, .. },
            ..
        } = self
            .store()
            .tree
            .find(&ObjectKey::object(self.object_id))
            .await?
            .expect("Unable to find object record")
        {
            Ok(allocated_size)
        } else {
            panic!("Unexpected object value");
        }
    }

    async fn update_allocated_size(
        &self,
        transaction: &mut Transaction<'_>,
        allocated: u64,
        deallocated: u64,
    ) -> Result<(), Error> {
        if allocated == deallocated {
            return Ok(());
        }
        let mut item = self.txn_get_object(transaction).await?;
        if let ObjectItem {
            value: ObjectValue::Object { kind: ObjectKind::File { ref mut allocated_size, .. }, .. },
            ..
        } = item
        {
            // The only way for these to fail are if the volume is inconsistent.
            *allocated_size = allocated_size
                .checked_add(allocated)
                .ok_or(FxfsError::Inconsistent)?
                .checked_sub(deallocated)
                .ok_or(FxfsError::Inconsistent)?;
        } else {
            panic!("Unexpceted object value");
        }
        transaction.add(
            self.store().store_object_id,
            Mutation::replace_or_insert_object(item.key, item.value),
        );
        Ok(())
    }
}

impl<S: Send + Sync + 'static> AssociatedObject for StoreObjectHandle<S> {
    fn will_apply_mutation(&self, mutation: &Mutation) {
        match mutation {
            Mutation::ObjectStore(ObjectStoreMutation {
                item: ObjectItem { value: ObjectValue::Attribute { size }, .. },
                ..
            }) => *self.size.lock().unwrap() = *size,
            _ => {}
        }
    }
}

// TODO(jfsulliv): Move into utils module or something else.
pub fn round_down<T: Into<u64>>(offset: u64, block_size: T) -> u64 {
    offset - offset % block_size.into()
}

pub fn round_up<T: Into<u64>>(offset: u64, block_size: T) -> Option<u64> {
    let block_size = block_size.into();
    Some(round_down(offset.checked_add(block_size - 1)?, block_size))
}

#[async_trait]
impl<S: AsRef<ObjectStore> + Send + Sync + 'static> ObjectHandle for StoreObjectHandle<S> {
    fn set_trace(&self, v: bool) {
        log::info!("{}.{} tracing: {}", self.store().store_object_id, self.object_id(), v);
        self.trace.store(v, atomic::Ordering::Relaxed);
    }

    fn object_id(&self) -> u64 {
        return self.object_id;
    }

    fn allocate_buffer(&self, size: usize) -> Buffer<'_> {
        self.store().device.allocate_buffer(size)
    }

    fn block_size(&self) -> u32 {
        self.store().block_size()
    }

    async fn read(&self, mut offset: u64, mut buf: MutableBufferRef<'_>) -> Result<usize, Error> {
        if buf.len() == 0 {
            return Ok(0);
        }
        if offset % self.block_size != 0 || buf.range().start as u64 % self.block_size != 0 {
            panic!("Misaligned read off: {} buf_range: {:?}", offset, buf.range());
        }
        let fs = self.store().filesystem();
        let _guard = fs
            .read_lock(&[LockKey::object_attribute(
                self.store().store_object_id,
                self.object_id,
                self.attribute_id,
            )])
            .await;
        let size = self.get_size();
        if offset >= size {
            return Ok(0);
        }
        let tree = &self.store().tree;
        let layer_set = tree.layer_set();
        let mut merger = layer_set.merger();
        let mut iter = merger
            .seek(Bound::Included(&ObjectKey::extent(
                self.object_id,
                self.attribute_id,
                offset..offset + 1,
            )))
            .await?;
        let to_do = min(buf.len() as u64, size - offset) as usize;
        buf = buf.subslice_mut(0..to_do);
        let end_align = ((offset + to_do as u64) % self.block_size) as usize;
        let trace = self.trace.load(atomic::Ordering::Relaxed);
        while let Some((object_id, attribute_id, extent_key, extent_value)) =
            iter.get().and_then(Into::into)
        {
            if object_id != self.object_id || attribute_id != self.attribute_id {
                break;
            }
            if extent_key.range.start > offset {
                // Zero everything up to the start of the extent.
                let to_zero = min(extent_key.range.start - offset, buf.len() as u64) as usize;
                for i in &mut buf.as_mut_slice()[..to_zero] {
                    *i = 0;
                }
                buf = buf.subslice_mut(to_zero..);
                if buf.is_empty() {
                    break;
                }
                offset += to_zero as u64;
            }

            if let ExtentValue { device_offset: Some((device_offset, _)) } = extent_value {
                let mut device_offset = device_offset + (offset - extent_key.range.start);

                let to_copy = min(buf.len() - end_align, (extent_key.range.end - offset) as usize);
                if to_copy > 0 {
                    if trace {
                        log::info!(
                            "{}.{} R {:?}",
                            self.store().store_object_id(),
                            self.object_id,
                            device_offset..device_offset + to_copy as u64
                        );
                    }
                    self.store()
                        .device
                        .read(device_offset, buf.reborrow().subslice_mut(..to_copy))
                        .await?;
                    buf = buf.subslice_mut(to_copy..);
                    if buf.is_empty() {
                        break;
                    }
                    offset += to_copy as u64;
                    device_offset += to_copy as u64;
                }

                // Deal with end alignment, again by reading the exsting contents into an alignment
                // buffer.
                if offset < extent_key.range.end && end_align > 0 {
                    let mut align_buf =
                        self.store().device.allocate_buffer(self.block_size as usize);
                    if trace {
                        log::info!(
                            "{}.{} RT {:?}",
                            self.store().store_object_id(),
                            self.object_id,
                            device_offset..device_offset + align_buf.len() as u64
                        );
                    }
                    self.store().device.read(device_offset, align_buf.as_mut()).await?;
                    buf.as_mut_slice().copy_from_slice(&align_buf.as_slice()[..end_align]);
                    buf = buf.subslice_mut(0..0);
                    break;
                }
            } else if extent_key.range.end >= offset + buf.len() as u64 {
                // Deleted extent covers remainder, so we're done.
                break;
            }

            iter.advance().await?;
        }
        buf.as_mut_slice().fill(0);
        Ok(to_do)
    }

    async fn txn_write<'a>(
        &'a self,
        transaction: &mut Transaction<'a>,
        offset: u64,
        buf: BufferRef<'_>,
    ) -> Result<(), Error> {
        if buf.is_empty() {
            return Ok(());
        }
        if offset % self.block_size() as u64 != buf.range().start as u64 % self.block_size() as u64
        {
            panic!("Unaligned write off: {} buf.range: {:?}", offset, buf.range());
        }
        self.apply_pending_properties(transaction).await?;
        if self.options.overwrite {
            self.overwrite(offset, buf).await
        } else {
            self.write_cow(transaction, offset, buf).await
        }
    }

    fn get_size(&self) -> u64 {
        *self.size.lock().unwrap()
    }

    async fn truncate<'a>(
        &'a self,
        transaction: &mut Transaction<'a>,
        size: u64,
    ) -> Result<(), Error> {
        let old_size = self.txn_get_size(transaction);
        if size < old_size {
            let aligned_size = round_up(size, self.block_size).ok_or(FxfsError::TooBig)?;
            self.zero(
                transaction,
                aligned_size..round_up(old_size, self.block_size).ok_or(FxfsError::Inconsistent)?,
            )
            .await?;
            let to_zero = aligned_size - size;
            if to_zero > 0 {
                assert!(to_zero < self.block_size);
                // We intentionally use the COW write path even if we're in overwrite mode. There's
                // no need to support overwrite mode here, and it would be difficult since we'd need
                // to transactionalize zeroing the tail of the last block with the other metadata
                // changes, which we don't currently have a way to do.
                // TODO(csuter): This is allocating a small buffer that we'll just end up copying.
                // Is there a better way?
                let mut buf = self.store().device.allocate_buffer(to_zero as usize);
                buf.as_mut_slice().fill(0);
                self.write_cow(transaction, size, buf.as_ref()).await?;
            }
        }
        transaction.add_with_object(
            self.store().store_object_id,
            Mutation::replace_or_insert_object(
                ObjectKey::attribute(self.object_id, self.attribute_id),
                ObjectValue::attribute(size),
            ),
            AssocObj::Borrowed(self),
        );
        self.apply_pending_properties(transaction).await?;
        Ok(())
    }

    // Must be multiple of block size.
    async fn preallocate_range<'a>(
        &'a self,
        transaction: &mut Transaction<'a>,
        mut file_range: Range<u64>,
    ) -> Result<Vec<Range<u64>>, Error> {
        let mut ranges = Vec::new();
        let tree = &self.store().tree;
        let layer_set = tree.layer_set();
        let mut merger = layer_set.merger();
        let mut iter = merger
            .seek(Bound::Included(&ObjectKey::with_extent_key(
                self.object_id,
                self.attribute_id,
                ExtentKey::new(file_range.clone()).search_key(),
            )))
            .await?;
        let mut allocated = 0;
        'outer: while file_range.start < file_range.end {
            let allocate_end = loop {
                match iter.get().and_then(Into::into) {
                    Some((
                        oid,
                        attribute_id,
                        extent_key,
                        ExtentValue { device_offset: Some((device_offset, _)) },
                    )) if oid == self.object_id
                        && attribute_id == self.attribute_id
                        && extent_key.range.start < file_range.end =>
                    {
                        if extent_key.range.start <= file_range.start {
                            // Record the existing extent and move on.
                            let device_range = device_offset + file_range.start
                                - extent_key.range.start
                                ..device_offset + min(extent_key.range.end, file_range.end)
                                    - extent_key.range.start;
                            file_range.start += device_range.end - device_range.start;
                            ranges.push(device_range);
                            if file_range.start >= file_range.end {
                                break 'outer;
                            }
                            iter.advance().await?;
                            continue;
                        } else {
                            // There's nothing allocated between file_range.start and the beginning
                            // of this extent.
                            break extent_key.range.start;
                        }
                    }
                    Some((oid, attribute_id, extent_key, ExtentValue { device_offset: None }))
                        if oid == self.object_id
                            && attribute_id == self.attribute_id
                            && extent_key.range.end < file_range.end =>
                    {
                        iter.advance().await?;
                    }
                    _ => {
                        // We can just preallocate the rest.
                        break file_range.end;
                    }
                }
            };
            let device_range = self
                .store()
                .allocator()
                .allocate(transaction, allocate_end - file_range.start)
                .await
                .context("allocation failed")?;
            allocated += device_range.end - device_range.start;
            let this_file_range =
                file_range.start..file_range.start + device_range.end - device_range.start;
            file_range.start = this_file_range.end;
            transaction.add(
                self.store().store_object_id,
                Mutation::merge_object(
                    ObjectKey::extent(self.object_id, self.attribute_id, this_file_range),
                    ObjectValue::extent(device_range.start),
                ),
            );
            ranges.push(device_range);
            // If we didn't allocate all that we requested, we'll loop around and try again.
        }
        // Update the file size if it changed.
        if file_range.end > self.txn_get_size(transaction) {
            transaction.add_with_object(
                self.store().store_object_id,
                Mutation::replace_or_insert_object(
                    ObjectKey::attribute(self.object_id, self.attribute_id),
                    ObjectValue::attribute(file_range.end),
                ),
                AssocObj::Borrowed(self),
            );
        }
        self.update_allocated_size(transaction, allocated, 0).await?;
        Ok(ranges)
    }

    async fn update_timestamps<'a>(
        &'a self,
        transaction: Option<&mut Transaction<'a>>,
        crtime: Option<Timestamp>,
        mtime: Option<Timestamp>,
    ) -> Result<(), Error> {
        let (crtime, mtime) = {
            let mut pending = self.pending_properties.lock().unwrap();

            if transaction.is_none() {
                // Just buffer the new values for later.
                if crtime.is_some() {
                    pending.creation_time = crtime;
                }
                if mtime.is_some() {
                    pending.modification_time = mtime;
                }
                return Ok(());
            }
            (crtime.or(pending.creation_time.clone()), mtime.or(pending.modification_time.clone()))
        };
        self.write_timestamps(transaction.unwrap(), crtime, mtime).await
    }

    // TODO(jfsulliv): Make StoreObjectHandle per-object (not per-attribute as it currently is)
    // and pass in a list of attributes to fetch properties for.
    async fn get_properties(&self) -> Result<ObjectProperties, Error> {
        // Take a read guard since we need to return a consistent view of all object properties.
        let fs = self.store().filesystem();
        let _guard = fs
            .read_lock(&[LockKey::object_attribute(
                self.store().store_object_id,
                self.object_id,
                self.attribute_id,
            )])
            .await;
        let item = self
            .store()
            .tree
            .find(&ObjectKey::object(self.object_id))
            .await?
            .expect("Unable to find object record");
        match item.value {
            ObjectValue::Object {
                kind: ObjectKind::File { refs, allocated_size },
                attributes: ObjectAttributes { creation_time, modification_time },
            } => {
                let data_attribute_size = self.get_size();
                let pending = self.pending_properties.lock().unwrap();
                Ok(ObjectProperties {
                    refs,
                    allocated_size,
                    data_attribute_size,
                    creation_time: pending.creation_time.clone().unwrap_or(creation_time),
                    modification_time: pending
                        .modification_time
                        .clone()
                        .unwrap_or(modification_time),
                    sub_dirs: 0,
                })
            }
            _ => bail!(FxfsError::NotFile),
        }
    }

    async fn new_transaction<'a>(&self) -> Result<Transaction<'a>, Error> {
        self.new_transaction_with_options(Options {
            skip_journal_checks: self.options.skip_journal_checks,
            ..Default::default()
        })
        .await
    }

    async fn new_transaction_with_options<'a>(
        &self,
        options: Options<'a>,
    ) -> Result<Transaction<'a>, Error> {
        Ok(self
            .store()
            .filesystem()
            .new_transaction(
                &[LockKey::object_attribute(
                    self.store().store_object_id,
                    self.object_id,
                    self.attribute_id,
                )],
                options,
            )
            .await?)
    }
}

#[cfg(test)]
mod tests {
    use {
        crate::{
            lsm_tree::types::{ItemRef, LayerIterator},
            object_handle::{ObjectHandle, ObjectHandleExt, ObjectProperties},
            object_store::{
                allocator::Allocator,
                filesystem::{Filesystem, FxFilesystem, Mutations, SyncOptions},
                record::{ObjectKey, ObjectKeyData, Timestamp},
                round_up,
                transaction::{Options, TransactionHandler},
                HandleOptions, ObjectStore, StoreObjectHandle,
            },
        },
        fuchsia_async as fasync,
        futures::{channel::oneshot::channel, future::join_all, join},
        matches::assert_matches,
        rand::Rng,
        std::{
            ops::Bound,
            sync::{Arc, Mutex},
            time::Duration,
        },
        storage_device::{fake_device::FakeDevice, DeviceHolder},
    };

    const TEST_DEVICE_BLOCK_SIZE: u32 = 512;

    // Some tests (the preallocate_range ones) currently assume that the data only occupies a single
    // device block.
    const TEST_DATA_OFFSET: u64 = 600;
    const TEST_DATA: &[u8] = b"hello";
    const TEST_OBJECT_SIZE: u64 = 913;
    const TEST_OBJECT_ALLOCATED_SIZE: u64 = 512;

    async fn test_filesystem() -> Arc<FxFilesystem> {
        let device = DeviceHolder::new(FakeDevice::new(8192, TEST_DEVICE_BLOCK_SIZE));
        FxFilesystem::new_empty(device).await.expect("new_empty failed")
    }

    async fn test_filesystem_and_object() -> (Arc<FxFilesystem>, StoreObjectHandle<ObjectStore>) {
        let fs = test_filesystem().await;
        let store = fs.root_store();
        let object;
        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        object = ObjectStore::create_object(&store, &mut transaction, HandleOptions::default())
            .await
            .expect("create_object failed");
        {
            let align = TEST_DATA_OFFSET as usize % TEST_DEVICE_BLOCK_SIZE as usize;
            let mut buf = object.allocate_buffer(align + TEST_DATA.len());
            buf.as_mut_slice()[align..].copy_from_slice(TEST_DATA);
            object
                .txn_write(&mut transaction, TEST_DATA_OFFSET, buf.subslice(align..))
                .await
                .expect("write failed");
        }
        object.truncate(&mut transaction, TEST_OBJECT_SIZE).await.expect("truncate failed");
        transaction.commit().await;
        (fs, object)
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_zero_buf_len_read() {
        let (fs, object) = test_filesystem_and_object().await;
        let mut buf = object.allocate_buffer(0);
        assert_eq!(object.read(0u64, buf.as_mut()).await.expect("read failed"), 0);
        fs.close().await.expect("Close failed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_beyond_eof_read() {
        let (fs, object) = test_filesystem_and_object().await;
        let offset = TEST_OBJECT_SIZE as usize - 2;
        let align = offset % TEST_DEVICE_BLOCK_SIZE as usize;
        let len: usize = 2;
        let mut buf = object.allocate_buffer(align + len + 1);
        buf.as_mut_slice().fill(123u8);
        assert_eq!(
            object.read((offset - align) as u64, buf.as_mut()).await.expect("read failed"),
            align + len
        );
        assert_eq!(&buf.as_slice()[align..align + len], &vec![0u8; len]);
        assert_eq!(&buf.as_slice()[align + len..], &vec![123u8; buf.len() - align - len]);
        fs.close().await.expect("Close failed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_read_sparse() {
        let (fs, object) = test_filesystem_and_object().await;
        // Deliberately read not right to eof.
        let len = TEST_OBJECT_SIZE as usize - 1;
        let mut buf = object.allocate_buffer(len);
        buf.as_mut_slice().fill(123u8);
        assert_eq!(object.read(0, buf.as_mut()).await.expect("read failed"), len);
        let mut expected = vec![0; len];
        let offset = TEST_DATA_OFFSET as usize;
        &mut expected[offset..offset + TEST_DATA.len()].copy_from_slice(TEST_DATA);
        assert_eq!(buf.as_slice()[..len], expected[..]);
        fs.close().await.expect("Close failed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_read_after_writes_interspersed_with_flush() {
        let (fs, object) = test_filesystem_and_object().await;

        object.owner().flush().await.expect("flush failed");

        // Write more test data to the first block fo the file.
        let mut buf = object.allocate_buffer(TEST_DATA.len());
        buf.as_mut_slice().copy_from_slice(TEST_DATA);
        object.write(0u64, buf.as_ref()).await.expect("write failed");

        let len = TEST_OBJECT_SIZE as usize - 1;
        let mut buf = object.allocate_buffer(len);
        buf.as_mut_slice().fill(123u8);
        assert_eq!(object.read(0, buf.as_mut()).await.expect("read failed"), len);

        let mut expected = vec![0u8; len];
        let offset = TEST_DATA_OFFSET as usize;
        &mut expected[offset..offset + TEST_DATA.len()].copy_from_slice(TEST_DATA);
        &mut expected[..TEST_DATA.len()].copy_from_slice(TEST_DATA);
        assert_eq!(buf.as_slice(), &expected);
        fs.close().await.expect("Close failed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_read_after_truncate_and_extend() {
        let (fs, object) = test_filesystem_and_object().await;

        // Arrange for there to be <extent><deleted-extent><extent>.
        let mut buf = object.allocate_buffer(TEST_DATA.len());
        buf.as_mut_slice().copy_from_slice(TEST_DATA);
        object.write(0, buf.as_ref()).await.expect("write failed"); // This adds an extent at 0..512.
        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        object.truncate(&mut transaction, 3).await.expect("truncate failed"); // This deletes 512..1024.
        transaction.commit().await;
        let data = b"foo";
        let offset = 1500u64;
        let align = (offset % TEST_DEVICE_BLOCK_SIZE as u64) as usize;
        let mut buf = object.allocate_buffer(align + data.len());
        buf.as_mut_slice()[align..].copy_from_slice(data);
        object.write(1500, buf.subslice(align..)).await.expect("write failed"); // This adds 1024..1536.

        const LEN1: usize = 1503;
        let mut buf = object.allocate_buffer(LEN1);
        buf.as_mut_slice().fill(123u8);
        assert_eq!(object.read(0, buf.as_mut()).await.expect("read failed"), LEN1);
        let mut expected = [0; LEN1];
        &mut expected[..3].copy_from_slice(&TEST_DATA[..3]);
        &mut expected[1500..].copy_from_slice(b"foo");
        assert_eq!(buf.as_slice(), &expected);

        // Also test a read that ends midway through the deleted extent.
        const LEN2: usize = 601;
        let mut buf = object.allocate_buffer(LEN2);
        buf.as_mut_slice().fill(123u8);
        assert_eq!(object.read(0, buf.as_mut()).await.expect("read failed"), LEN2);
        assert_eq!(buf.as_slice(), &expected[..LEN2]);
        fs.close().await.expect("Close failed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_read_whole_blocks_with_multiple_objects() {
        let (fs, object) = test_filesystem_and_object().await;
        let mut buffer = object.allocate_buffer(512);
        buffer.as_mut_slice().fill(0xaf);
        object.write(0, buffer.as_ref()).await.expect("write failed");

        let store = object.owner();
        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        let object2 =
            ObjectStore::create_object(&store, &mut transaction, HandleOptions::default())
                .await
                .expect("create_object failed");
        transaction.commit().await;
        let mut ef_buffer = object.allocate_buffer(512);
        ef_buffer.as_mut_slice().fill(0xef);
        object2.write(0, ef_buffer.as_ref()).await.expect("write failed");

        let mut buffer = object.allocate_buffer(512);
        buffer.as_mut_slice().fill(0xaf);
        object.write(512, buffer.as_ref()).await.expect("write failed");
        let mut transaction = object.new_transaction().await.expect("new_transaction failed");
        object.truncate(&mut transaction, 1536).await.expect("truncate failed");
        transaction.commit().await;
        object2.write(512, ef_buffer.as_ref()).await.expect("write failed");

        let mut buffer = object.allocate_buffer(2048);
        buffer.as_mut_slice().fill(123);
        assert_eq!(object.read(0, buffer.as_mut()).await.expect("read failed"), 1536);
        assert_eq!(&buffer.as_slice()[..1024], &[0xaf; 1024]);
        assert_eq!(&buffer.as_slice()[1024..1536], &[0; 512]);
        assert_eq!(object2.read(0, buffer.as_mut()).await.expect("read failed"), 1024);
        assert_eq!(&buffer.as_slice()[..1024], &[0xef; 1024]);
        fs.close().await.expect("Close failed");
    }

    async fn test_preallocate_common(
        allocator: &dyn Allocator,
        object: StoreObjectHandle<ObjectStore>,
    ) {
        let allocated_before = allocator.get_allocated_bytes();
        let mut transaction = object.new_transaction().await.expect("new_transaction failed");
        object.preallocate_range(&mut transaction, 0..512).await.expect("preallocate_range failed");
        transaction.commit().await;
        assert!(object.get_size() < 1048576);
        let mut transaction = object.new_transaction().await.expect("new_transaction failed");
        object
            .preallocate_range(&mut transaction, 0..1048576)
            .await
            .expect("preallocate_range failed");
        transaction.commit().await;
        assert_eq!(object.get_size(), 1048576);
        // Check that it didn't reallocate the space for the existing extent
        let allocated_after = allocator.get_allocated_bytes();
        assert_eq!(allocated_after - allocated_before, 1048576 - TEST_DEVICE_BLOCK_SIZE as u64);

        // Reopen the object in overwrite mode.
        let object = ObjectStore::open_object(
            &object.owner,
            object.object_id(),
            HandleOptions { overwrite: true, ..Default::default() },
        )
        .await
        .expect("open_object failed");
        let mut buf = object.allocate_buffer(2048);
        buf.as_mut_slice().fill(47);
        object.write(0, buf.subslice(..TEST_DATA_OFFSET as usize)).await.expect("write failed");
        buf.as_mut_slice().fill(95);
        let offset = round_up(TEST_OBJECT_SIZE, TEST_DEVICE_BLOCK_SIZE).unwrap();
        object.write(offset, buf.as_ref()).await.expect("write failed");

        // Make sure there were no more allocations.
        assert_eq!(allocator.get_allocated_bytes(), allocated_after);

        // Read back the data and make sure it is what we expect.
        let mut buf = object.allocate_buffer(104876);
        assert_eq!(object.read(0, buf.as_mut()).await.expect("read failed"), buf.len());
        assert_eq!(&buf.as_slice()[..TEST_DATA_OFFSET as usize], &[47; TEST_DATA_OFFSET as usize]);
        assert_eq!(
            &buf.as_slice()[TEST_DATA_OFFSET as usize..TEST_DATA_OFFSET as usize + TEST_DATA.len()],
            TEST_DATA
        );
        assert_eq!(&buf.as_slice()[offset as usize..offset as usize + 2048], &[95; 2048]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_preallocate_range() {
        let (fs, object) = test_filesystem_and_object().await;
        test_preallocate_common(fs.allocator().as_ref(), object).await;
        fs.close().await.expect("Close failed");
    }

    // This is identical to the previous test except that we flush so that extents end up in
    // different layers.
    #[fasync::run_singlethreaded(test)]
    async fn test_preallocate_suceeds_when_extents_are_in_different_layers() {
        let (fs, object) = test_filesystem_and_object().await;
        object.owner().flush().await.expect("flush failed");
        test_preallocate_common(fs.allocator().as_ref(), object).await;
        fs.close().await.expect("Close failed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_already_preallocated() {
        let (fs, object) = test_filesystem_and_object().await;
        let allocator = fs.allocator();
        let allocated_before = allocator.get_allocated_bytes();
        let mut transaction = object.new_transaction().await.expect("new_transaction failed");
        let offset = TEST_DATA_OFFSET - TEST_DATA_OFFSET % TEST_DEVICE_BLOCK_SIZE as u64;
        object
            .preallocate_range(&mut transaction, offset..offset + 512)
            .await
            .expect("preallocate_range failed");
        transaction.commit().await;
        // Check that it didn't reallocate any new space.
        assert_eq!(allocator.get_allocated_bytes(), allocated_before);
        fs.close().await.expect("Close failed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_overwrite_fails_if_not_preallocated() {
        let (fs, object) = test_filesystem_and_object().await;

        let object = ObjectStore::open_object(
            &object.owner,
            object.object_id(),
            HandleOptions { overwrite: true, ..Default::default() },
        )
        .await
        .expect("open_object failed");
        let mut buf = object.allocate_buffer(2048);
        buf.as_mut_slice().fill(95);
        let offset = round_up(TEST_OBJECT_SIZE, TEST_DEVICE_BLOCK_SIZE).unwrap();
        object.write(offset, buf.as_ref()).await.expect_err("write suceceded");
        fs.close().await.expect("Close failed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_extend() {
        let fs = test_filesystem().await;
        let handle;
        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        let store = fs.root_store();
        handle = ObjectStore::create_object(
            &store,
            &mut transaction,
            HandleOptions { overwrite: true, ..Default::default() },
        )
        .await
        .expect("create_object failed");
        handle
            .extend(&mut transaction, 0..5 * TEST_DEVICE_BLOCK_SIZE as u64)
            .await
            .expect("extend failed");
        transaction.commit().await;
        let mut buf = handle.allocate_buffer(5 * TEST_DEVICE_BLOCK_SIZE as usize);
        buf.as_mut_slice().fill(123);
        handle.write(0, buf.as_ref()).await.expect("write failed");
        buf.as_mut_slice().fill(67);
        handle.read(0, buf.as_mut()).await.expect("read failed");
        assert_eq!(buf.as_slice(), [123; 5 * TEST_DEVICE_BLOCK_SIZE as usize]);
        fs.close().await.expect("Close failed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_truncate_deallocates_old_extents() {
        let (fs, object) = test_filesystem_and_object().await;
        let mut buf = object.allocate_buffer(5 * TEST_DEVICE_BLOCK_SIZE as usize);
        buf.as_mut_slice().fill(0xaa);
        object.write(0, buf.as_ref()).await.expect("write failed");

        let allocator = fs.allocator();
        let allocated_before = allocator.get_allocated_bytes();
        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        object
            .truncate(&mut transaction, TEST_DEVICE_BLOCK_SIZE as u64)
            .await
            .expect("truncate failed");
        transaction.commit().await;
        let allocated_after = allocator.get_allocated_bytes();
        assert!(
            allocated_after < allocated_before,
            "before = {} after = {}",
            allocated_before,
            allocated_after
        );
        fs.close().await.expect("Close failed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_adjust_refs() {
        let (fs, object) = test_filesystem_and_object().await;
        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        let store = object.owner();
        assert_eq!(
            store
                .adjust_refs(&mut transaction, object.object_id(), 1)
                .await
                .expect("adjust_refs failed"),
            false
        );
        transaction.commit().await;

        let allocator = fs.allocator();
        let allocated_before = allocator.get_allocated_bytes();
        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        assert_eq!(
            store
                .adjust_refs(&mut transaction, object.object_id(), -2)
                .await
                .expect("adjust_refs failed"),
            true
        );
        transaction.commit().await;

        assert_eq!(allocator.get_allocated_bytes(), allocated_before);

        store.tombstone(object.object_id).await.expect("purge failed");

        assert_eq!(
            allocated_before - allocator.get_allocated_bytes(),
            TEST_DEVICE_BLOCK_SIZE as u64
        );

        let layer_set = store.tree.layer_set();
        let mut merger = layer_set.merger();
        let mut iter = merger.seek(Bound::Unbounded).await.expect("seek failed");
        let mut found = false;
        while let Some(ItemRef { key: ObjectKey { object_id, data }, .. }) = iter.get() {
            if let ObjectKeyData::Tombstone = data {
                assert!(!found);
                found = true;
            } else {
                assert!(*object_id != object.object_id(), "{:?}", iter.get());
            }
            iter.advance().await.expect("advance failed");
        }
        assert!(found);
        fs.close().await.expect("Close failed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_locks() {
        let (fs, object) = test_filesystem_and_object().await;
        let (send1, recv1) = channel();
        let (send2, recv2) = channel();
        let (send3, recv3) = channel();
        let done = Mutex::new(false);
        join!(
            async {
                let mut t = object.new_transaction().await.expect("new_transaction failed");
                send1.send(()).unwrap(); // Tell the next future to continue.
                send3.send(()).unwrap(); // Tell the last future to continue.
                recv2.await.unwrap();
                let mut buf = object.allocate_buffer(5);
                buf.as_mut_slice().copy_from_slice(b"hello");
                object.txn_write(&mut t, 0, buf.as_ref()).await.expect("write failed");
                // This is a halting problem so all we can do is sleep.
                fasync::Timer::new(Duration::from_millis(100)).await;
                assert!(!*done.lock().unwrap());
                t.commit().await;
            },
            async {
                recv1.await.unwrap();
                // Reads should not block.
                let offset = TEST_DATA_OFFSET as usize;
                let align = offset % TEST_DEVICE_BLOCK_SIZE as usize;
                let len = TEST_DATA.len();
                let mut buf = object.allocate_buffer(align + len);
                assert_eq!(
                    object.read((offset - align) as u64, buf.as_mut()).await.expect("read failed"),
                    align + TEST_DATA.len()
                );
                assert_eq!(&buf.as_slice()[align..], TEST_DATA);
                // Tell the first future to continue.
                send2.send(()).unwrap();
            },
            async {
                // This should block until the first future has completed.
                recv3.await.unwrap();
                let _t = object.new_transaction().await.expect("new_transaction failed");
                let mut buf = object.allocate_buffer(5);
                assert_eq!(object.read(0, buf.as_mut()).await.expect("read failed"), 5);
                assert_eq!(buf.as_slice(), b"hello");
            }
        );
        fs.close().await.expect("Close failed");
    }

    #[fasync::run(10, test)]
    async fn test_racy_reads() {
        let fs = test_filesystem().await;
        let object;
        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        let store = fs.root_store();
        object = Arc::new(
            ObjectStore::create_object(&store, &mut transaction, HandleOptions::default())
                .await
                .expect("create_object failed"),
        );
        transaction.commit().await;
        for _ in 0..100 {
            let cloned_object = object.clone();
            let writer = fasync::Task::spawn(async move {
                let mut buf = cloned_object.allocate_buffer(10);
                buf.as_mut_slice().fill(123);
                cloned_object.write(0, buf.as_ref()).await.expect("write failed");
            });
            let cloned_object = object.clone();
            let reader = fasync::Task::spawn(async move {
                let wait_time = rand::thread_rng().gen_range(0, 5);
                fasync::Timer::new(Duration::from_millis(wait_time)).await;
                let mut buf = cloned_object.allocate_buffer(10);
                buf.as_mut_slice().fill(23);
                let amount = cloned_object.read(0, buf.as_mut()).await.expect("write failed");
                // If we succeed in reading data, it must include the write; i.e. if we see the size
                // change, we should see the data too.  For this to succeed it requires locking on
                // the read size to ensure that when we read the size, we get the extents changed in
                // that same transaction.
                if amount != 0 {
                    assert_eq!(amount, 10);
                    assert_eq!(buf.as_slice(), &[123; 10]);
                }
            });
            writer.await;
            reader.await;
            let mut transaction = object.new_transaction().await.expect("new_transaction failed");
            object.truncate(&mut transaction, 0).await.expect("truncate failed");
            transaction.commit().await;
        }
        fs.close().await.expect("Close failed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_allocated_size() {
        let (fs, object) = test_filesystem_and_object().await;

        let before = object.get_allocated_size().await.expect("get_allocated_size failed");
        let mut buf = object.allocate_buffer(5);
        buf.as_mut_slice().copy_from_slice(b"hello");
        object.write(0, buf.as_ref()).await.expect("write failed");
        let after = object.get_allocated_size().await.expect("get_allocated_size failed");
        assert_eq!(after, before + TEST_DEVICE_BLOCK_SIZE as u64);

        // Do the same write again and there should be no change.
        object.write(0, buf.as_ref()).await.expect("write failed");
        assert_eq!(object.get_allocated_size().await.expect("get_allocated_size failed"), after);

        // extend...
        let mut transaction = object.new_transaction().await.expect("new_transaction failed");
        let offset = 1000 * TEST_DEVICE_BLOCK_SIZE as u64;
        let before = after;
        object
            .extend(&mut transaction, offset..offset + TEST_DEVICE_BLOCK_SIZE as u64)
            .await
            .expect("extend failed");
        transaction.commit().await;
        let after = object.get_allocated_size().await.expect("get_allocated_size failed");
        assert_eq!(after, before + TEST_DEVICE_BLOCK_SIZE as u64);

        // truncate...
        let mut transaction = object.new_transaction().await.expect("new_transaction failed");
        let before = after;
        let size = object.get_size();
        object
            .truncate(&mut transaction, size - TEST_DEVICE_BLOCK_SIZE as u64)
            .await
            .expect("extend failed");
        transaction.commit().await;
        let after = object.get_allocated_size().await.expect("get_allocated_size failed");
        assert_eq!(after, before - TEST_DEVICE_BLOCK_SIZE as u64);

        // preallocate_range...
        let mut transaction = object.new_transaction().await.expect("new_transaction failed");
        let before = after;
        object
            .preallocate_range(&mut transaction, offset..offset + TEST_DEVICE_BLOCK_SIZE as u64)
            .await
            .expect("extend failed");
        transaction.commit().await;
        let after = object.get_allocated_size().await.expect("get_allocated_size failed");
        assert_eq!(after, before + TEST_DEVICE_BLOCK_SIZE as u64);
        fs.close().await.expect("Close failed");
    }

    #[fasync::run(10, test)]
    async fn test_zero() {
        let (fs, object) = test_filesystem_and_object().await;
        let expected_size = object.get_size();
        let mut transaction = object.new_transaction().await.expect("new_transaction failed");
        object
            .zero(&mut transaction, 0..TEST_DEVICE_BLOCK_SIZE as u64 * 10)
            .await
            .expect("zero failed");
        transaction.commit().await;
        assert_eq!(object.get_size(), expected_size);
        let mut buf = object.allocate_buffer(TEST_DEVICE_BLOCK_SIZE as usize * 10);
        assert_eq!(object.read(0, buf.as_mut()).await.expect("read failed") as u64, expected_size);
        assert_eq!(
            &buf.as_slice()[0..expected_size as usize],
            vec![0u8; expected_size as usize].as_slice()
        );
        fs.close().await.expect("Close failed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_properties() {
        let (fs, object) = test_filesystem_and_object().await;
        const CRTIME: Timestamp = Timestamp::from_nanos(1234);
        const MTIME: Timestamp = Timestamp::from_nanos(5678);

        let mut transaction = object.new_transaction().await.expect("new_transaction failed");
        object
            .update_timestamps(Some(&mut transaction), Some(CRTIME), Some(MTIME))
            .await
            .expect("update_timestamps failed");
        transaction.commit().await;

        let properties = object.get_properties().await.expect("get_properties failed");
        assert_matches!(
            properties,
            ObjectProperties {
                refs: 1u64,
                allocated_size: TEST_OBJECT_ALLOCATED_SIZE,
                data_attribute_size: TEST_OBJECT_SIZE,
                creation_time: CRTIME,
                modification_time: MTIME,
                ..
            }
        );
        fs.close().await.expect("Close failed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_pending_properties() {
        let (fs, object) = test_filesystem_and_object().await;
        let crtime = Timestamp::from_nanos(1234u64);
        let mtime = Timestamp::from_nanos(5678u64);

        object
            .update_timestamps(None, Some(crtime.clone()), None)
            .await
            .expect("update_timestamps failed");
        let properties = object.get_properties().await.expect("get_properties failed");
        assert_eq!(properties.creation_time, crtime);
        assert_ne!(properties.modification_time, mtime);

        object
            .update_timestamps(None, None, Some(mtime.clone()))
            .await
            .expect("update_timestamps failed");
        let properties = object.get_properties().await.expect("get_properties failed");
        assert_eq!(properties.creation_time, crtime);
        assert_eq!(properties.modification_time, mtime);

        object
            .update_timestamps(None, None, Some(mtime.clone()))
            .await
            .expect("update_timestamps failed");
        let properties = object.get_properties().await.expect("get_properties failed");
        assert_eq!(properties.creation_time, crtime);
        assert_eq!(properties.modification_time, mtime);

        // Writes should flush the pending attrs, rather than using the current time (which would
        // change mtime).
        let mut buf = object.allocate_buffer(5);
        buf.as_mut_slice().copy_from_slice(b"hello");
        object.write(0, buf.as_ref()).await.expect("write failed");

        let properties = object.get_properties().await.expect("get_properties failed");
        assert_eq!(properties.creation_time, crtime);
        assert_eq!(properties.modification_time, mtime);
        fs.close().await.expect("Close failed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_item_sequences() {
        let fs = test_filesystem().await;
        let object1;
        let object2;
        let object3;
        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        let store = fs.root_store();
        object1 = Arc::new(
            ObjectStore::create_object(&store, &mut transaction, HandleOptions::default())
                .await
                .expect("create_object failed"),
        );
        transaction.commit().await;
        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        object2 = Arc::new(
            ObjectStore::create_object(&store, &mut transaction, HandleOptions::default())
                .await
                .expect("create_object failed"),
        );
        transaction.commit().await;

        fs.sync(SyncOptions::default()).await.expect("sync failed");

        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        object3 = Arc::new(
            ObjectStore::create_object(&store, &mut transaction, HandleOptions::default())
                .await
                .expect("create_object failed"),
        );
        transaction.commit().await;

        let layer_set = store.tree.layer_set();
        let mut merger = layer_set.merger();
        let mut iter = merger.seek(Bound::Unbounded).await.expect("seek failed");
        let mut sequences = [0u64; 3];
        while let Some(ItemRef { key: ObjectKey { object_id, .. }, sequence, .. }) = iter.get() {
            if *object_id == object1.object_id() {
                sequences[0] = sequence;
            } else if *object_id == object2.object_id() {
                sequences[1] = sequence;
            } else if *object_id == object3.object_id() {
                sequences[2] = sequence;
            }
            iter.advance().await.expect("advance failed");
        }

        assert!(sequences[0] <= sequences[1], "sequences: {:?}", sequences);
        // The last item came after a sync, so should be strictly greater.
        assert!(sequences[1] < sequences[2], "sequences: {:?}", sequences);
        fs.close().await.expect("Close failed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_create_and_open_store() {
        let fs = test_filesystem().await;
        let store_id = {
            let root_store = fs.root_store();
            let mut transaction = fs
                .clone()
                .new_transaction(&[], Options::default())
                .await
                .expect("new_transaction failed");
            let child_store = root_store
                .create_child_store(&mut transaction)
                .await
                .expect("create_child_store failed");
            transaction.commit().await;

            child_store.store_object_id()
        };

        fs.close().await.expect("close failed");
        let device = fs.take_device().await;
        device.reopen();
        let fs = FxFilesystem::open(device).await.expect("open failed");

        fs.object_manager().open_store(store_id).await.expect("open_store failed");
        fs.close().await.expect("Close failed");
    }

    #[fasync::run(10, test)]
    async fn test_concurrent_store_opening() {
        let fs = test_filesystem().await;
        let store_id = {
            let store = fs.root_store();
            let mut transaction = fs
                .clone()
                .new_transaction(&[], Options::default())
                .await
                .expect("new_transaction failed");
            let child_store = store
                .create_child_store_with_id(&mut transaction, 555u64)
                .await
                .expect("create_child_store failed");
            transaction.commit().await;
            child_store.store_object_id()
        };

        let mut fs = Some(fs);
        for _ in 0..20 {
            let device = {
                let fs = fs.unwrap();
                fs.close().await.expect("close failed");
                let device = fs.take_device().await;
                device.reopen();
                device
            };
            fs = Some(FxFilesystem::open(device).await.expect("open failed"));
            join_all((0..4).map(|_| {
                let manager = fs.as_ref().unwrap().object_manager();
                fasync::Task::spawn(async move {
                    manager.open_store(store_id).await.expect("open_store failed");
                })
            }))
            .await;
        }
        fs.unwrap().close().await.expect("Close failed");
    }
}

// TODO(csuter): validation of all deserialized structs.
// TODO(csuter): check all panic! calls.
