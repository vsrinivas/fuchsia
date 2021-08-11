// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod allocator;
pub mod caching_object_handle;
mod constants;
pub mod crypt;
pub mod data_buffer;
pub mod directory;
pub mod filesystem;
pub mod fsck;
mod graveyard;
mod journal;
mod merge;
pub mod object_manager;
mod record;
pub mod store_object_handle;
#[cfg(test)]
mod testing;
pub mod transaction;
mod tree;
#[cfg(target_os = "fuchsia")]
pub mod vmo_data_buffer;
pub mod volume;
mod writeback_cache;

pub use caching_object_handle::CachingObjectHandle;
pub use directory::Directory;
pub use filesystem::FxFilesystem;
pub use record::{ObjectDescriptor, Timestamp};
pub use store_object_handle::{round_down, round_up, StoreObjectHandle};

use {
    crate::{
        errors::FxfsError,
        lsm_tree::{
            layers_from_handles,
            types::{BoxedLayerIterator, Item, ItemRef, LayerIterator},
            LSMTree,
        },
        object_handle::{ObjectHandle, ObjectHandleExt, INVALID_OBJECT_ID},
        object_store::{
            data_buffer::{DataBufferFactory, NativeDataBuffer},
            filesystem::{Filesystem, Mutations},
            journal::checksum_list::ChecksumList,
            record::{
                Checksums, EncryptionKeys, ExtentKey, ExtentValue, ObjectItem, ObjectKey,
                ObjectKind, ObjectValue, DEFAULT_DATA_ATTRIBUTE_ID,
            },
            store_object_handle::DirectWriter,
            transaction::{
                AssocObj, AssociatedObject, ExtentMutation, LockKey, Mutation, ObjectStoreMutation,
                Operation, Options, StoreInfoMutation, Transaction,
            },
        },
        trace_duration,
    },
    allocator::Allocator,
    anyhow::{anyhow, bail, Context, Error},
    async_trait::async_trait,
    bincode::{deserialize_from, serialize_into},
    futures::{future::BoxFuture, FutureExt},
    interval_tree::utils::RangeOps,
    once_cell::sync::OnceCell,
    serde::{Deserialize, Serialize},
    std::{
        convert::TryFrom,
        ops::Bound,
        sync::{
            atomic::{self, AtomicU64},
            Arc, Mutex, Weak,
        },
    },
    storage_device::Device,
};

// StoreInfo stores information about the object store.  This is stored within the parent object
// store, and is used, for example, to get the persistent layer objects.
#[derive(Clone, Debug, Default, Serialize, Deserialize)]
pub struct StoreInfo {
    // The last used object ID.  Note that this field is not accurate in memory; ObjectStore's
    // last_object_id field is the one to use in that case.
    last_object_id: u64,

