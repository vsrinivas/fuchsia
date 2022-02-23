// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod allocator;
pub mod caching_object_handle;
mod constants;
pub mod data_buffer;
pub mod directory;
mod extent_record;
pub mod filesystem;
pub mod fsck;
mod graveyard;
mod journal;
mod merge;
pub mod object_manager;
mod object_record;
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
pub use object_record::{ObjectDescriptor, Timestamp};
pub use store_object_handle::StoreObjectHandle;

use {
    crate::{
        crypt::{Crypt, StreamCipher, WrappedKeys},
        debug_assert_not_too_long,
        errors::FxfsError,
        lsm_tree::{
            layers_from_handles,
            types::{BoxedLayerIterator, Item, ItemRef, LayerIterator},
            LSMTree,
        },
        object_handle::{ObjectHandle, ObjectHandleExt, WriteObjectHandle, INVALID_OBJECT_ID},
        object_store::{
            data_buffer::{DataBuffer, MemDataBuffer},
            extent_record::{Checksums, DEFAULT_DATA_ATTRIBUTE_ID},
            filesystem::{ApplyContext, ApplyMode, Filesystem, Mutations},
            journal::{checksum_list::ChecksumList, JournalCheckpoint},
            object_manager::{ObjectManager, ReservationUpdate},
            object_record::{EncryptionKeys, ObjectKind},
            store_object_handle::DirectWriter,
            transaction::{
                AssocObj, AssociatedObject, ExtentMutation, LockKey, Mutation, NoOrd,
                ObjectStoreMutation, Operation, Options, StoreInfoMutation, Transaction,
            },
        },
        range::RangeExt,
        serialized_types::{Versioned, VersionedLatest},
        trace_duration,
    },
    allocator::Allocator,
    anyhow::{anyhow, bail, Context, Error},
    async_trait::async_trait,
    once_cell::sync::OnceCell,
    serde::{Deserialize, Serialize},
    std::{
        collections::VecDeque,
        convert::TryFrom,
        ops::Bound,
        sync::{
            atomic::{AtomicBool, Ordering},
            Arc, Mutex, Weak,
        },
    },
    storage_device::Device,
};

// Exposed for serialized_types.
pub use allocator::{AllocatorInfo, AllocatorKey, AllocatorValue};
pub use extent_record::{ExtentKey, ExtentValue};
pub use journal::{JournalRecord, SuperBlock, SuperBlockRecord};
pub use object_record::{ObjectKey, ObjectValue};

/// StoreObjectHandle stores an owner that must implement this trait, which allows the handle to get
/// back to an ObjectStore and provides a callback for creating a data buffer for the handle.
pub trait HandleOwner: AsRef<ObjectStore> + Send + Sync + 'static {
    type Buffer: DataBuffer;

    fn create_data_buffer(&self, object_id: u64, initial_size: u64) -> Self::Buffer;
}

// StoreInfo stores information about the object store.  This is stored within the parent object
// store, and is used, for example, to get the persistent layer objects.
#[derive(Clone, Debug, Default, Serialize, Deserialize, Versioned)]
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

    // The number of live objects in the store.
    object_count: u64,

    // The (wrapped) key that encrypted mutations should use.
    mutations_key: Option<WrappedKeys>,

    // Mutations for the store are encrypted using a stream cipher.  To decrypt the mutations, we
    // need to know the offset in the cipher stream to start it.
    mutations_cipher_offset: u64,

    // If we have to flush the store whilst we do not have the key, we need to write the encrypted
    // mutations to an object. This is the object ID of that file if it exists.
    encrypted_mutations_object_id: u64,
}

// TODO(csuter): We should test or put checks in place to ensure this limit isn't exceeded.  It
// will likely involve placing limits on the maximum number of layers.
const MAX_STORE_INFO_SERIALIZED_SIZE: usize = 131072;

const MAX_ENCRYPTED_MUTATIONS_SIZE: usize = journal::RECLAIM_SIZE as usize;

#[derive(Default)]
pub struct HandleOptions {
    /// If true, transactions used by this handle will skip journal space checks.
    pub skip_journal_checks: bool,
}

#[derive(Debug, Default, Deserialize, Serialize, Versioned)]
pub struct EncryptedMutations {
    // Information about the mutations are held here, but the actual encrypted data is held within
    // data.  For each transaction, we record the checkpoint and the count of mutations within the
    // transaction.  The checkpoint is required for the log file offset (which we need to apply the
    // mutations), and the version so that we can correctly decode the mutation after it has been
    // decrypted.
    transactions: Vec<(JournalCheckpoint, u64)>,

    // The encrypted mutations.
    data: Vec<u8>,
}

impl EncryptedMutations {
    fn is_empty(&self) -> bool {
        self.transactions.is_empty()
    }

    fn extend(&mut self, other: EncryptedMutations) {
        self.transactions.extend(other.transactions);
        self.data.extend(other.data);
    }

    fn extend_from(&mut self, other: &EncryptedMutations) {
        self.transactions.extend_from_slice(&other.transactions);
        self.data.extend_from_slice(&other.data);
    }

    fn push(&mut self, checkpoint: &JournalCheckpoint, data: Box<[u8]>) {
        self.data.append(&mut data.into());
        // If the checkpoint is the same as the last mutation we pushed, increment the count.
        if let Some((last_checkpoint, count)) = self.transactions.last_mut() {
            if last_checkpoint.file_offset == checkpoint.file_offset {
                *count += 1;
                return;
            }
        }
        self.transactions.push((checkpoint.clone(), 1));
    }
}

// Whilst we are replaying the store, we need to keep track of changes to StoreInfo that arise from
// mutations in the journal stream that don't include all the fields in StoreInfo.  After replay has
// finished, we load the full store information and merge it with the deltas here.
// NOTE: While changing this struct, make sure to also update fsck::Fsck::check_child_store if
// needed, which currently doesn't bother replaying this information.
#[derive(Debug, Default)]
struct ReplayInfo {
    last_object_id: u64,
    object_count_delta: i64,
    root_directory: Option<u64>,
    graveyard_directory: Option<u64>,

    // Encrypted mutations consist of an array of log offsets (1 per mutation), followed by the
    // mutations.
    encrypted_mutations: EncryptedMutations,
}

impl ReplayInfo {
    fn new() -> VecDeque<ReplayInfo> {
        let mut info = VecDeque::new();
        info.push_back(ReplayInfo::default());
        info
    }
}

enum StoreOrReplayInfo {
    Info(StoreInfo),