    // Object ids for layers.  TODO(csuter): need a layer of indirection here so we can
    // support snapshots.
    object_tree_layers: Vec<u64>,
    extent_tree_layers: Vec<u64>,

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
    extent_tree: LSMTree<ExtentKey, ExtentValue>,

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
    ) -> Arc<ObjectStore> {
        let device = filesystem.device();
        let block_size = filesystem.block_size();
        let store = Arc::new(ObjectStore {
            parent_store,
            store_object_id,
            device,
            block_size,
            filesystem: Arc::downgrade(&filesystem),
            last_object_id: AtomicU64::new(0),
            store_info: Mutex::new(store_info),
            tree: LSMTree::new(merge::merge),
            extent_tree: LSMTree::new(merge::merge_extents),
            store_info_handle: OnceCell::new(),
        });
        store
    }

    pub fn new_empty(
        parent_store: Option<Arc<ObjectStore>>,
        store_object_id: u64,
        filesystem: Arc<dyn Filesystem>,
    ) -> Arc<Self> {
        Self::new(parent_store, store_object_id, filesystem, Some(StoreInfo::default()))
    }

    pub fn device(&self) -> &Arc<dyn Device> {
        &self.device
    }

    pub fn block_size(&self) -> u32 {
        self.block_size
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

    pub fn extent_tree(&self) -> &LSMTree<ExtentKey, ExtentValue> {
        &self.extent_tree
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
            Some(0),
        )
        .await?;
        let fs = self.filesystem.upgrade().unwrap();
        let store = Self::new_empty(Some(self.clone()), handle.object_id(), fs.clone());
        assert!(store.store_info_handle.set(handle).is_ok());
        fs.object_manager().add_store(store.clone());
        Ok(store)
    }

    pub async fn open_object<S: AsRef<ObjectStore> + Send + Sync + 'static>(
        owner: &Arc<S>,
        object_id: u64,
        options: HandleOptions,
    ) -> Result<StoreObjectHandle<S>, Error> {
        let store = owner.as_ref().as_ref();
        store.ensure_open().await?;
        let keys = match store
            .tree
            .find(&ObjectKey::object(object_id))
            .await?
            .ok_or(FxfsError::NotFound)?
        {
            Item {
                value: ObjectValue::Object { kind: ObjectKind::File { keys, .. }, .. }, ..
            } => match keys {
                EncryptionKeys::None => None,
                EncryptionKeys::AES256XTS(keys) => {
                    Some(store.filesystem().crypt().unwrap_keys(&keys, object_id).await?)
                }
            },
            Item { value: ObjectValue::None, .. } => bail!(FxfsError::NotFound),
            _ => bail!(FxfsError::Inconsistent),
        };

        let item = store
            .tree
            .find(&ObjectKey::attribute(object_id, DEFAULT_DATA_ATTRIBUTE_ID))
            .await?
            .ok_or(FxfsError::NotFound)?;
        if let ObjectValue::Attribute { size } = item.value {
            Ok(StoreObjectHandle::new(
                owner.clone(),
                object_id,
                keys,
                DEFAULT_DATA_ATTRIBUTE_ID,
                size,
                options,
                false,
            ))
        } else {
            bail!(FxfsError::Inconsistent);
        }
    }

    async fn create_object_with_id<S: AsRef<ObjectStore> + Send + Sync + 'static>(
        owner: &Arc<S>,
        transaction: &mut Transaction<'_>,
        object_id: u64,
        options: HandleOptions,
        wrapping_key_id: Option<u64>,
    ) -> Result<StoreObjectHandle<S>, Error> {
        let store = owner.as_ref().as_ref();
        store.ensure_open().await?;
        let (keys, unwrapped_keys) = if let Some(wrapping_key_id) = wrapping_key_id {
            let (keys, unwrapped_keys) =
                store.filesystem().crypt().create_key(wrapping_key_id, object_id).await?;
            (EncryptionKeys::AES256XTS(keys), Some(unwrapped_keys))
        } else {
            (EncryptionKeys::None, None)
        };
        // If the object ID was specified i.e. this hasn't come via create_object, then we
        // should update last_object_id in case the caller wants to create more objects in
        // the same transaction.
        store.update_last_object_id(object_id);
        let now = Timestamp::now();
        transaction.add(
            store.store_object_id(),
            Mutation::insert_object(
                ObjectKey::object(object_id),
                ObjectValue::file(1, 0, now.clone(), now, keys),
            ),
        );
        transaction.add(
            store.store_object_id(),
            Mutation::insert_object(
                ObjectKey::attribute(object_id, DEFAULT_DATA_ATTRIBUTE_ID),
                ObjectValue::attribute(0),
            ),
        );
        Ok(StoreObjectHandle::new(
            owner.clone(),
            object_id,
            unwrapped_keys,
            DEFAULT_DATA_ATTRIBUTE_ID,
            0,
            options,
            false,
        ))
    }

    pub async fn create_object<S: AsRef<ObjectStore> + Send + Sync + 'static>(
        owner: &Arc<S>,
        mut transaction: &mut Transaction<'_>,
        options: HandleOptions,
        wrapping_key_id: Option<u64>,
    ) -> Result<StoreObjectHandle<S>, Error> {
        let object_id = owner.as_ref().as_ref().get_next_object_id();
        ObjectStore::create_object_with_id(
            owner,
            &mut transaction,
            object_id,
            options,
            wrapping_key_id,
        )
        .await
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
    pub async fn tombstone(&self, object_id: u64, txn_options: Options<'_>) -> Result<(), Error> {
        let fs = self.filesystem();
        let mut search_key = ExtentKey::new(object_id, 0, 0..0);
        // TODO(csuter): There should be a test that runs fsck after each transaction.
        loop {
            let mut transaction = fs.clone().new_transaction(&[], txn_options).await?;
            let next_key = self.delete_extents(&mut transaction, &search_key).await?;
            if next_key.is_none() {
                transaction.add(
                    self.store_object_id,
                    Mutation::merge_object(
                        ObjectKey::object(search_key.object_id),
                        ObjectValue::None,
                    ),
                );
                // Remove the object from the graveyard.
                self.filesystem().object_manager().graveyard().unwrap().remove(
                    &mut transaction,
                    self.store_object_id,
                    search_key.object_id,
                );
            }
            transaction.commit().await?;
            search_key = if let Some(next_key) = next_key {
                next_key
            } else {
                break;
            };
        }
        Ok(())
    }

    // Makes progress on deleting part of a file but stops before a transaction gets too big.
    async fn delete_extents(
        &self,
        transaction: &mut Transaction<'_>,
        search_key: &ExtentKey,
    ) -> Result<Option<ExtentKey>, Error> {
        let layer_set = self.extent_tree.layer_set();
        let mut merger = layer_set.merger();
        let allocator = self.allocator();
        let mut iter = merger.seek(Bound::Included(search_key)).await?;
        let mut delete_extent_mutation = None;
        // Loop over the extents and deallocate them.
        while let Some(ItemRef {
            key: ExtentKey { object_id, attribute_id, range }, value, ..
        }) = iter.get()
        {
            if *object_id != search_key.object_id {
                break;
            }
            if let ExtentValue::Some { device_offset, .. } = value {
                let device_range = *device_offset..*device_offset + (range.end - range.start);
                allocator.deallocate(transaction, device_range).await?;
                delete_extent_mutation = Some(Mutation::extent(
                    ExtentKey::new(search_key.object_id, *attribute_id, 0..range.end),
                    ExtentValue::deleted_extent(),
                ));
                // Stop if the transaction is getting too big.  At time of writing, this threshold
                // limits transactions to about 10,000 bytes.
                const TRANSACTION_MUTATION_THRESHOLD: usize = 200;
                if transaction.mutations.len() >= TRANSACTION_MUTATION_THRESHOLD {
                    transaction.add(self.store_object_id, delete_extent_mutation.unwrap());
                    return Ok(Some(ExtentKey::search_key_from_offset(
                        search_key.object_id,
                        *attribute_id,
                        range.end,
                    )));
                }
            }
            iter.advance().await?;
        }
        if let Some(m) = delete_extent_mutation {
            transaction.add(self.store_object_id, m);
        }
        Ok(None)
    }

    /// Returns all objects that exist in the parent store that pertain to this object store.
    pub fn parent_objects(&self) -> Vec<u64> {
        assert!(self.store_info_handle.get().is_some());
        let mut objects = Vec::new();
        // We should not include the ID of the store itself, since that should be referred to in the
        // volume directory.
        let guard = self.store_info.lock().unwrap();
        let store_info = guard.as_ref().unwrap();
        objects.extend_from_slice(&store_info.object_tree_layers);
        objects.extend_from_slice(&store_info.extent_tree_layers);
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
            let (object_tree_layer_object_ids, extent_tree_layer_object_ids) = loop {
                if let Some(store_info) = &*self.store_info.lock().unwrap() {
                    break (
                        store_info.object_tree_layers.clone(),
                        store_info.extent_tree_layers.clone(),
                    );
                }

                if handle.get_size() > 0 {
                    let serialized_info = handle.contents(MAX_STORE_INFO_SERIALIZED_SIZE).await?;
                    let store_info: StoreInfo = deserialize_from(&serialized_info[..])
                        .context("Failed to deserialize StoreInfo")?;
                    let layer_object_ids = (
                        store_info.object_tree_layers.clone(),
                        store_info.extent_tree_layers.clone(),
                    );
                    self.update_last_object_id(store_info.last_object_id);
                    *self.store_info.lock().unwrap() = Some(store_info);
                    break layer_object_ids;
                }

                // The store_info will be absent for a newly created and empty object store, since
                // there have been no StoreInfoMutations applied to it.
                break (vec![], vec![]);
            };

            let mut handles = Vec::new();
            let mut total_size = 0;
            for object_id in object_tree_layer_object_ids {
                let handle = CachingObjectHandle::new(
                    ObjectStore::open_object(&parent_store, object_id, HandleOptions::default())
                        .await?,
                );
                total_size += handle.get_size();
                handles.push(handle);
            }
            self.tree.append_layers(handles.into()).await?;

            let mut handles = Vec::new();
            for object_id in extent_tree_layer_object_ids {
                let handle = CachingObjectHandle::new(
                    ObjectStore::open_object(&parent_store, object_id, HandleOptions::default())
                        .await?,
                );
                total_size += handle.get_size();
                handles.push(handle);
            }
            self.extent_tree.append_layers(handles.into()).await?;

            let _ = self.store_info_handle.set(handle);
            self.filesystem().object_manager().update_reservation(self.store_object_id, total_size);
            Ok(())
        }
        .boxed()
    }

    fn get_next_object_id(&self) -> u64 {
        self.last_object_id.fetch_add(1, atomic::Ordering::Relaxed) + 1
    }

    pub fn allocator(&self) -> Arc<dyn Allocator> {
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
        if let Mutation::Extent(ExtentMutation(
            ExtentKey { range, .. },
            ExtentValue::Some { device_offset, checksums: Checksums::Fletcher(checksums), .. },
        )) = mutation
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
        Ok(true)
    }
}

impl DataBufferFactory for ObjectStore {
    fn create_data_buffer(&self, _object_id: u64, initial_size: u64) -> NativeDataBuffer {
        NativeDataBuffer::new(initial_size)
    }
}

// In a major compaction (i.e. a compaction which involves the base layer), we have an opportunity
// to apply a number of optimizations, such as removing tombstoned objects or deleted extents.
// These optimizations can only be applied after the compaction completes, thus we have an explicit
// iterator to apply these optimizations.
struct MajorCompactionIterator<'a, K, V, F> {
    iter: BoxedLayerIterator<'a, K, V>,
    can_discard: F,
}

impl<'a, K, V, F> MajorCompactionIterator<'a, K, V, F> {
    pub fn new(iter: BoxedLayerIterator<'a, K, V>, can_discard: F) -> Self {
        Self { iter, can_discard }
    }
}

#[async_trait]
impl<K: Send + Sync, V: Send + Sync, F: for<'b> Fn(ItemRef<'b, K, V>) -> bool + Send + Sync>
    LayerIterator<K, V> for MajorCompactionIterator<'_, K, V, F>
{
    async fn advance(&mut self) -> Result<(), Error> {
        self.iter.advance().await?;
        loop {
            match self.iter.get() {
                Some(item) if (self.can_discard)(item) => self.iter.advance().await?,
                _ => return Ok(()),
            }
        }
    }

    fn get(&self) -> Option<ItemRef<'_, K, V>> {
        self.iter.get()
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
            Mutation::BeginFlush => {
                self.tree.seal().await;
                self.extent_tree.seal().await;
            }
            Mutation::EndFlush => {
                if transaction.is_none() {
                    self.tree.reset_immutable_layers();
                    self.extent_tree.reset_immutable_layers();
                    // StoreInfo needs to be read from the store-info file.
                    *self.store_info.lock().unwrap() = None;
                } else {
                    let object_tree_layer_set = self.tree.immutable_layer_set();
                    let object_tree_handles =
                        object_tree_layer_set.layers.iter().map(|l| l.handle());
                    let extent_tree_layer_set = self.extent_tree.immutable_layer_set();
                    let extent_tree_handles =
                        extent_tree_layer_set.layers.iter().map(|l| l.handle());
                    self.filesystem().object_manager().update_reservation(
                        self.store_object_id,
                        object_tree_handles
                            .chain(extent_tree_handles)
                            .map(|h| h.map(ObjectHandle::get_size).unwrap_or(0))
                            .sum(),
                    );
                }
            }
            Mutation::Extent(ExtentMutation(key, value)) => {
                let item = Item::new_with_sequence(key, value, log_offset);
                let lower_bound = item.key.key_for_merge_into();
                self.extent_tree.merge_into(item, &lower_bound).await;
            }
            _ => panic!("unexpected mutation: {:?}", mutation), // TODO(csuter): can't panic
        }
    }

    fn drop_mutation(&self, _mutation: Mutation, _transaction: &Transaction<'_>) {}

    /// Push all in-memory structures to the device. This is not necessary for sync since the
    /// journal will take care of it.  This is supposed to be called when there is either memory or
    /// space pressure (flushing the store will persist in-memory data and allow the journal file to
    /// be trimmed).  This is not thread-safe insofar as calling flush from multiple threads at the
    /// same time is not safe.
    async fn flush(&self) -> Result<(), Error> {
        trace_duration!("ObjectStore::flush", "store_object_id" => self.store_object_id);
        if self.parent_store.is_none() {
            return Ok(());
        }
        self.ensure_open().await?;

        let filesystem = self.filesystem();
        let object_manager = filesystem.object_manager();
        if !object_manager.needs_flush(self.store_object_id) {
            return Ok(());
        }

        let parent_store = self.parent_store.as_ref().unwrap();
        let graveyard = object_manager.graveyard().ok_or(anyhow!("Missing graveyard!"))?;

        let reservation = object_manager.metadata_reservation();
        let txn_options = Options {
            skip_journal_checks: true,
            borrow_metadata_space: true,
            allocator_reservation: Some(reservation),
            ..Default::default()
        };
        let mut transaction = filesystem.clone().new_transaction(&[], txn_options).await?;

        let new_object_tree_layer = ObjectStore::create_object(
            parent_store,
            &mut transaction,
            HandleOptions { skip_journal_checks: true, ..Default::default() },
            Some(0),
        )
        .await?;
        let new_object_tree_layer_object_id = new_object_tree_layer.object_id();
        graveyard.add(
            &mut transaction,
            parent_store.store_object_id(),
            new_object_tree_layer_object_id,
        );

        let new_extent_tree_layer = ObjectStore::create_object(
            parent_store,
            &mut transaction,
            HandleOptions { skip_journal_checks: true, ..Default::default() },
            Some(0),
        )
        .await?;
        let new_extent_tree_layer_object_id = new_extent_tree_layer.object_id();
        graveyard.add(
            &mut transaction,
            parent_store.store_object_id(),
            new_extent_tree_layer_object_id,
        );

        transaction.add(self.store_object_id(), Mutation::BeginFlush);
        transaction.commit().await?;

        impl tree::MajorCompactable<ObjectKey, ObjectValue> for LSMTree<ObjectKey, ObjectValue> {
            fn major_iter(
                iter: BoxedLayerIterator<'_, ObjectKey, ObjectValue>,
            ) -> BoxedLayerIterator<'_, ObjectKey, ObjectValue> {
                Box::new(MajorCompactionIterator::new(iter, |item: ItemRef<'_, _, _>| match item {
                    ItemRef { value: ObjectValue::None, .. } => true,
                    _ => false,
                }))
            }
        }

        impl tree::MajorCompactable<ExtentKey, ExtentValue> for LSMTree<ExtentKey, ExtentValue> {
            fn major_iter(
                iter: BoxedLayerIterator<'_, ExtentKey, ExtentValue>,
            ) -> BoxedLayerIterator<'_, ExtentKey, ExtentValue> {
                Box::new(MajorCompactionIterator::new(iter, |item: ItemRef<'_, _, ExtentValue>| {
                    item.value.is_deleted()
                }))
            }
        }

        // TODO(jfsulliv): Add transaction::Options to CachingObjectHandle so that we can get rid of
        // DirectWriter and use the cached handle for both writing and reading.
        let (object_tree_layers_to_keep, old_object_tree_layers) =
            tree::flush(&self.tree, DirectWriter::new(&new_object_tree_layer, txn_options)).await?;
        let (extent_tree_layers_to_keep, old_extent_tree_layers) =
            tree::flush(&self.extent_tree, DirectWriter::new(&new_extent_tree_layer, txn_options))
                .await?;

        let mut new_object_tree_layers =
            layers_from_handles(Box::new([CachingObjectHandle::new(new_object_tree_layer)]))
                .await?;
        new_object_tree_layers.extend(object_tree_layers_to_keep.iter().map(|l| (*l).clone()));

        let mut new_extent_tree_layers =
            layers_from_handles(Box::new([CachingObjectHandle::new(new_extent_tree_layer)]))
                .await?;
        new_extent_tree_layers.extend(extent_tree_layers_to_keep.iter().map(|l| (*l).clone()));

        let mut serialized_info = Vec::new();
        let mut new_store_info = self.store_info();

        let mut transaction = filesystem.clone().new_transaction(&[], txn_options).await?;

        // Move the existing layers we're compacting to the graveyard.
        for layer in &old_object_tree_layers {
            if let Some(handle) = layer.handle() {
                graveyard.add(&mut transaction, parent_store.store_object_id(), handle.object_id());
            }
        }
        for layer in &old_extent_tree_layers {
            if let Some(handle) = layer.handle() {
                graveyard.add(&mut transaction, parent_store.store_object_id(), handle.object_id());
            }
        }

        new_store_info.last_object_id = self.last_object_id.load(atomic::Ordering::Relaxed);

        new_store_info.object_tree_layers = Vec::new();
        for layer in &new_object_tree_layers {
            if let Some(handle) = layer.handle() {
                new_store_info.object_tree_layers.push(handle.object_id());
            }
        }

        new_store_info.extent_tree_layers = Vec::new();
        for layer in &new_extent_tree_layers {
            if let Some(handle) = layer.handle() {
                new_store_info.extent_tree_layers.push(handle.object_id());
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
        transaction.add(self.store_object_id(), Mutation::EndFlush);
        graveyard.remove(
            &mut transaction,
            parent_store.store_object_id(),
            new_object_tree_layer_object_id,
        );
        graveyard.remove(
            &mut transaction,
            parent_store.store_object_id(),
            new_extent_tree_layer_object_id,
        );

        transaction
            .commit_with_callback(|_| {
                *self.store_info.lock().unwrap() = Some(new_store_info);
                self.tree.set_layers(new_object_tree_layers);
                self.extent_tree.set_layers(new_extent_tree_layers);
            })
            .await?;

        // Now close the layers and purge them.
        for layer in old_object_tree_layers {
            let object_id = layer.handle().map(|h| h.object_id());
            layer.close_layer().await;
            if let Some(object_id) = object_id {
                parent_store.tombstone(object_id, txn_options).await?;
            }
        }

        for layer in old_extent_tree_layers {
            let object_id = layer.handle().map(|h| h.object_id());
            layer.close_layer().await;
            if let Some(object_id) = object_id {
                parent_store.tombstone(object_id, txn_options).await?;
            }
        }

        Ok(())
    }
}