    // When we flush a store, we take a snapshot of store information when we begin flushing, and
    // that snapshot gets committed when we end flushing.  In the intervening period, we need to
    // record any changes made to store information.  If during replay we don't get around to ending
    // the flush, we need to hang on to the deltas that were applied before we started flushing.
    // This is why the information is stored in a VecDeque.  The frontmost element is always the
    // most recent.
    Replay(VecDeque<ReplayInfo>),
}

impl StoreOrReplayInfo {
    fn last_object_id(&mut self) -> &mut u64 {
        match self {
            StoreOrReplayInfo::Info(StoreInfo { last_object_id, .. }) => last_object_id,
            StoreOrReplayInfo::Replay(replay_info) => {
                &mut replay_info.front_mut().unwrap().last_object_id
            }
        }
    }

    fn set_root_directory(&mut self, oid: u64) {
        match self {
            StoreOrReplayInfo::Info(StoreInfo { root_directory_object_id, .. }) => {
                *root_directory_object_id = oid
            }
            StoreOrReplayInfo::Replay(replay_info) => {
                replay_info.front_mut().unwrap().root_directory = Some(oid);
            }
        }
    }

    fn set_graveyard_directory(&mut self, oid: u64) {
        match self {
            StoreOrReplayInfo::Info(StoreInfo { graveyard_directory_object_id, .. }) => {
                *graveyard_directory_object_id = oid
            }
            StoreOrReplayInfo::Replay(replay_info) => {
                replay_info.front_mut().unwrap().graveyard_directory = Some(oid);
            }
        }
    }

    fn info(&self) -> Option<&StoreInfo> {
        match self {
            StoreOrReplayInfo::Info(info) => Some(info),
            _ => None,
        }
    }

    fn info_mut(&mut self) -> Option<&mut StoreInfo> {
        match self {
            StoreOrReplayInfo::Info(info) => Some(info),
            _ => None,
        }
    }

    fn replay_info_mut(&mut self) -> Option<&mut VecDeque<ReplayInfo>> {
        match self {
            StoreOrReplayInfo::Replay(info) => Some(info),
            _ => None,
        }
    }

    fn adjust_object_count(&mut self, delta: i64) {
        match self {
            StoreOrReplayInfo::Info(StoreInfo { object_count, .. }) => {
                if delta < 0 {
                    *object_count = object_count.saturating_sub(-delta as u64);
                } else {
                    *object_count = object_count.saturating_add(delta as u64);
                }
            }
            StoreOrReplayInfo::Replay(replay_info) => {
                replay_info.front_mut().unwrap().object_count_delta += delta;
            }
        }
    }

    fn begin_flush(&mut self) {
        if let StoreOrReplayInfo::Replay(replay_info) = self {
            // Push a new record on the front keeping the old one.  We'll remove the old one when
            // `end_flush` is called.
            replay_info.push_front(ReplayInfo::default());
        }
    }

    fn end_flush(&mut self) {
        if let StoreOrReplayInfo::Replay(replay_info) = self {
            replay_info.truncate(1);
        }
    }

    fn push_encrypted_mutation(&mut self, checkpoint: &JournalCheckpoint, data: Box<[u8]>) {
        if let StoreOrReplayInfo::Replay(replay_info) = self {
            replay_info.front_mut().unwrap().encrypted_mutations.push(checkpoint, data);
        }
    }
}

pub enum LockState {
    Locked,
    Unencrypted,
    Unlocked(Arc<dyn Crypt>),
}

/// An object store supports a file like interface for objects.  Objects are keyed by a 64 bit
/// identifier.  And object store has to be backed by a parent object store (which stores metadata
/// for the object store).  The top-level object store (a.k.a. the root parent object store) is
/// in-memory only.
pub struct ObjectStore {
    parent_store: Option<Arc<ObjectStore>>,
    store_object_id: u64,
    device: Arc<dyn Device>,
    block_size: u64,
    filesystem: Weak<dyn Filesystem>,
    store_info: Mutex<StoreOrReplayInfo>,
    tree: LSMTree<ObjectKey, ObjectValue>,
    extent_tree: LSMTree<ExtentKey, ExtentValue>,

    // When replaying the journal, the store cannot read StoreInfo until the whole journal
    // has been replayed, so during that time, store_info_handle will be None and records
    // just get sent to the tree. Once the journal has been replayed, we can open the store
    // and load all the other layer information.
    store_info_handle: OnceCell<StoreObjectHandle<ObjectStore>>,

    // The cipher to use for encrypted mutations, if this store is encrypted.
    mutations_cipher: Mutex<Option<StreamCipher>>,

    // Any encrypted mutations that exist after replaying an encrypted store, but before it is
    // unlocked.
    encrypted_mutations: Mutex<Option<Arc<EncryptedMutations>>>,

    // Holds the current lock state of the store.
    lock_state: Mutex<LockState>,

    trace: AtomicBool,
}

impl ObjectStore {
    fn new(
        parent_store: Option<Arc<ObjectStore>>,
        store_object_id: u64,
        filesystem: Arc<dyn Filesystem>,
        store_info: Option<StoreInfo>,
        mutations_cipher: Option<StreamCipher>,
        lock_state: LockState,
    ) -> Arc<ObjectStore> {
        let device = filesystem.device();
        let block_size = filesystem.block_size();
        let store = Arc::new(ObjectStore {
            parent_store,
            store_object_id,
            device,
            block_size,
            filesystem: Arc::downgrade(&filesystem),
            store_info: Mutex::new(match store_info {
                Some(info) => StoreOrReplayInfo::Info(info),
                None => StoreOrReplayInfo::Replay(ReplayInfo::new()),
            }),
            tree: LSMTree::new(merge::merge),
            extent_tree: LSMTree::new(merge::merge_extents),
            store_info_handle: OnceCell::new(),
            mutations_cipher: Mutex::new(mutations_cipher),
            encrypted_mutations: Mutex::new(None),
            lock_state: Mutex::new(lock_state),
            trace: AtomicBool::new(false),
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
            None,
            LockState::Unencrypted,
        )
    }

    /// Creating an encrypted store is a multi-step process:
    ///
    ///   1. Create an object for the store.
    ///   2. Call `ObjectStore::new_encrypted`.
    ///   3. Call `ObjectStore::create` to write the store.
    ///   4. If successful, register the store with object_manager.
    ///
    /// It requires the first three steps to be separate because of lifetime issues when working
    /// with a transaction.
    pub async fn new_encrypted(
        parent_store: Arc<ObjectStore>,
        handle: StoreObjectHandle<ObjectStore>,
        crypt: Arc<dyn Crypt>,
    ) -> Result<Arc<Self>, Error> {
        let filesystem = parent_store.filesystem();
        let (wrapped_keys, unwrapped_keys) = crypt.create_key(handle.object_id()).await?;
        let store = Self::new(
            Some(parent_store),
            handle.object_id(),
            filesystem.clone(),
            Some(StoreInfo { mutations_key: Some(wrapped_keys), ..Default::default() }),
            Some(StreamCipher::new(&unwrapped_keys[0], 0)),
            LockState::Unlocked(crypt),
        );
        assert!(store.store_info_handle.set(handle).is_ok());
        Ok(store)
    }

    /// Actually creates the store in a transaction.  This will create a root directory for the
    /// store.  See `new_encrypted` above.
    pub async fn create<'a>(
        self: &'a Arc<Self>,
        transaction: &mut Transaction<'a>,
    ) -> Result<(), Error> {
        let buf = {
            // Create a root directory.
            let root_directory = Directory::create(transaction, &self).await?;

            let mut store_info = self.store_info.lock().unwrap();
            let mut store_info = store_info.info_mut().unwrap();

            store_info.root_directory_object_id = root_directory.object_id();

            let mut serialized_info = Vec::new();
            store_info.serialize_with_version(&mut serialized_info)?;
            let mut buf = self.device.allocate_buffer(serialized_info.len());
            buf.as_mut_slice().copy_from_slice(&serialized_info[..]);
            buf
        };

        self.store_info_handle.get().unwrap().txn_write(transaction, 0u64, buf.as_ref()).await
    }