impl AsRef<ObjectStore> for ObjectStore {
    fn as_ref(&self) -> &ObjectStore {
        self
    }
}

impl AssociatedObject for ObjectStore {}
#[cfg(test)]
mod tests {
    use {
        crate::{
            errors::FxfsError,
            lsm_tree::types::{Item, ItemRef, LayerIterator},
            object_handle::{ObjectHandle, WriteObjectHandle},
            object_store::{
                crypt::InsecureCrypt,
                directory::Directory,
                filesystem::{Filesystem, FxFilesystem, Mutations, OpenFxFilesystem, SyncOptions},
                fsck::fsck,
                record::{ExtentKey, ExtentValue, ObjectKey, ObjectValue},
                transaction::{Options, TransactionHandler},
                HandleOptions, ObjectStore,
            },
        },
        fuchsia_async as fasync,
        futures::{future::join_all, join},
        matches::assert_matches,
        std::{
            ops::Bound,
            sync::{Arc, Mutex},
            time::Duration,
        },
        storage_device::{fake_device::FakeDevice, DeviceHolder},
    };

    const TEST_DEVICE_BLOCK_SIZE: u32 = 512;

    async fn test_filesystem() -> OpenFxFilesystem {
        let device = DeviceHolder::new(FakeDevice::new(8192, TEST_DEVICE_BLOCK_SIZE));
        FxFilesystem::new_empty(device, Arc::new(InsecureCrypt::new()))
            .await
            .expect("new_empty failed")
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
            ObjectStore::create_object(&store, &mut transaction, HandleOptions::default(), Some(0))
                .await
                .expect("create_object failed"),
        );
        transaction.commit().await.expect("commit failed");
        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        object2 = Arc::new(
            ObjectStore::create_object(&store, &mut transaction, HandleOptions::default(), Some(0))
                .await
                .expect("create_object failed"),
        );
        transaction.commit().await.expect("commit failed");