    pub fn set_trace(&self, trace: bool) {
        let old_value = self.trace.swap(trace, Ordering::Relaxed);
        if trace != old_value {
            log::info!(
                "OS {} tracing {}",
                self.store_object_id(),
                if trace { "enabled" } else { "disabled" },
            );
        }
    }

    pub fn is_root(&self) -> bool {
        if let Some(parent) = &self.parent_store {
            parent.parent_store.is_none()
        } else {
            // The root parent store isn't the root store.
            false
        }
    }

    pub fn device(&self) -> &Arc<dyn Device> {
        &self.device
    }

    pub fn block_size(&self) -> u64 {
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
        self.store_info.lock().unwrap().info().unwrap().root_directory_object_id
    }

    pub fn set_root_directory_object_id<'a>(&'a self, transaction: &mut Transaction<'a>, oid: u64) {
        transaction.add(self.store_object_id, Mutation::root_directory(oid));
    }

    pub fn graveyard_directory_object_id(&self) -> u64 {
        self.store_info.lock().unwrap().info().unwrap().graveyard_directory_object_id
    }

    pub fn set_graveyard_directory_object_id<'a>(&'a self, oid: u64) {
        self.store_info.lock().unwrap().info_mut().unwrap().graveyard_directory_object_id = oid;
    }

    pub fn object_count(&self) -> u64 {
        self.store_info.lock().unwrap().info().unwrap().object_count
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
        // TODO(csuter): if the transaction rolls back, we need to delete the store.
        let handle = ObjectStore::create_object_with_id(
            self,
            transaction,
            object_id,
            HandleOptions::default(),
            None,
        )
        .await?;
        let fs = self.filesystem.upgrade().unwrap();
        let store = Self::new_empty(Some(self.clone()), handle.object_id(), fs.clone());
        assert!(store.store_info_handle.set(handle).is_ok());
        fs.object_manager().add_store(store.clone());
        Ok(store)
    }

    /// Returns the crypt object for the store. Returns None if the store is unencrypted. This will
    /// panic if the store is locked.
    pub fn crypt(&self) -> Option<Arc<dyn Crypt>> {
        match &*self.lock_state.lock().unwrap() {
            LockState::Locked => panic!("Store is locked"),
            LockState::Unencrypted => None,
            LockState::Unlocked(crypt) => Some(crypt.clone()),
        }
    }

    /// Returns the file size for the object without opening the object.
    pub async fn get_file_size(&self, object_id: u64) -> Result<u64, Error> {
        let item = self
            .tree
            .find(&ObjectKey::attribute(object_id, DEFAULT_DATA_ATTRIBUTE_ID))
            .await?
            .ok_or(FxfsError::NotFound)?;
        if let ObjectValue::Attribute { size } = item.value {
            Ok(size)
        } else {
            bail!(FxfsError::NotFile);
        }
    }

    /// `crypt` can be provided if the crypt service should be different to the default; see the
    /// comment on create_object.
    pub async fn open_object<S: HandleOwner>(
        owner: &Arc<S>,
        object_id: u64,
        options: HandleOptions,
        mut crypt: Option<&dyn Crypt>,
    ) -> Result<StoreObjectHandle<S>, Error> {
        let store = owner.as_ref().as_ref();
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
                    let store_crypt;
                    if crypt.is_none() {
                        store_crypt = store.crypt();
                        crypt = store_crypt.as_deref();
                    }
                    Some(crypt.ok_or(FxfsError::Inconsistent)?.unwrap_keys(&keys, object_id).await?)
                }
            },
            Item { value: ObjectValue::None, .. } => bail!(FxfsError::NotFound),
            _ => {
                bail!(anyhow!(FxfsError::Inconsistent).context("open_object: Expected object value"))
            }
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
            bail!(anyhow!(FxfsError::Inconsistent).context("open_object: Expected attribute"));
        }
    }

    // See the comment on create_object for the semantics of the `crypt` argument.
    async fn create_object_with_id<S: HandleOwner>(
        owner: &Arc<S>,
        transaction: &mut Transaction<'_>,
        object_id: u64,
        options: HandleOptions,
        mut crypt: Option<&dyn Crypt>,
    ) -> Result<StoreObjectHandle<S>, Error> {
        let store = owner.as_ref().as_ref();
        let store_crypt;
        if crypt.is_none() {
            store_crypt = store.crypt();
            crypt = store_crypt.as_deref();
        }
        let (keys, unwrapped_keys) = if let Some(crypt) = crypt {
            let (keys, unwrapped_keys) = crypt.create_key(object_id).await?;
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

    /// There are instances where a store might not be an encrypted store, but the object should
    /// still be encrypted.  For example, the layer files for child stores should be encrypted using
    /// the crypt service of the child store even though the root store doesn't have encryption.  If
    /// `crypt` is None, the default for the store is used.
    pub async fn create_object<S: HandleOwner>(
        owner: &Arc<S>,
        mut transaction: &mut Transaction<'_>,
        options: HandleOptions,
        crypt: Option<&dyn Crypt>,
    ) -> Result<StoreObjectHandle<S>, Error> {
        let object_id = owner.as_ref().as_ref().get_next_object_id();
        ObjectStore::create_object_with_id(owner, &mut transaction, object_id, options, crypt).await
    }

    /// Adjusts the reference count for a given object.  If the reference count reaches zero, the
    /// object is moved into the graveyard and true is returned.
    pub async fn adjust_refs(
        &self,
        transaction: &mut Transaction<'_>,
        oid: u64,
        delta: i64,
    ) -> Result<bool, Error> {
        let mut mutation = self.txn_get_object_mutation(transaction, oid).await?;
        let refs = if let ObjectValue::Object { kind: ObjectKind::File { refs, .. }, .. } =
            &mut mutation.item.value
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
            self.add_to_graveyard(transaction, oid);

            // We might still need to adjust the reference count if delta was something other than
            // -1.
            if delta != -1 {
                *refs = 1;
                transaction.add(self.store_object_id, Mutation::ObjectStore(mutation));
            }
            Ok(true)
        } else {
            transaction.add(self.store_object_id, Mutation::ObjectStore(mutation));
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
                // Tombstone records *must* be merged so as to consume all other records for the
                // object.
                transaction.add(
                    self.store_object_id,
                    Mutation::merge_object(
                        ObjectKey::object(search_key.object_id),
                        ObjectValue::None,
                    ),
                );

                self.remove_from_graveyard(&mut transaction, search_key.object_id);
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
        let store_info = guard.info().unwrap();
        objects.extend_from_slice(&store_info.object_tree_layers);
        objects.extend_from_slice(&store_info.extent_tree_layers);
        if store_info.encrypted_mutations_object_id != INVALID_OBJECT_ID {
            objects.push(store_info.encrypted_mutations_object_id);
        }
        objects
    }

    /// Returns root objects for this store.
    pub fn root_objects(&self) -> Vec<u64> {
        let mut objects = Vec::new();
        let store_info = self.store_info.lock().unwrap();
        let info = store_info.info().unwrap();
        if info.root_directory_object_id != INVALID_OBJECT_ID {
            objects.push(info.root_directory_object_id);
        }
        if info.graveyard_directory_object_id != INVALID_OBJECT_ID {
            objects.push(info.graveyard_directory_object_id);
        }
        objects
    }

    pub fn store_info(&self) -> StoreInfo {
        self.store_info.lock().unwrap().info().unwrap().clone()
    }

    /// Returns None if called during journal replay.
    pub fn store_info_handle_object_id(&self) -> Option<u64> {
        self.store_info_handle.get().map(|h| h.object_id())
    }

    /// Called when replay for a store has completed.
    pub async fn on_replay_complete(&self) -> Result<(), Error> {
        if self.parent_store.is_none() || self.store_info_handle.get().is_some() {
            return Ok(());
        }

        let parent_store = self.parent_store.as_ref().unwrap();
        let handle = ObjectStore::open_object(
            &parent_store,
            self.store_object_id,
            HandleOptions::default(),
            None,
        )
        .await?;

        let mut encrypted_mutations = EncryptedMutations::default();

        let (object_tree_layer_object_ids, extent_tree_layer_object_ids, encrypted) = {
            let mut info = if handle.get_size() > 0 {
                let serialized_info = handle.contents(MAX_STORE_INFO_SERIALIZED_SIZE).await?;
                let mut cursor = std::io::Cursor::new(&serialized_info[..]);
                let (store_info, _version) = StoreInfo::deserialize_with_version(&mut cursor)
                    .context("Failed to deserialize StoreInfo")?;
                store_info
            } else {
                // The store_info will be absent for a newly created and empty object store.
                StoreInfo::default()
            };

            // Merge the replay information.
            let mut store_info = self.store_info.lock().unwrap();

            // The frontmost element of the replay information is the most recent so we must apply
            // that last.
            for replay_info in store_info.replay_info_mut().unwrap().iter_mut().rev() {
                if replay_info.last_object_id > info.last_object_id {
                    info.last_object_id = replay_info.last_object_id;
                }
                if replay_info.object_count_delta < 0 {
                    info.object_count =
                        info.object_count.saturating_sub(-replay_info.object_count_delta as u64);
                } else {
                    info.object_count =
                        info.object_count.saturating_add(replay_info.object_count_delta as u64);
                }
                if let Some(oid) = replay_info.root_directory {
                    info.root_directory_object_id = oid;
                }
                if let Some(oid) = replay_info.graveyard_directory {
                    info.graveyard_directory_object_id = oid;
                }
                encrypted_mutations.extend(std::mem::take(&mut replay_info.encrypted_mutations));
            }

            let result = (
                info.object_tree_layers.clone(),
                info.extent_tree_layers.clone(),
                if info.mutations_key.is_some() {
                    Some(info.encrypted_mutations_object_id)
                } else {
                    None
                },
            );
            *store_info = StoreOrReplayInfo::Info(info);
            result
        };

        // TODO(csuter): the layer size here could be bad and cause overflow.
        let extent_layers = self.open_layers(extent_tree_layer_object_ids, None).await?;

        let mut total_size = extent_layers.iter().map(|h| h.get_size()).sum();

        self.extent_tree.append_layers(extent_layers.into()).await?;

        // If the store is encrypted, we can't open the object tree layers now, but we need to
        // compute the size of the layers.
        if let Some(encrypted_mutations_object_id) = encrypted {
            let parent_store = self.parent_store.as_ref().unwrap();
            for oid in object_tree_layer_object_ids.into_iter() {
                total_size += parent_store.get_file_size(oid).await?;
            }
            if encrypted_mutations_object_id != INVALID_OBJECT_ID {
                total_size += layer_size_from_encrypted_mutations_size(
                    parent_store.get_file_size(encrypted_mutations_object_id).await?,
                );
            }
        } else {
            let object_layers = self.open_layers(object_tree_layer_object_ids, None).await?;
            total_size += object_layers.iter().map(|h| h.get_size()).sum::<u64>();
            self.tree.append_layers(object_layers.into()).await?;
            *self.lock_state.lock().unwrap() = LockState::Unencrypted;
        }

        let _ = self.store_info_handle.set(handle);
        self.filesystem().object_manager().update_reservation(
            self.store_object_id,
            tree::reservation_amount_from_layer_size(total_size),
        );

        if !encrypted_mutations.is_empty() {
            *self.encrypted_mutations.lock().unwrap() = Some(Arc::new(encrypted_mutations.into()));
        }

        Ok(())
    }

    async fn open_layers(
        &self,
        object_ids: impl std::iter::IntoIterator<Item = u64>,
        crypt: Option<&dyn Crypt>,
    ) -> Result<Vec<CachingObjectHandle<ObjectStore>>, Error> {
        let parent_store = self.parent_store.as_ref().unwrap();
        let mut handles = Vec::new();
        for object_id in object_ids {
            let handle = CachingObjectHandle::new(
                ObjectStore::open_object(&parent_store, object_id, HandleOptions::default(), crypt)
                    .await
                    .context(format!("Failed to open layerr file {}", object_id))?,
            );
            handles.push(handle);
        }
        Ok(handles)
    }

    /// Unlocks a store so that it is ready to be used.
    /// This is not thread-safe.
    pub async fn unlock(&self, crypt: Arc<dyn Crypt>) -> Result<(), Error> {
        let store_info = self.store_info();

        // The store should be locked.
        assert!(self.is_locked());

        self.tree
            .append_layers(
                self.open_layers(
                    store_info.object_tree_layers.iter().cloned(),
                    Some(crypt.as_ref()),
                )
                .await?
                .into(),
            )
            .await?;

        let unwrapped_keys = crypt
            .unwrap_keys(store_info.mutations_key.as_ref().unwrap(), self.store_object_id)
            .await?;
        let mut mutations_cipher =
            StreamCipher::new(&unwrapped_keys[0], store_info.mutations_cipher_offset);

        // Apply the encrypted mutations.
        let mut mutations = {
            if store_info.encrypted_mutations_object_id == INVALID_OBJECT_ID {
                EncryptedMutations::default()
            } else {
                let parent_store = self.parent_store.as_ref().unwrap();
                let handle = ObjectStore::open_object(
                    &parent_store,
                    store_info.encrypted_mutations_object_id,
                    HandleOptions::default(),
                    None,
                )
                .await?;
                let mut cursor = std::io::Cursor::new(
                    handle
                        .contents(MAX_ENCRYPTED_MUTATIONS_SIZE)
                        .await
                        .context(FxfsError::Inconsistent)?,
                );
                let mut mutations = EncryptedMutations::deserialize_with_version(&mut cursor)?.0;
                let len = cursor.get_ref().len() as u64;
                while cursor.position() < len {
                    mutations.extend(EncryptedMutations::deserialize_with_version(&mut cursor)?.0);
                }
                mutations
            }
        };

        if let Some(m) = &*self.encrypted_mutations.lock().unwrap() {
            mutations.extend_from(m);
        }

        let EncryptedMutations { transactions, mut data } = mutations;
        mutations_cipher.decrypt(&mut data);
        let mut cursor = std::io::Cursor::new(data);
        for (checkpoint, count) in transactions {
            let context = ApplyContext { mode: ApplyMode::Replay, checkpoint };
            for _ in 0..count {
                self.apply_mutation(
                    Mutation::deserialize_from_version(&mut cursor, context.checkpoint.version)?,
                    &context,
                    AssocObj::None,
                )
                .await;
            }
        }

        *self.mutations_cipher.lock().unwrap() = Some(mutations_cipher);
        *self.lock_state.lock().unwrap() = LockState::Unlocked(crypt);

        // TODO(fxbug.dev/92275): we should force a flush here since otherwise it is possible to end
        // up unbounded memory usage: unlock -> make changes with no intervening flush -> lock ->
        // flush.
        Ok(())
    }

    pub fn is_locked(&self) -> bool {
        matches!(*self.lock_state.lock().unwrap(), LockState::Locked)
    }

    pub fn get_next_object_id(&self) -> u64 {
        let mut store_info = self.store_info.lock().unwrap();
        let last_object_id = store_info.last_object_id();
        *last_object_id += 1;
        *last_object_id
    }

    pub fn allocator(&self) -> Arc<dyn Allocator> {
        self.filesystem().allocator()
    }

    // If |transaction| has an impending mutation for the underlying object, returns that.
    // Otherwise, looks up the object from the tree and returns a suitable mutation for it.  The
    // mutation is returned here rather than the item because the mutation includes the operation
    // which has significance: inserting an object implies it's the first of its kind unlike
    // replacing an object.
    async fn txn_get_object_mutation(
        &self,
        transaction: &Transaction<'_>,
        object_id: u64,
    ) -> Result<ObjectStoreMutation, Error> {
        if let Some(mutation) =
            transaction.get_object_mutation(self.store_object_id, ObjectKey::object(object_id))
        {
            Ok(mutation.clone())
        } else {
            Ok(ObjectStoreMutation {
                item: self
                    .tree
                    .find(&ObjectKey::object(object_id))
                    .await?
                    .ok_or(anyhow!(FxfsError::NotFound))?,
                op: Operation::ReplaceOrInsert,
            })
        }
    }

    fn update_last_object_id(&self, object_id: u64) {
        let mut store_info = self.store_info.lock().unwrap();
        let last_object_id = store_info.last_object_id();
        if object_id > *last_object_id {
            *last_object_id = object_id;
        }
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
            let len = if let Some(l) = range.length().ok().and_then(|l| usize::try_from(l).ok()) {
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
                *device_offset..*device_offset + range.length().unwrap(),
                checksums,
            );
        }
        Ok(true)
    }

    /// Adds the specified object to the graveyard.
    pub fn add_to_graveyard(&self, transaction: &mut Transaction<'_>, object_id: u64) {
        let graveyard_id = self.graveyard_directory_object_id();
        assert_ne!(graveyard_id, INVALID_OBJECT_ID);
        transaction.add(
            self.store_object_id,
            Mutation::replace_or_insert_object(
                ObjectKey::graveyard_entry(graveyard_id, object_id),
                ObjectValue::Some,
            ),
        );
    }

    /// Removes the specified object from the graveyard.
    pub fn remove_from_graveyard(&self, transaction: &mut Transaction<'_>, object_id: u64) {
        transaction.add(
            self.store_object_id,
            Mutation::replace_or_insert_object(
                ObjectKey::graveyard_entry(self.graveyard_directory_object_id(), object_id),
                ObjectValue::None,
            ),
        );
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
        context: &ApplyContext<'_, '_>,
        _assoc_obj: AssocObj<'_>,
    ) {
        // It's not safe to fully open a store until replay is fully complete (because subsequent
        // mutations could render current records invalid).

        match mutation {
            Mutation::ObjectStore(ObjectStoreMutation { mut item, op }) => {
                item.sequence = context.checkpoint.file_offset;
                self.update_last_object_id(item.key.object_id);
                match op {
                    Operation::Insert => {
                        // If we are inserting an object record for the first time, it signifies the
                        // birth of the object so we need to adjust the object count.
                        if matches!(item.value, ObjectValue::Object { .. }) {
                            self.store_info.lock().unwrap().adjust_object_count(1);
                        }
                        self.tree.insert(item).await;
                    }
                    Operation::ReplaceOrInsert => self.tree.replace_or_insert(item).await,
                    Operation::Merge => {
                        if item.is_tombstone() {
                            self.store_info.lock().unwrap().adjust_object_count(-1);
                        }
                        let lower_bound = item.key.key_for_merge_into();
                        self.tree.merge_into(item, &lower_bound).await;
                    }
                }
            }
            Mutation::ObjectStoreInfo(info) => match info {
                StoreInfoMutation::RootDirectory(NoOrd(oid)) => {
                    self.store_info.lock().unwrap().set_root_directory(oid)
                }
                StoreInfoMutation::GraveyardDirectory(NoOrd(oid)) => {
                    self.store_info.lock().unwrap().set_graveyard_directory(oid)
                }
            },
            Mutation::BeginFlush => {
                self.tree.seal().await;
                self.extent_tree.seal().await;
                self.store_info.lock().unwrap().begin_flush();
            }
            Mutation::EndFlush => {
                if context.mode.is_replay() {
                    self.tree.reset_immutable_layers();
                    self.extent_tree.reset_immutable_layers();
                    self.store_info.lock().unwrap().end_flush();
                }
            }
            Mutation::Extent(ExtentMutation(key, value)) => {
                let item = Item::new_with_sequence(key, value, context.checkpoint.file_offset);
                let lower_bound = item.key.key_for_merge_into();
                self.extent_tree.merge_into(item, &lower_bound).await;
            }
            Mutation::EncryptedObjectStore(data) => {
                self.store_info.lock().unwrap().push_encrypted_mutation(&context.checkpoint, data);
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
        trace_duration!("ObjectStore::flush", "store_object_id" => self.store_object_id);
        if self.parent_store.is_none() {
            return Ok(());
        }
        let filesystem = self.filesystem();
        let object_manager = filesystem.object_manager();

        // TODO(fxbug.dev/92275): This should also flush if there are encrypted mutations that can
        // be flushed in their unencrypted form.
        if !object_manager.needs_flush(self.store_object_id) {
            return Ok(());
        }

        let trace = self.trace.load(Ordering::Relaxed);
        if trace {
            log::info!("OS {} begin flush", self.store_object_id());
        }

        let keys = [LockKey::flush(self.store_object_id())];
        let _guard = debug_assert_not_too_long!(filesystem.write_lock(&keys));

        let parent_store = self.parent_store.as_ref().unwrap();

        let reservation = object_manager.metadata_reservation();
        let txn_options = Options {
            skip_journal_checks: true,
            borrow_metadata_space: true,
            allocator_reservation: Some(reservation),
            ..Default::default()
        };

        struct StoreInfoSnapshot<'a> {
            store: &'a ObjectStore,
            store_info: OnceCell<StoreInfo>,
        }
        impl AssociatedObject for StoreInfoSnapshot<'_> {
            fn will_apply_mutation(
                &self,
                _mutation: &Mutation,
                _object_id: u64,
                _manager: &ObjectManager,
            ) {
                let mut store_info = self.store.store_info();

                // Capture the offset in the cipher stream.
                let mutations_cipher = self.store.mutations_cipher.lock().unwrap();
                if let Some(cipher) = mutations_cipher.as_ref() {
                    store_info.mutations_cipher_offset = cipher.offset();
                }

                self.store_info.set(store_info).unwrap();
            }
        }
        let store_info_snapshot = StoreInfoSnapshot { store: self, store_info: OnceCell::new() };

        // The BeginFlush mutation must be within a transaction that has no impact on StoreInfo
        // since we want to get an accurate snapshot of StoreInfo.
        let mut transaction = filesystem.clone().new_transaction(&[], txn_options).await?;
        transaction.add_with_object(
            self.store_object_id(),
            Mutation::BeginFlush,
            AssocObj::Borrowed(&store_info_snapshot),
        );
        transaction.commit().await?;

        // There is a transaction to create objects at the start and then another transaction at the
        // end. Between those two transactions, there are transactions that write to the files.  In
        // the first transaction, objects are created in the graveyard. Upon success, the objects
        // are removed from the graveyard.
        let mut transaction = filesystem.clone().new_transaction(&[], txn_options).await?;

        let reservation_update: ReservationUpdate; // Must live longer than end_transaction.
        let mut end_transaction = filesystem.clone().new_transaction(&[], txn_options).await?;

        let new_extent_tree_layer = ObjectStore::create_object(
            parent_store,
            &mut transaction,
            HandleOptions { skip_journal_checks: true, ..Default::default() },
            None,
        )
        .await?;

        let new_extent_tree_layer_object_id = new_extent_tree_layer.object_id();
        parent_store.add_to_graveyard(&mut transaction, new_extent_tree_layer_object_id);
        parent_store.remove_from_graveyard(&mut end_transaction, new_extent_tree_layer_object_id);

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
        let mut new_store_info = store_info_snapshot.store_info.into_inner().unwrap();
        let mut total_layer_size = 0;

        let mut old_encrypted_mutations_object_id = INVALID_OBJECT_ID;

        let (old_object_tree_layers, new_object_tree_layers) = if self.is_locked() {
            // The store is locked so we need to either write our encrypted mutations to a new file,
            // or append them to an existing one.
            let handle = if new_store_info.encrypted_mutations_object_id == INVALID_OBJECT_ID {
                let handle = ObjectStore::create_object(
                    parent_store,
                    &mut transaction,
                    HandleOptions { skip_journal_checks: true, ..Default::default() },
                    None,
                )
                .await?;
                let oid = handle.object_id();
                new_store_info.encrypted_mutations_object_id = oid;
                parent_store.add_to_graveyard(&mut transaction, oid);
                parent_store.remove_from_graveyard(&mut end_transaction, oid);
                handle
            } else {
                ObjectStore::open_object(
                    parent_store,
                    new_store_info.encrypted_mutations_object_id,
                    HandleOptions { skip_journal_checks: true, ..Default::default() },
                    None,
                )
                .await?
            };
            transaction.commit().await?;

            // Append the encrypted mutations.
            let mut buffer = handle.allocate_buffer(MAX_ENCRYPTED_MUTATIONS_SIZE);
            let mut cursor = std::io::Cursor::new(buffer.as_mut_slice());
            self.encrypted_mutations
                .lock()
                .unwrap()
                .as_ref()
                .unwrap()
                .serialize_with_version(&mut cursor)?;
            let len = cursor.position() as usize;
            handle.write_or_append(None, buffer.subslice(..len)).await?;

            total_layer_size += layer_size_from_encrypted_mutations_size(handle.get_size())
                + self
                    .tree
                    .immutable_layer_set()
                    .layers
                    .iter()
                    .map(|l| l.handle().map(ObjectHandle::get_size).unwrap_or(0))
                    .sum::<u64>();

            // There are no changes to the layers in this case.
            (Vec::new(), None)
        } else {
            // Create and write a new layer, compacting existing layers.
            let new_object_tree_layer = ObjectStore::create_object(
                parent_store,
                &mut transaction,
                HandleOptions { skip_journal_checks: true, ..Default::default() },
                self.crypt().as_deref(),
            )
            .await?;
            let writer = DirectWriter::new(&new_object_tree_layer, txn_options);
            let new_object_tree_layer_object_id = new_object_tree_layer.object_id();
            parent_store.add_to_graveyard(&mut transaction, new_object_tree_layer_object_id);
            parent_store
                .remove_from_graveyard(&mut end_transaction, new_object_tree_layer_object_id);

            transaction.commit().await?;
            let (object_tree_layers_to_keep, old_object_tree_layers) =
                tree::flush(&self.tree, writer).await?;

            let mut new_object_tree_layers =
                layers_from_handles(Box::new([CachingObjectHandle::new(new_object_tree_layer)]))
                    .await?;
            new_object_tree_layers.extend(object_tree_layers_to_keep.iter().map(|l| (*l).clone()));

            new_store_info.object_tree_layers = Vec::new();
            for layer in &new_object_tree_layers {
                if let Some(handle) = layer.handle() {
                    new_store_info.object_tree_layers.push(handle.object_id());
                }
            }

            // Move the existing layers we're compacting to the graveyard at the end.
            for layer in &old_object_tree_layers {
                if let Some(handle) = layer.handle() {
                    parent_store.add_to_graveyard(&mut end_transaction, handle.object_id());
                }
            }

            let object_tree_handles = new_object_tree_layers.iter().map(|l| l.handle());
            total_layer_size += object_tree_handles
                .map(|h| h.map(ObjectHandle::get_size).unwrap_or(0))
                .sum::<u64>();

            old_encrypted_mutations_object_id = std::mem::replace(
                &mut new_store_info.encrypted_mutations_object_id,
                INVALID_OBJECT_ID,
            );
            if old_encrypted_mutations_object_id != INVALID_OBJECT_ID {
                parent_store
                    .add_to_graveyard(&mut end_transaction, old_encrypted_mutations_object_id);
            }

            (old_object_tree_layers, Some(new_object_tree_layers))
        };

        let (extent_tree_layers_to_keep, old_extent_tree_layers) =
            tree::flush(&self.extent_tree, DirectWriter::new(&new_extent_tree_layer, txn_options))
                .await?;

        let mut new_extent_tree_layers =
            layers_from_handles(Box::new([CachingObjectHandle::new(new_extent_tree_layer)]))
                .await?;
        new_extent_tree_layers.extend(extent_tree_layers_to_keep.iter().map(|l| (*l).clone()));

        for layer in &old_extent_tree_layers {
            if let Some(handle) = layer.handle() {
                parent_store.add_to_graveyard(&mut end_transaction, handle.object_id());
            }
        }

        new_store_info.extent_tree_layers = Vec::new();
        for layer in &new_extent_tree_layers {
            if let Some(handle) = layer.handle() {
                new_store_info.extent_tree_layers.push(handle.object_id());
            }
        }

        let mut serialized_info = Vec::new();
        new_store_info.serialize_with_version(&mut serialized_info)?;
        let mut buf = self.device.allocate_buffer(serialized_info.len());
        buf.as_mut_slice().copy_from_slice(&serialized_info[..]);

        self.store_info_handle
            .get()
            .unwrap()
            .txn_write(&mut end_transaction, 0u64, buf.as_ref())
            .await?;

        let extent_tree_handles = new_extent_tree_layers.iter().map(|l| l.handle());

        total_layer_size +=
            extent_tree_handles.map(|h| h.map(ObjectHandle::get_size).unwrap_or(0)).sum::<u64>();

        reservation_update =
            ReservationUpdate::new(tree::reservation_amount_from_layer_size(total_layer_size));

        end_transaction.add_with_object(
            self.store_object_id(),
            Mutation::EndFlush,
            AssocObj::Borrowed(&reservation_update),
        );

        if trace {
            log::info!(
                "OS {} compacting {} obj, {} ext -> {} obj, {} ext layers (sz {})",
                self.store_object_id(),
                old_object_tree_layers.len(),
                old_extent_tree_layers.len(),
                new_object_tree_layers.as_ref().map(|v| v.len()).unwrap_or(0),
                new_extent_tree_layers.len(),
                total_layer_size
            );
        }
        end_transaction
            .commit_with_callback(|_| {
                let mut store_info = self.store_info.lock().unwrap();
                let info = store_info.info_mut().unwrap();
                info.object_tree_layers = new_store_info.object_tree_layers;
                info.extent_tree_layers = new_store_info.extent_tree_layers;
                info.encrypted_mutations_object_id = new_store_info.encrypted_mutations_object_id;
                info.mutations_cipher_offset = new_store_info.mutations_cipher_offset;

                if let Some(layers) = new_object_tree_layers {
                    self.tree.set_layers(layers);
                }
                self.extent_tree.set_layers(new_extent_tree_layers);
                self.encrypted_mutations.lock().unwrap().take();
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

        if old_encrypted_mutations_object_id != INVALID_OBJECT_ID {
            parent_store.tombstone(old_encrypted_mutations_object_id, txn_options).await?;
        }

        if trace {
            log::info!("OS {} end flush", self.store_object_id());
        }
        Ok(())
    }

    fn encrypt_mutation(&self, mutation: &Mutation) -> Option<Mutation> {
        match mutation {
            // Encrypt all object store mutations.
            Mutation::ObjectStore(_) => self.mutations_cipher.lock().unwrap().as_mut().map(|c| {
                let mut buffer = Vec::new();
                mutation.serialize_into(&mut buffer).unwrap();
                c.encrypt(&mut buffer);
                Mutation::EncryptedObjectStore(buffer.into())
            }),
            _ => None,
        }
    }
}

// TODO(csuter): MemDataBuffer has size limits so we should check sizes before we use it.
impl HandleOwner for ObjectStore {
    type Buffer = MemDataBuffer;

    fn create_data_buffer(&self, _object_id: u64, initial_size: u64) -> Self::Buffer {
        MemDataBuffer::new(initial_size)
    }
}

impl AsRef<ObjectStore> for ObjectStore {
    fn as_ref(&self) -> &ObjectStore {
        self
    }
}

fn layer_size_from_encrypted_mutations_size(size: u64) -> u64 {
    // This is similar to reserved_space_from_journal_usage. It needs to be a worst case estimate of
    // the amount of metadata space that might need to be reserved to allow the encrypted mutations
    // to be written to layer files.  It needs to be >= than reservation_amount_from_layer_size will
    // return once the data has been written to layer files and <= than
    // reserved_space_from_journal_usage would use.  We can't just use
    // reserved_space_from_journal_usage because the encrypted mutations file includes some extra
    // data (it includes the checkpoints) that isn't written in the same way to the journal.
    size * 3
}

impl AssociatedObject for ObjectStore {}
#[cfg(test)]
mod tests {
    use {
        crate::{
            crypt::{Crypt, InsecureCrypt},
            errors::FxfsError,
            lsm_tree::types::{Item, ItemRef, LayerIterator},
            object_handle::{ObjectHandle, ReadObjectHandle, WriteObjectHandle},
            object_store::{
                directory::Directory,
                extent_record::{ExtentKey, ExtentValue},
                filesystem::{Filesystem, FxFilesystem, Mutations, OpenFxFilesystem, SyncOptions},
                fsck::fsck,
                object_record::{ObjectKey, ObjectValue},
                transaction::{Options, TransactionHandler},
                volume::root_volume,
                HandleOptions, ObjectStore,
            },
        },
        assert_matches::assert_matches,
        fuchsia_async as fasync,
        futures::join,
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
        FxFilesystem::new_empty(device).await.expect("new_empty failed")
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
            ObjectStore::create_object(&store, &mut transaction, HandleOptions::default(), None)
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
            ObjectStore::create_object(&store, &mut transaction, HandleOptions::default(), None)
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
            ObjectStore::create_object(&store, &mut transaction, HandleOptions::default(), None)
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
            let root_volume = root_volume(&fs).await.expect("root_volume failed");
            root_volume
                .new_volume("test", Arc::new(InsecureCrypt::new()))
                .await
                .expect("new_volume failed")
                .store_object_id()
        };

        fs.close().await.expect("close failed");
        let device = fs.take_device().await;
        device.reopen();
        let fs = FxFilesystem::open(device).await.expect("open failed");

        fs.object_manager()
            .open_store(store_id, Arc::new(InsecureCrypt::new()))
            .await
            .expect("open_store failed");
        fs.close().await.expect("Close failed");
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
            ObjectStore::create_object(&store, &mut transaction, HandleOptions::default(), None)
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
            store.crypt().as_deref(),
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
                None,
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
                None,
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

        fsck(&fs, None).await.expect("fsck failed");
    }

    #[fasync::run(10, test)]
    async fn test_encrypted_mutations() {
        async fn one_iteration(
            fs: OpenFxFilesystem,
            crypt: Arc<dyn Crypt>,
            iteration: u64,
        ) -> OpenFxFilesystem {
            async fn reopen(fs: OpenFxFilesystem) -> OpenFxFilesystem {
                fs.close().await.expect("Close failed");
                let device = fs.take_device().await;
                device.reopen();
                FxFilesystem::open(device).await.expect("FS open failed")
            }

            let fs = reopen(fs).await;

            let object_id = {
                let root_volume = root_volume(&fs).await.expect("root_volume failed");
                let store = root_volume.volume("test", crypt.clone()).await.expect("volume failed");
                let root_directory = Directory::open(&store, store.root_directory_object_id())
                    .await
                    .expect("open failed");

                let mut transaction = fs
                    .clone()
                    .new_transaction(&[], Options::default())
                    .await
                    .expect("new_transaction failed");
                let object = root_directory
                    .create_child_file(&mut transaction, &format!("test {}", iteration))
                    .await
                    .expect("create_child_file failed");
                transaction.commit().await.expect("commit failed");

                let mut buf = object.allocate_buffer(1000);
                for i in 0..buf.len() {
                    buf.as_mut_slice()[i] = i as u8;
                }
                object.write_or_append(Some(0), buf.as_ref()).await.expect("write failed");

                object.object_id()
            };

            let fs = reopen(fs).await;

            let check_object = |fs| {
                let crypt = crypt.clone();
                async move {
                    let root_volume = root_volume(&fs).await.expect("root_volume failed");
                    let volume = root_volume.volume("test", crypt).await.expect("volume failed");

                    let object = ObjectStore::open_object(
                        &volume,
                        object_id,
                        HandleOptions::default(),
                        None,
                    )
                    .await
                    .expect("open_object failed");
                    let mut buf = object.allocate_buffer(1000);
                    assert_eq!(object.read(0, buf.as_mut()).await.expect("read failed"), 1000);
                    for i in 0..buf.len() {
                        assert_eq!(buf.as_slice()[i], i as u8);
                    }
                }
            };

            check_object(fs.clone()).await;

            let fs = reopen(fs).await;

            // Before checking the object, flush the filesystem.  This should leave a file with
            // encrypted mutations.
            fs.object_manager().flush().await.expect("flush failed");

            check_object(fs.clone()).await;

            let fs = reopen(fs).await;

            fsck(&fs, Some(crypt.clone())).await.expect("fsck failed");

            // Reopen and flush the volume once more.
            let fs = reopen(fs).await;

            {
                let root_volume = root_volume(&fs).await.expect("root_volume failed");
                let _volume =
                    root_volume.volume("test", crypt.clone()).await.expect("volume failed");

                // This should do a normal flush and remove the encrypted file.
                fs.object_manager().flush().await.expect("flush failed");
            }

            let fs = reopen(fs).await;

            check_object(fs.clone()).await;

            let fs = reopen(fs).await;

            fsck(&fs, Some(crypt.clone())).await.expect("fsck failed");

            fs
        }

        let mut fs = test_filesystem().await;
        let crypt = Arc::new(InsecureCrypt::new());

        {
            let root_volume = root_volume(&fs).await.expect("root_volume failed");
            let _store =
                root_volume.new_volume("test", crypt.clone()).await.expect("new_volume failed");
        }

        // Run a few iterations so that we test changes with the stream cipher offset.
        for i in 0..5 {
            fs = one_iteration(fs, crypt.clone(), i).await;
        }
    }
}

// TODO(csuter): validation of all deserialized structs.
// TODO(csuter): check all panic! calls.