        fs.sync(SyncOptions::default()).await.expect("sync failed");

        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        object3 = Arc::new(
            ObjectStore::create_object(&store, &mut transaction, HandleOptions::default(), Some(0))
                .await
                .expect("create_object failed"),
        );
        transaction.commit().await.expect("commit failed");

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
            transaction.commit().await.expect("commit failed");

            child_store.store_object_id()
        };

        fs.close().await.expect("close failed");
        let device = fs.take_device().await;
        device.reopen();
        let fs =
            FxFilesystem::open(device, Arc::new(InsecureCrypt::new())).await.expect("open failed");

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
            transaction.commit().await.expect("commit failed");
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
            fs = Some(
                FxFilesystem::open(device, Arc::new(InsecureCrypt::new()))
                    .await
                    .expect("open failed"),
            );
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

    #[fasync::run(10, test)]
    async fn test_old_layers_are_purged() {
        let fs = test_filesystem().await;

        let store = fs.root_store();
        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        let object = Arc::new(
            ObjectStore::create_object(&store, &mut transaction, HandleOptions::default(), Some(0))
                .await
                .expect("create_object failed"),
        );
        transaction.commit().await.expect("commit failed");

        store.flush().await.expect("flush failed");

        let mut buf = object.allocate_buffer(5);
        buf.as_mut_slice().copy_from_slice(b"hello");
        object.write_or_append(Some(0), buf.as_ref()).await.expect("write failed");

        // Getting the layer-set should cause the flush to stall.
        let layer_set = store.tree().layer_set();

        let done = Mutex::new(false);
        let mut object_id = 0;

        join!(
            async {
                store.flush().await.expect("flush failed");
                assert!(*done.lock().unwrap());
            },
            async {
                // This is a halting problem so all we can do is sleep.
                fasync::Timer::new(Duration::from_secs(1)).await;
                *done.lock().unwrap() = true;
                object_id = layer_set.layers.last().unwrap().handle().unwrap().object_id();
                std::mem::drop(layer_set);
            }
        );

        if let Err(e) = ObjectStore::open_object(
            &store.parent_store.as_ref().unwrap(),
            object_id,
            HandleOptions::default(),
        )
        .await
        {
            assert!(FxfsError::NotFound.matches(&e));
        } else {
            panic!("open_object succeeded");
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_tombstone_deletes_data() {
        let fs = test_filesystem().await;
        let root_store = fs.root_store();
        let child_id = {
            let mut transaction = fs
                .clone()
                .new_transaction(&[], Options::default())
                .await
                .expect("new_transaction failed");
            let child = ObjectStore::create_object(
                &root_store,
                &mut transaction,
                HandleOptions::default(),
                Some(0),
            )
            .await
            .expect("create_child failed");
            transaction.commit().await.expect("commit failed");

            // Allocate an extent in the file.
            let mut buffer = child.allocate_buffer(8192);
            buffer.as_mut_slice().fill(0xaa);
            child.write_or_append(Some(0), buffer.as_ref()).await.expect("write failed");

            child.object_id()
        };

        root_store.tombstone(child_id, Options::default()).await.expect("tombstone failed");

        let layers = root_store.extent_tree.layer_set();
        let mut merger = layers.merger();
        let mut iter = merger
            .seek(Bound::Included(&ExtentKey::new(child_id, 0, 0..8192).search_key()))
            .await
            .expect("seek failed");
        assert_matches!(iter.get(), Some(ItemRef { value: ExtentValue::None, .. }));
        iter.advance().await.expect("advance failed");
        assert_matches!(iter.get(), None);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_major_compaction_discards_unnecessary_records() {
        let fs = test_filesystem().await;
        let root_store = fs.root_store();
        let child_id = {
            let mut transaction = fs
                .clone()
                .new_transaction(&[], Options::default())
                .await
                .expect("new_transaction failed");
            let child = ObjectStore::create_object(
                &root_store,
                &mut transaction,
                HandleOptions::default(),
                Some(0),
            )
            .await
            .expect("create_child failed");
            transaction.commit().await.expect("commit failed");

            // Allocate an extent in the file.
            let mut buffer = child.allocate_buffer(8192);
            buffer.as_mut_slice().fill(0xaa);
            child.write_or_append(Some(0), buffer.as_ref()).await.expect("write failed");

            child.object_id()
        };

        let has_deleted_extent_records = |root_store: Arc<ObjectStore>, child_id| async move {
            let layers = root_store.extent_tree.layer_set();
            let mut merger = layers.merger();
            let mut iter = merger
                .seek(Bound::Included(&ExtentKey::new(child_id, 0, 0..1).search_key()))
                .await
                .expect("seek failed");
            loop {
                match iter.get() {
                    None => return false,
                    Some(ItemRef {
                        key: ExtentKey { object_id, .. },
                        value: ExtentValue::None,
                        ..
                    }) if *object_id == child_id => return true,
                    _ => {}
                }
                iter.advance().await.expect("advance failed");
            }
        };

        root_store.tombstone(child_id, Options::default()).await.expect("tombstone failed");
        assert_matches!(
            root_store.tree.find(&ObjectKey::object(child_id)).await.expect("find failed"),
            Some(Item { value: ObjectValue::None, .. })
        );
        assert!(has_deleted_extent_records(root_store.clone(), child_id).await);

        root_store.flush().await.expect("flush failed");
        assert_matches!(
            root_store.tree.find(&ObjectKey::object(child_id)).await.expect("find failed"),
            None
        );
        assert!(!has_deleted_extent_records(root_store.clone(), child_id).await);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_overlapping_extents_in_different_layers() {
        let fs = test_filesystem().await;
        let store = fs.root_store();
        let root_directory =
            Directory::open(&store, store.root_directory_object_id()).await.expect("open failed");

        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        let object = root_directory
            .create_child_file(&mut transaction, "test")
            .await
            .expect("create_child_file failed");
        transaction.commit().await.expect("commit failed");

        let buf = object.allocate_buffer(16384);
        object.write_or_append(Some(0), buf.as_ref()).await.expect("write failed");

        store.flush().await.expect("flush failed");

        object.write_or_append(Some(0), buf.subslice(0..4096)).await.expect("write failed");

        // At this point, we should have an extent for 0..16384 in a layer that has been flushed,
        // and an extent for 0..4096 that partially overwrites it.  Writing to 0..16384 should
        // overwrite both of those extents.
        object.write_or_append(Some(0), buf.as_ref()).await.expect("write failed");

        fsck(&fs).await.expect("fsck failed");
    }
}

// TODO(csuter): validation of all deserialized structs.
// TODO(csuter): check all panic! calls.
