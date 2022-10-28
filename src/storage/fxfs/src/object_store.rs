// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod allocator;
pub mod caching_object_handle;
pub mod directory;
mod extent_record;
mod flush;
pub mod graveyard;
pub mod journal;
mod merge;
pub mod object_manager;
mod object_record;
mod store_object_handle;
#[cfg(test)]
mod testing;
pub mod transaction;
mod tree;
pub mod volume;
mod writeback_cache;

pub use caching_object_handle::CachingObjectHandle;
pub use directory::Directory;
pub use object_record::{ObjectDescriptor, Timestamp};
pub use store_object_handle::StoreObjectHandle;

use {
    crate::{
        crypt::{Crypt, KeyPurpose, StreamCipher, WrappedKey, WrappedKeys},
        data_buffer::{DataBuffer, MemDataBuffer},
        debug_assert_not_too_long,
        errors::FxfsError,
        ff1::Ff1,
        filesystem::{
            ApplyContext, ApplyMode, Filesystem, FxFilesystem, JournalingObject, MAX_FILE_SIZE,
        },
        log::*,
        lsm_tree::{
            types::{Item, ItemRef, LayerIterator},
            LSMTree,
        },
        object_handle::{ObjectHandle, ObjectHandleExt, INVALID_OBJECT_ID},
        object_store::{
            allocator::SimpleAllocator,
            graveyard::Graveyard,
            journal::JournalCheckpoint,
            transaction::{
                AssocObj, AssociatedObject, LockKey, ObjectStoreMutation, Operation, Options,
                Transaction, UpdateMutationsKey,
            },
        },
        round::round_up,
        serialized_types::{Version, Versioned, VersionedLatest},
    },
    allocator::Allocator,
    anyhow::{anyhow, bail, ensure, Context, Error},
    assert_matches::assert_matches,
    async_trait::async_trait,
    fuchsia_inspect::{ArrayProperty, LazyNode},
    futures::FutureExt,
    once_cell::sync::OnceCell,
    scopeguard::ScopeGuard,
    serde::{Deserialize, Serialize},
    std::{
        collections::VecDeque,
        fmt,
        ops::Bound,
        sync::{
            atomic::{AtomicBool, Ordering},
            Arc, Mutex, Weak,
        },
    },
    storage_device::Device,
    uuid::Uuid,
};

// Exposed for serialized_types.
pub use allocator::{AllocatorInfo, AllocatorKey, AllocatorValue};
pub use extent_record::{ExtentKey, ExtentValue, DEFAULT_DATA_ATTRIBUTE_ID};
pub use journal::{JournalRecord, SuperBlock, SuperBlockRecord};
pub use object_record::{
    AttributeKey, EncryptionKeys, ObjectAttributes, ObjectKey, ObjectKeyData, ObjectKind,
    ObjectValue,
};
pub use transaction::Mutation;

// For encrypted stores, the lower 32 bits of the object ID are encrypted to make side-channel
// attacks more difficult. This mask can be used to extract the hi part of the object ID.
const OBJECT_ID_HI_MASK: u64 = 0xffffffff00000000;

// At time of writing, this threshold limits transactions that delete extents to about 10,000 bytes.
const TRANSACTION_MUTATION_THRESHOLD: usize = 200;

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
    /// The globally unique identifier for the associated object store. If unset, will be all zero.
    guid: [u8; 16],

    /// The last used object ID.  Note that this field is not accurate in memory; ObjectStore's
    /// last_object_id field is the one to use in that case.  Technically, this might not be the
    /// last object ID used for the latest transaction that created an object because we use this at
    /// the point of creating the object but before we commit the transaction.  Transactions can
    /// then get committed in an arbitrary order (or not at all).
    last_object_id: u64,

    /// Object ids for layers.  TODO(fxbug.dev/95971): need a layer of indirection here so we can
    /// support snapshots.
    pub layers: Vec<u64>,

    /// The object ID for the root directory.
    root_directory_object_id: u64,

    /// The object ID for the graveyard.
    graveyard_directory_object_id: u64,

    /// The number of live objects in the store.
    object_count: u64,

    /// The (wrapped) key that encrypted mutations should use.
    mutations_key: Option<WrappedKey>,

    /// Mutations for the store are encrypted using a stream cipher.  To decrypt the mutations, we
    /// need to know the offset in the cipher stream to start it.
    mutations_cipher_offset: u64,

    /// If we have to flush the store whilst we do not have the key, we need to write the encrypted
    /// mutations to an object. This is the object ID of that file if it exists.
    pub encrypted_mutations_object_id: u64,

    /// Object IDs are encrypted to reduce the amount of information that sequential object IDs
    /// reveal (such as the number of files in the system and the ordering of their creation in
    /// time).  Only the bottom 32 bits of the object ID are encrypted whilst the top 32 bits will
    /// increment after 2^32 object IDs have been used and this allows us to roll the key.
    object_id_key: Option<WrappedKey>,
}

impl StoreInfo {
    /// Create a new/default [`StoreInfo`] but with a newly generated GUID.
    fn new_with_guid() -> Self {
        let guid = Uuid::new_v4();
        Self { guid: *guid.as_bytes(), ..Default::default() }
    }
}

// TODO(fxbug.dev/95972): We should test or put checks in place to ensure this limit isn't exceeded.
// It will likely involve placing limits on the maximum number of layers.
pub const MAX_STORE_INFO_SERIALIZED_SIZE: usize = 131072;

// This needs to be large enough to accommodate the maximum amount of unflushed data (data that is
// in the journal but hasn't yet been written to layer files) for a store.  We set a limit because
// we want to limit the amount of memory use in the case the filesystem is corrupt or under attaack.
pub const MAX_ENCRYPTED_MUTATIONS_SIZE: usize = 8 * journal::DEFAULT_RECLAIM_SIZE as usize;

#[derive(Default)]
pub struct HandleOptions {
    /// If true, transactions used by this handle will skip journal space checks.
    skip_journal_checks: bool,
}

#[derive(Default)]
pub struct NewChildStoreOptions {
    /// The store is unencrypted if store is none.
    pub crypt: Option<Arc<dyn Crypt>>,

    /// Specifies the object ID in the root store to be used for the store.  If set to
    /// INVALID_OBJECT_ID (the default and typical case), a suitable ID will be chosen.
    pub object_id: u64,
}

#[derive(Debug, Default, Deserialize, Serialize)]
pub struct EncryptedMutations {
    // Information about the mutations are held here, but the actual encrypted data is held within
    // data.  For each transaction, we record the checkpoint and the count of mutations within the
    // transaction.  The checkpoint is required for the log file offset (which we need to apply the
    // mutations), and the version so that we can correctly decode the mutation after it has been
    // decrypted. The count specifies the number of serialized mutations encoded in |data|.
    transactions: Vec<(JournalCheckpoint, u64)>,

    // The encrypted mutations.
    data: Vec<u8>,

    // If the mutations key was rolled, this holds the offset in `data` where the new key should
    // apply.
    mutations_key_roll: Vec<(usize, WrappedKey)>,
}

impl Versioned for EncryptedMutations {
    fn max_serialized_size() -> u64 {
        MAX_ENCRYPTED_MUTATIONS_SIZE as u64
    }
}

impl EncryptedMutations {
    fn extend(&mut self, other: &EncryptedMutations) -> Result<(), Error> {
        self.transactions.extend_from_slice(&other.transactions[..]);
        self.mutations_key_roll.extend(
            other
                .mutations_key_roll
                .iter()
                .map(|(offset, key)| (offset + self.data.len(), key.clone())),
        );
        self.data.extend_from_slice(&other.data[..]);
        Ok(())
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
    object_count_delta: i64,
    encrypted_mutations: EncryptedMutations,
}

impl ReplayInfo {
    fn new() -> VecDeque<ReplayInfo> {
        let mut info = VecDeque::new();
        info.push_back(ReplayInfo::default());
        info
    }
}

#[derive(Debug)]
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

    fn set_mutations_key(&mut self, key: WrappedKey) {
        let mutations =
            &mut self.replay_info_mut().unwrap().front_mut().unwrap().encrypted_mutations;
        mutations.mutations_key_roll.push((mutations.data.len(), key));
    }
}

pub enum LockState {
    // Contains the encrypted mutations found in the journal during replay.
    Locked(EncryptedMutations),
    Unencrypted,
    Unlocked(Arc<dyn Crypt>),
    // The store is unlocked, but in a read-only state, and no flushes or other operations will be
    // performed on the store.  We have to retain the old store info and journaled encrypted
    // mutations (see LockState::Locked), which normally get written out during unlock when we flush
    // the object store, which we skip in the read-only mode.  This way, when the store is
    // re-locked, we don't lose these journaled encrypted mutations.
    UnlockedReadOnly(Arc<dyn Crypt>, StoreInfo, EncryptedMutations),

    // The store is encrypted but is now in an unusable state (either due to a failure to unlock, or
    // a failure to lock).
    Invalid,

    // Before we've read the StoreInfo we might not know whether the store is Locked or Unencrypted.
    // This can happen when lazily opening stores (ObjectManager::lazy_open_store).
    Unknown,

    // The store is in the process of being locked.  Whilst the store is being locked, the store
    // isn't usable and the only mutations we expect are those to do with flushing; assertions will
    // trip if this isn't the case.
    Locking(Arc<dyn Crypt>),

    // Whilst we're unlocking, we will replay encrypted mutations.  The store isn't usable until
    // it's in the Unlocked state.
    Unlocking,
}

impl LockState {
    fn encrypted_mutations(&self) -> Option<&EncryptedMutations> {
        if let LockState::Locked(m) = self {
            Some(m)
        } else if let LockState::UnlockedReadOnly(_, _, m) = self {
            Some(m)
        } else {
            None
        }
    }

    fn encrypted_mutations_mut(&mut self) -> Option<&mut EncryptedMutations> {
        if let LockState::Locked(m) = self {
            Some(m)
        } else if let LockState::UnlockedReadOnly(_, _, m) = self {
            Some(m)
        } else {
            None
        }
    }
}

impl fmt::Debug for LockState {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(match self {
            LockState::Locked(_) => "Locked",
            LockState::Unencrypted => "Unencrypted",
            LockState::Unlocked(_) => "Unlocked",
            LockState::UnlockedReadOnly(..) => "UnlockedReadOnly",
            LockState::Invalid => "Invalid",
            LockState::Unknown => "Unknown",
            LockState::Locking(_) => "Locking",
            LockState::Unlocking => "Unlocking",
        })
    }
}

#[derive(Default)]
struct LastObjectId {
    // The *unencrypted* value of the last object ID.
    id: u64,

    // Encrypted stores will use a cipher to obfuscate the object ID.
    cipher: Option<Ff1>,
}

impl LastObjectId {
    // Returns true if a cipher is needed to generate new object IDs.
    fn should_create_cipher(&self) -> bool {
        self.id as u32 == u32::MAX
    }

    fn get_next_object_id(&mut self) -> u64 {
        self.id += 1;
        if let Some(cipher) = &self.cipher {
            let hi = self.id & OBJECT_ID_HI_MASK;
            assert_ne!(hi, INVALID_OBJECT_ID);
            assert_ne!(self.id as u32, 0); // This would indicate the ID wrapped.
            hi | cipher.encrypt(self.id as u32) as u64
        } else {
            self.id
        }
    }
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
    // Lock ordering: This must be taken before `lock_state`.
    store_info: Mutex<StoreOrReplayInfo>,
    tree: LSMTree<ObjectKey, ObjectValue>,

    // When replaying the journal, the store cannot read StoreInfo until the whole journal
    // has been replayed, so during that time, store_info_handle will be None and records
    // just get sent to the tree. Once the journal has been replayed, we can open the store
    // and load all the other layer information.
    store_info_handle: OnceCell<StoreObjectHandle<ObjectStore>>,

    // The cipher to use for encrypted mutations, if this store is encrypted.
    mutations_cipher: Mutex<Option<StreamCipher>>,

    // Current lock state of the store.
    // Lock ordering: This must be taken after `store_info`.
    lock_state: Mutex<LockState>,

    // Enable/disable tracing.
    trace: AtomicBool,

    // Informational counters for events occurring within the store.
    counters: Mutex<ObjectStoreCounters>,

    // While the object store is being tracked, the node is retained here.  See
    // `Self::track_statistics`.
    tracking: Mutex<Option<LazyNode>>,

    // Contains the last object ID and, optionally, a cipher to be used when generating new object
    // IDs.
    last_object_id: Mutex<LastObjectId>,
}

#[derive(Clone, Default)]
struct ObjectStoreCounters {
    mutations_applied: u64,
    mutations_dropped: u64,
    num_flushes: u64,
    last_flush_time: Option<std::time::SystemTime>,
    persistent_layer_file_sizes: Vec<u64>,
}

impl ObjectStore {
    fn new(
        parent_store: Option<Arc<ObjectStore>>,
        store_object_id: u64,
        filesystem: Arc<dyn Filesystem>,
        store_info: Option<StoreInfo>,
        mutations_cipher: Option<StreamCipher>,
        lock_state: LockState,
        last_object_id: LastObjectId,
    ) -> Arc<ObjectStore> {
        let device = filesystem.device();
        let block_size = filesystem.block_size();
        Arc::new(ObjectStore {
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
            store_info_handle: OnceCell::new(),
            mutations_cipher: Mutex::new(mutations_cipher),
            lock_state: Mutex::new(lock_state),
            trace: AtomicBool::new(false),
            counters: Mutex::new(ObjectStoreCounters::default()),
            tracking: Mutex::new(None),
            last_object_id: Mutex::new(last_object_id),
        })
    }

    fn new_empty(
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
            LastObjectId::default(),
        )
    }

    /// Cycle breaker constructor that returns an ObjectStore without a filesystem.
    /// This should only be used from SuperBlock code.
    pub fn new_root_parent(device: Arc<dyn Device>, block_size: u64, store_object_id: u64) -> Self {
        ObjectStore {
            parent_store: None,
            store_object_id,
            device,
            block_size,
            filesystem: Weak::<FxFilesystem>::new(),
            store_info: Mutex::new(StoreOrReplayInfo::Info(StoreInfo::default())),
            tree: LSMTree::new(merge::merge),
            store_info_handle: OnceCell::new(),
            mutations_cipher: Mutex::new(None),
            lock_state: Mutex::new(LockState::Unencrypted),
            trace: AtomicBool::new(false),
            counters: Mutex::new(ObjectStoreCounters::default()),
            tracking: Mutex::new(None),
            last_object_id: Mutex::new(LastObjectId::default()),
        }
    }

    /// Used to set filesystem on root_parent stores at bootstrap time after the filesystem has
    /// been created.
    pub fn attach_filesystem(
        mut this: ObjectStore,
        filesystem: Arc<dyn Filesystem>,
    ) -> ObjectStore {
        this.filesystem = Arc::downgrade(&filesystem);
        this
    }

    /// Create a child store. It is a multi-step process:
    ///
    ///   1. Call `ObjectStore::new_child_store`.
    ///   2. Register the store with the object-manager.
    ///   3. Call `ObjectStore::create` to write the store-info.
    ///
    /// If the procedure fails, care must be taken to unregister store with the object-manager.
    ///
    /// The steps have to be separate because of lifetime issues when working with a transaction.
    async fn new_child_store(
        self: &Arc<Self>,
        transaction: &mut Transaction<'_>,
        options: NewChildStoreOptions,
    ) -> Result<Arc<Self>, Error> {
        let handle = if options.object_id != INVALID_OBJECT_ID {
            ObjectStore::create_object_with_id(
                self,
                transaction,
                options.object_id,
                HandleOptions::default(),
                None,
            )
            .await?
        } else {
            ObjectStore::create_object(self, transaction, HandleOptions::default(), None).await?
        };
        let filesystem = self.filesystem();
        let store = if let Some(crypt) = options.crypt {
            let (wrapped_key, unwrapped_key) =
                crypt.create_key(handle.object_id(), KeyPurpose::Metadata).await?;
            let (object_id_wrapped, object_id_unwrapped) =
                crypt.create_key(handle.object_id(), KeyPurpose::Metadata).await?;
            Self::new(
                Some(self.clone()),
                handle.object_id(),
                filesystem.clone(),
                Some(StoreInfo {
                    mutations_key: Some(wrapped_key),
                    object_id_key: Some(object_id_wrapped),
                    ..StoreInfo::new_with_guid()
                }),
                Some(StreamCipher::new(&unwrapped_key, 0)),
                LockState::Unlocked(crypt),
                LastObjectId {
                    // We need to avoid accidentally getting INVALID_OBJECT_ID, so we set
                    // the top 32 bits to a non-zero value.
                    id: 1 << 32,
                    cipher: Some(Ff1::new(&object_id_unwrapped)),
                },
            )
        } else {
            Self::new(
                Some(self.clone()),
                handle.object_id(),
                filesystem.clone(),
                Some(StoreInfo::new_with_guid()),
                None,
                LockState::Unencrypted,
                LastObjectId::default(),
            )
        };
        assert!(store.store_info_handle.set(handle).is_ok());
        Ok(store)
    }

    /// Actually creates the store in a transaction.  This will also create a root directory and
    /// graveyard directory for the store.  See `new_child_store` above.
    async fn create<'a>(
        self: &'a Arc<Self>,
        transaction: &mut Transaction<'a>,
    ) -> Result<(), Error> {
        let buf = {
            // Create a root directory and graveyard directory.
            let graveyard_directory_object_id = Graveyard::create(transaction, &self);
            let root_directory = Directory::create(transaction, &self).await?;

            let mut store_info = self.store_info.lock().unwrap();
            let mut store_info = store_info.info_mut().unwrap();

            store_info.graveyard_directory_object_id = graveyard_directory_object_id;
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
            info!(store_id = self.store_object_id(), trace, "OS: trace",);
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

    /// Creates a lazy inspect node named `str` under `parent` which will yield statistics for the
    /// object store when queried.
    pub fn track_statistics(self: &Arc<Self>, parent: &fuchsia_inspect::Node, name: &str) {
        let this = Arc::downgrade(self);
        *self.tracking.lock().unwrap() = Some(parent.create_lazy_child(name, move || {
            let this_clone = this.clone();
            async move {
                let inspector = fuchsia_inspect::Inspector::new();
                if let Some(this) = this_clone.upgrade() {
                    let counters = this.counters.lock().unwrap();
                    let root = inspector.root();
                    root.record_string(
                        "guid",
                        Uuid::from_bytes(this.store_info().guid).to_string(),
                    );
                    root.record_uint("store_object_id", this.store_object_id);
                    root.record_uint("mutations_applied", counters.mutations_applied);
                    root.record_uint("mutations_dropped", counters.mutations_dropped);
                    root.record_uint("num_flushes", counters.num_flushes);
                    if let Some(last_flush_time) = counters.last_flush_time.as_ref() {
                        root.record_uint(
                            "last_flush_time_ms",
                            last_flush_time
                                .duration_since(std::time::UNIX_EPOCH)
                                .unwrap_or(std::time::Duration::ZERO)
                                .as_millis()
                                .try_into()
                                .unwrap_or(0u64),
                        );
                    }
                    let sizes = root.create_uint_array(
                        "persistent_layer_file_sizes",
                        counters.persistent_layer_file_sizes.len(),
                    );
                    for i in 0..counters.persistent_layer_file_sizes.len() {
                        sizes.set(i, counters.persistent_layer_file_sizes[i]);
                    }
                    root.record(sizes);
                }
                Ok(inspector)
            }
            .boxed()
        }));
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

    pub fn root_directory_object_id(&self) -> u64 {
        self.store_info.lock().unwrap().info().unwrap().root_directory_object_id
    }

    pub fn graveyard_directory_object_id(&self) -> u64 {
        self.store_info.lock().unwrap().info().unwrap().graveyard_directory_object_id
    }

    fn set_graveyard_directory_object_id(&self, oid: u64) {
        assert_eq!(
            std::mem::replace(
                &mut self
                    .store_info
                    .lock()
                    .unwrap()
                    .info_mut()
                    .unwrap()
                    .graveyard_directory_object_id,
                oid
            ),
            INVALID_OBJECT_ID
        );
    }

    pub fn object_count(&self) -> u64 {
        self.store_info.lock().unwrap().info().unwrap().object_count
    }

    /// Returns the crypt object for the store. Returns None if the store is unencrypted. This will
    /// panic if the store is locked.
    pub fn crypt(&self) -> Option<Arc<dyn Crypt>> {
        match &*self.lock_state.lock().unwrap() {
            LockState::Locked(_) => panic!("Store is locked"),
            LockState::Invalid | LockState::Unencrypted | LockState::Unlocking => None,
            LockState::Unlocked(crypt) => Some(crypt.clone()),
            LockState::UnlockedReadOnly(crypt, _, _) => Some(crypt.clone()),
            LockState::Locking(crypt) => Some(crypt.clone()),
            LockState::Unknown => {
                panic!("Store is of unknown lock state; has the journal been replayed yet?")
            }
        }
    }

    /// Returns the file size for the object without opening the object.
    async fn get_file_size(&self, object_id: u64) -> Result<u64, Error> {
        let item = self
            .tree
            .find(&ObjectKey::attribute(object_id, DEFAULT_DATA_ATTRIBUTE_ID, AttributeKey::Size))
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
        let store_crypt = store.crypt();
        if crypt.is_none() {
            crypt = store_crypt.as_deref();
        }
        let keys = if let Some(crypt) = crypt {
            match store.tree.find(&ObjectKey::keys(object_id)).await?.ok_or(FxfsError::NotFound)? {
                Item { value: ObjectValue::Keys(EncryptionKeys::AES256XTS(keys)), .. } => Some(
                    crypt.unwrap_keys(&keys, object_id).await.context("Failed to unwrap keys")?,
                ),
                _ => {
                    bail!(anyhow!(FxfsError::Inconsistent).context("open_object: Expected keys"))
                }
            }
        } else {
            None
        };

        let item = store
            .tree
            .find(&ObjectKey::attribute(object_id, DEFAULT_DATA_ATTRIBUTE_ID, AttributeKey::Size))
            .await?
            .ok_or(FxfsError::NotFound)?;
        if let ObjectValue::Attribute { size } = item.value {
            ensure!(size <= MAX_FILE_SIZE, FxfsError::Inconsistent);
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

    // See the comment on create_object for the semantics of the `crypt` argument.  If object_id ==
    // INVALID_OBJECT_ID (which should usually be the case), an object ID will be chosen.
    async fn create_object_with_id<S: HandleOwner>(
        owner: &Arc<S>,
        transaction: &mut Transaction<'_>,
        mut object_id: u64,
        options: HandleOptions,
        mut crypt: Option<&dyn Crypt>,
    ) -> Result<StoreObjectHandle<S>, Error> {
        let store = owner.as_ref().as_ref();
        if object_id == INVALID_OBJECT_ID {
            object_id = store.get_next_object_id().await?;
        } else {
            store.update_last_object_id(object_id);
        }
        let store_crypt;
        if crypt.is_none() {
            store_crypt = store.crypt();
            crypt = store_crypt.as_deref();
        }
        let now = Timestamp::now();
        transaction.add(
            store.store_object_id(),
            Mutation::insert_object(
                ObjectKey::object(object_id),
                ObjectValue::file(1, 0, now.clone(), now),
            ),
        );
        let unwrapped_keys = if let Some(crypt) = crypt {
            let (key, unwrapped_key) = crypt.create_key(object_id, KeyPurpose::Data).await?;
            transaction.add(
                store.store_object_id(),
                Mutation::insert_object(
                    ObjectKey::keys(object_id),
                    ObjectValue::keys(EncryptionKeys::AES256XTS(WrappedKeys(vec![(0, key)]))),
                ),
            );
            Some(vec![(0, unwrapped_key)])
        } else {
            None
        };
        transaction.add(
            store.store_object_id(),
            Mutation::insert_object(
                ObjectKey::attribute(object_id, DEFAULT_DATA_ATTRIBUTE_ID, AttributeKey::Size),
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
        ObjectStore::create_object_with_id(
            owner,
            &mut transaction,
            INVALID_OBJECT_ID,
            options,
            crypt,
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

    // Purges an object that is in the graveyard.
    pub async fn tombstone(&self, object_id: u64, txn_options: Options<'_>) -> Result<(), Error> {
        self.trim_or_tombstone(object_id, true, txn_options).await
    }

    /// Trim extents beyond the end of a file for all attributes.  This will remove the entry from
    /// the graveyard when done.
    pub async fn trim(&self, object_id: u64) -> Result<(), Error> {
        // For the root and root parent store, we would need to use the metadata reservation which
        // we don't currently support, so assert that we're not those stores.
        assert!(self.parent_store.as_ref().unwrap().parent_store.is_some());

        self.trim_or_tombstone(
            object_id,
            false,
            Options { borrow_metadata_space: true, ..Default::default() },
        )
        .await
    }

    async fn trim_or_tombstone(
        &self,
        object_id: u64,
        for_tombstone: bool,
        txn_options: Options<'_>,
    ) -> Result<(), Error> {
        let fs = self.filesystem();
        let mut next_attribute = Some(0);
        while let Some(attribute_id) = next_attribute.take() {
            let mut transaction = fs
                .clone()
                .new_transaction(
                    &[LockKey::object_attribute(self.store_object_id, object_id, attribute_id)],
                    txn_options,
                )
                .await?;

            match self
                .trim_some(
                    &mut transaction,
                    object_id,
                    attribute_id,
                    if for_tombstone { TrimMode::Tombstone } else { TrimMode::UseSize },
                )
                .await?
            {
                TrimResult::Incomplete => next_attribute = Some(attribute_id),
                TrimResult::Done(None) => {
                    if for_tombstone
                        || matches!(
                            self.tree
                                .find(&ObjectKey::graveyard_entry(
                                    self.graveyard_directory_object_id(),
                                    object_id,
                                ))
                                .await?,
                            Some(Item { value: ObjectValue::Trim, .. })
                        )
                    {
                        self.remove_from_graveyard(&mut transaction, object_id);
                    }
                }
                TrimResult::Done(id) => next_attribute = id,
            }

            if !transaction.mutations.is_empty() {
                transaction.commit().await?;
            }
        }
        Ok(())
    }

    /// Deletes extents for attribute `attribute_id` in object `object_id`.  Also see the comments
    /// for TrimMode and TrimResult.
    pub async fn trim_some(
        &self,
        transaction: &mut Transaction<'_>,
        object_id: u64,
        attribute_id: u64,
        mode: TrimMode,
    ) -> Result<TrimResult, Error> {
        let layer_set = self.tree.layer_set();
        let mut merger = layer_set.merger();

        let aligned_offset = match mode {
            TrimMode::FromOffset(offset) => {
                round_up(offset, self.block_size).ok_or(FxfsError::Inconsistent)?
            }
            TrimMode::Tombstone => 0,
            TrimMode::UseSize => {
                let iter = merger
                    .seek(Bound::Included(&ObjectKey::attribute(
                        object_id,
                        attribute_id,
                        AttributeKey::Size,
                    )))
                    .await?;
                if let Some(item_ref) = iter.get() {
                    if item_ref.key.object_id != object_id {
                        return Ok(TrimResult::Done(None));
                    }

                    if let ItemRef {
                        key:
                            ObjectKey {
                                data:
                                    ObjectKeyData::Attribute(size_attribute_id, AttributeKey::Size),
                                ..
                            },
                        value: ObjectValue::Attribute { size },
                        ..
                    } = item_ref
                    {
                        // If we found a different attribute_id, return so we can get the
                        // right lock.
                        if *size_attribute_id != attribute_id {
                            return Ok(TrimResult::Done(Some(*size_attribute_id)));
                        }
                        round_up(*size, self.block_size).ok_or(FxfsError::Inconsistent)?
                    } else {
                        // At time of writing, we should always see a size record here, but
                        // asserting here would be brittle so just skip to the the next attribute
                        // instead.
                        return Ok(TrimResult::Done(Some(attribute_id + 1)));
                    }
                } else {
                    // End of the tree.
                    return Ok(TrimResult::Done(None));
                }
            }
        };

        // Loop over the extents and deallocate them.
        let mut iter = merger
            .seek(Bound::Included(&ObjectKey::from_extent(
                object_id,
                attribute_id,
                ExtentKey::search_key_from_offset(aligned_offset),
            )))
            .await?;
        let mut end = 0;
        let allocator = self.allocator();
        let mut result = TrimResult::Done(None);
        let mut deallocated = 0;

        while let Some(item_ref) = iter.get() {
            if item_ref.key.object_id != object_id {
                break;
            }
            if let ObjectKey {
                data: ObjectKeyData::Attribute(extent_attribute_id, attribute_key),
                ..
            } = item_ref.key
            {
                if *extent_attribute_id != attribute_id {
                    result = TrimResult::Done(Some(*extent_attribute_id));
                    break;
                }
                if let (
                    AttributeKey::Extent(ExtentKey { range }),
                    ObjectValue::Extent(ExtentValue::Some { device_offset, .. }),
                ) = (attribute_key, item_ref.value)
                {
                    let start = std::cmp::max(range.start, aligned_offset);
                    ensure!(start < range.end, FxfsError::Inconsistent);
                    let device_offset = device_offset
                        .checked_add(start - range.start)
                        .ok_or(FxfsError::Inconsistent)?;
                    end = range.end;
                    let len = end - start;
                    allocator
                        .deallocate(
                            transaction,
                            self.store_object_id,
                            device_offset..device_offset + len,
                        )
                        .await?;
                    deallocated += len;
                    // Stop if the transaction is getting too big.
                    if transaction.mutations.len() >= TRANSACTION_MUTATION_THRESHOLD {
                        result = TrimResult::Incomplete;
                        break;
                    }
                }
            }
            iter.advance().await?;
        }

        if matches!(mode, TrimMode::Tombstone) && matches!(result, TrimResult::Done(None)) {
            // Tombstone records *must* be merged so as to consume all other records for the
            // object.
            transaction.add(
                self.store_object_id,
                Mutation::merge_object(ObjectKey::object(object_id), ObjectValue::None),
            );
        } else if deallocated > 0 {
            transaction.add(
                self.store_object_id,
                Mutation::merge_object(
                    ObjectKey::extent(object_id, attribute_id, aligned_offset..end),
                    ObjectValue::deleted_extent(),
                ),
            );

            // Update allocated size.
            let mut mutation = self.txn_get_object_mutation(transaction, object_id).await?;
            if let ObjectValue::Object { kind: ObjectKind::File { allocated_size, .. }, .. } =
                &mut mutation.item.value
            {
                // The only way for these to fail are if the volume is inconsistent.
                *allocated_size = allocated_size.checked_sub(deallocated).ok_or_else(|| {
                    anyhow!(FxfsError::Inconsistent).context("Allocated size overflow")
                })?;
            } else {
                panic!("Unexpected object value");
            }
            transaction.add(self.store_object_id, Mutation::ObjectStore(mutation));
        }
        Ok(result)
    }

    /// Returns all objects that exist in the parent store that pertain to this object store.
    /// Note that this doesn't include the object_id of the store itself which is generally
    /// referenced externally.
    pub fn parent_objects(&self) -> Vec<u64> {
        assert!(self.store_info_handle.get().is_some());
        let mut objects = Vec::new();
        // We should not include the ID of the store itself, since that should be referred to in the
        // volume directory.
        let guard = self.store_info.lock().unwrap();
        let store_info = guard.info().unwrap();
        objects.extend_from_slice(&store_info.layers);
        if store_info.encrypted_mutations_object_id != INVALID_OBJECT_ID {
            objects.push(store_info.encrypted_mutations_object_id);
        }
        objects
    }

    /// Returns the object ID of all layer files for this store (which are in the parent store).
    pub fn layer_file_object_ids(&self) -> Vec<u64> {
        assert!(self.store_info_handle.get().is_some());
        let guard = self.store_info.lock().unwrap();
        let store_info = guard.info().unwrap();
        store_info.layers.clone()
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
    async fn on_replay_complete(&self) -> Result<(), Error> {
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

        let (object_tree_layer_object_ids, encrypted) = {
            let mut info = if handle.get_size() > 0 {
                let serialized_info = handle.contents(MAX_STORE_INFO_SERIALIZED_SIZE).await?;
                let mut cursor = std::io::Cursor::new(&serialized_info[..]);
                let (store_info, _) = StoreInfo::deserialize_with_version(&mut cursor)
                    .context("Failed to deserialize StoreInfo")?;
                if store_info.object_id_key.is_none() {
                    self.update_last_object_id(store_info.last_object_id);
                }
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
                if replay_info.object_count_delta < 0 {
                    info.object_count =
                        info.object_count.saturating_sub(-replay_info.object_count_delta as u64);
                } else {
                    info.object_count =
                        info.object_count.saturating_add(replay_info.object_count_delta as u64);
                }
                encrypted_mutations
                    .extend(&std::mem::take(&mut replay_info.encrypted_mutations))?;
            }

            let result = (
                info.layers.clone(),
                if info.mutations_key.is_some() {
                    Some(info.encrypted_mutations_object_id)
                } else {
                    None
                },
            );
            *store_info = StoreOrReplayInfo::Info(info);
            result
        };

        if encrypted.is_some() {
            *self.lock_state.lock().unwrap() = LockState::Locked(encrypted_mutations);
        } else {
            *self.lock_state.lock().unwrap() = LockState::Unencrypted;
        }

        // TODO(fxbug.dev/95978): the layer size here could be bad and cause overflow.

        // If the store is encrypted, we can't open the object tree layers now, but we need to
        // compute the size of the layers.
        let total_size: u64 = if let Some(encrypted_mutations_object_id) = encrypted {
            let mut size = 0;
            let parent_store = self.parent_store.as_ref().unwrap();
            for oid in object_tree_layer_object_ids.into_iter() {
                size += parent_store.get_file_size(oid).await?;
            }
            if encrypted_mutations_object_id != INVALID_OBJECT_ID {
                size += layer_size_from_encrypted_mutations_size(
                    parent_store.get_file_size(encrypted_mutations_object_id).await?,
                );
            }
            size
        } else {
            let object_layers = self.open_layers(object_tree_layer_object_ids, None).await?;
            let size: u64 = object_layers.iter().map(|h| h.get_size()).sum();
            self.tree
                .append_layers(object_layers.into())
                .await
                .context("Failed to read object store layers")?;
            *self.lock_state.lock().unwrap() = LockState::Unencrypted;
            size
        };

        assert!(self.store_info_handle.set(handle).is_ok(), "Failed to set store_info_handle!");
        self.filesystem().object_manager().update_reservation(
            self.store_object_id,
            tree::reservation_amount_from_layer_size(total_size),
        );

        Ok(())
    }

    async fn open_layers(
        &self,
        object_ids: impl std::iter::IntoIterator<Item = u64>,
        crypt: Option<&dyn Crypt>,
    ) -> Result<Vec<CachingObjectHandle<ObjectStore>>, Error> {
        let parent_store = self.parent_store.as_ref().unwrap();
        let mut handles = Vec::new();
        let mut sizes = Vec::new();
        for object_id in object_ids {
            let handle = CachingObjectHandle::new(
                ObjectStore::open_object(&parent_store, object_id, HandleOptions::default(), crypt)
                    .await
                    .context(format!("Failed to open layer file {}", object_id))?,
            );
            sizes.push(handle.get_size());
            handles.push(handle);
        }
        self.counters.lock().unwrap().persistent_layer_file_sizes = sizes;
        Ok(handles)
    }

    /// Unlocks a store so that it is ready to be used.
    /// This is not thread-safe.
    pub async fn unlock(&self, crypt: Arc<dyn Crypt>) -> Result<(), Error> {
        self.unlock_inner(crypt, /*read_only=*/ false).await
    }

    /// Unlocks a store so that it is ready to be read from.
    /// The store will generally behave like it is still locked: when flushed, the store will
    /// write out its mutations into the encrypted mutations file, rather than directly updating
    /// the layer files of the object store.
    /// Re-locking the store (which *must* be done with `Self::lock_read_only` will not trigger a
    /// flush, although the store might still be flushed during other operations.
    /// This is not thread-safe.
    pub async fn unlock_read_only(&self, crypt: Arc<dyn Crypt>) -> Result<(), Error> {
        self.unlock_inner(crypt, /*read_only=*/ true).await
    }

    async fn unlock_inner(&self, crypt: Arc<dyn Crypt>, read_only: bool) -> Result<(), Error> {
        match &*self.lock_state.lock().unwrap() {
            LockState::Locked(_) => {}
            LockState::Invalid => bail!(FxfsError::Inconsistent),
            LockState::Unencrypted => bail!(FxfsError::InvalidArgs),
            LockState::Unlocked(_) | LockState::UnlockedReadOnly(..) => {
                bail!(FxfsError::AlreadyBound)
            }
            LockState::Unknown => panic!("Store was unlocked before replay"),
            LockState::Locking(_) => panic!("Store is being locked"),
            LockState::Unlocking => panic!("Store is being unlocked"),
        }

        // We must lock flushing since that can modify store_info and the encrypted mutations file.
        let keys = [LockKey::flush(self.store_object_id())];
        let fs = self.filesystem();
        let guard = debug_assert_not_too_long!(fs.write_lock(&keys));

        let store_info = self.store_info();
        self.tree
            .append_layers(
                self.open_layers(store_info.layers.iter().cloned(), Some(crypt.as_ref()))
                    .await?
                    .into(),
            )
            .await
            .context("Failed to read object tree layer file contents")?;

        let unwrapped_key = crypt
            .unwrap_key(store_info.mutations_key.as_ref().unwrap(), self.store_object_id)
            .await
            .context("Failed to unwrap mutations keys")?;
        let mut mutations_cipher =
            StreamCipher::new(&unwrapped_key, store_info.mutations_cipher_offset);

        let wrapped_key = store_info.object_id_key.as_ref().ok_or(FxfsError::Inconsistent)?;
        let object_id_cipher =
            Ff1::new(&crypt.unwrap_key(wrapped_key, self.store_object_id).await?);
        {
            let mut last_object_id = self.last_object_id.lock().unwrap();
            last_object_id.cipher = Some(object_id_cipher);
        }
        self.update_last_object_id(store_info.last_object_id);

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
                let mut mutations = EncryptedMutations::deserialize_with_version(&mut cursor)
                    .context("Failed to deserialize EncryptedMutations")?
                    .0;
                let len = cursor.get_ref().len() as u64;
                while cursor.position() < len {
                    mutations.extend(
                        &EncryptedMutations::deserialize_with_version(&mut cursor)
                            .context("Failed to deserialize EncryptedMutations")?
                            .0,
                    )?;
                }
                mutations
            }
        };

        let journaled_encrypted_mutations;
        if let LockState::Locked(m) =
            std::mem::replace(&mut *self.lock_state.lock().unwrap(), LockState::Unlocking)
        {
            mutations.extend(&m)?;
            journaled_encrypted_mutations = m;
        } else {
            unreachable!();
        }

        // If we fail, clean up.
        let clean_up = scopeguard::guard((), |_| {
            *self.lock_state.lock().unwrap() = LockState::Invalid;
            // Make sure we don't leave unencrypted data lying around in memory.
            self.tree.reset();
        });

        let EncryptedMutations { transactions, mut data, mutations_key_roll } = mutations;

        let mut slice = &mut data[..];
        let mut last_offset = 0;
        for (offset, key) in mutations_key_roll {
            let split_offset = offset.checked_sub(last_offset).ok_or(FxfsError::Inconsistent)?;
            last_offset = offset;
            ensure!(split_offset <= slice.len(), FxfsError::Inconsistent);
            let (old, new) = slice.split_at_mut(split_offset);
            mutations_cipher.decrypt(old);
            let unwrapped_key = crypt
                .unwrap_key(&key, self.store_object_id)
                .await
                .context("Failed to unwrap mutations keys")?;
            mutations_cipher = StreamCipher::new(&unwrapped_key, 0);
            slice = new;
        }
        mutations_cipher.decrypt(slice);

        // Always roll the mutations key when we unlock which guarantees we won't reuse a
        // previous key and nonce.
        self.roll_mutations_key(crypt.as_ref()).await?;

        let mut cursor = std::io::Cursor::new(data);
        for (checkpoint, count) in transactions {
            let context = ApplyContext { mode: ApplyMode::Replay, checkpoint };
            for _ in 0..count {
                self.apply_mutation(
                    Mutation::deserialize_from_version(&mut cursor, context.checkpoint.version)
                        .context("failed to deserialize encrypted mutation")?,
                    &context,
                    AssocObj::None,
                )
                .await
                .context("failed to apply encrypted mutation")?;
            }
        }

        *self.lock_state.lock().unwrap() = if read_only {
            LockState::UnlockedReadOnly(crypt, store_info, journaled_encrypted_mutations)
        } else {
            LockState::Unlocked(crypt)
        };

        // To avoid unbounded memory growth, we should flush the encrypted mutations now. Otherwise
        // it's possible for more writes to be queued and for the store to be locked before we can
        // flush anything and that can repeat.
        std::mem::drop(guard);

        if !read_only && !self.filesystem().options().read_only {
            self.flush_with_reason(flush::Reason::Unlock).await?;

            // Reap purged files within this store.
            let _ = self.filesystem().graveyard().initial_reap(&self).await?;
        }

        // Return and cancel the clean up.
        Ok(ScopeGuard::into_inner(clean_up))
    }

    pub fn is_locked(&self) -> bool {
        matches!(
            *self.lock_state.lock().unwrap(),
            LockState::Locked(_) | LockState::Locking(_) | LockState::Unknown
        )
    }

    pub fn is_unknown(&self) -> bool {
        matches!(*self.lock_state.lock().unwrap(), LockState::Unknown)
    }

    // Locks a store.  This assumes no other concurrent access to the store.  Whilst this can return
    // an error, the store will be placed into an unusable but safe state (i.e. no lingering
    // unencrypted data) if an error is encountered.
    pub async fn lock(&self) -> Result<(), Error> {
        {
            let mut lock_state = self.lock_state.lock().unwrap();
            if let LockState::Unlocked(crypt) = &*lock_state {
                *lock_state = LockState::Locking(crypt.clone());
            } else {
                panic!("Unexpected lock state: {:?}", &*lock_state);
            }
        }

        // We must flush because we want to discard unencrypted data and we can't easily replay
        // again later if we try and unlock this store again.
        let flush_result = self.flush_with_reason(flush::Reason::Lock).await;

        *self.lock_state.lock().unwrap() = if flush_result.is_err() {
            LockState::Invalid
        } else {
            // There should have been no concurrent access with the store so there should be nothing
            // to flush.
            assert!(!self.filesystem().object_manager().needs_flush(self.store_object_id));
            LockState::Locked(EncryptedMutations::default())
        };

        self.tree.reset();
        flush_result?;
        Ok(())
    }

    // Locks a store which was previously unlocked read-only (see `Self::unlock_read_only`).  Data
    // is not flushed, and instead any journaled mutations are buffered back into the ObjectStore
    // and will be replayed next time the store is unlocked.
    // Whilst this can return an error, the store will be placed into an unusable but safe state
    // (i.e. no lingering unencrypted data) if an error is encountered.
    pub fn lock_read_only(&self) -> Result<(), Error> {
        let mut store_info = self.store_info.lock().unwrap();
        let mut lock_state = self.lock_state.lock().unwrap();
        let (old_store_info, journaled_encrypted_mutations) =
            if let LockState::UnlockedReadOnly(_, store_info, mutations) =
                std::mem::replace(&mut *lock_state, LockState::Invalid)
            {
                (store_info, mutations)
            } else {
                panic!("Unexpected lock state: {:?}", &*lock_state);
            };
        *lock_state = LockState::Locked(journaled_encrypted_mutations);
        *store_info = StoreOrReplayInfo::Info(old_store_info);
        self.tree.reset();
        Ok(())
    }

    // Returns INVALID_OBJECT_ID if the object ID cipher needs to be created or rolled.
    fn maybe_get_next_object_id(&self) -> u64 {
        let mut last_object_id = self.last_object_id.lock().unwrap();
        if last_object_id.should_create_cipher() {
            INVALID_OBJECT_ID
        } else {
            last_object_id.get_next_object_id()
        }
    }

    // Returns a new object ID that can be used.  This will create an object ID cipher if necessary.
    pub async fn get_next_object_id(&self) -> Result<u64, Error> {
        let object_id = self.maybe_get_next_object_id();
        if object_id != INVALID_OBJECT_ID {
            return Ok(object_id);
        }

        // Create a transaction (which has a lock) and then check again.
        let mut transaction = self
            .filesystem()
            .new_transaction(
                &[LockKey::object(
                    self.parent_store.as_ref().unwrap().store_object_id,
                    self.store_object_id,
                )],
                Options {
                    // We must skip journal checks because this transaction might be needed to
                    // compact.
                    skip_journal_checks: true,
                    borrow_metadata_space: true,
                    ..Default::default()
                },
            )
            .await?;

        {
            let mut last_object_id = self.last_object_id.lock().unwrap();
            if !last_object_id.should_create_cipher() {
                // We lost a race.
                return Ok(last_object_id.get_next_object_id());
            }
        }

        // Create a key.
        let (object_id_wrapped, object_id_unwrapped) =
            self.crypt().unwrap().create_key(self.store_object_id, KeyPurpose::Metadata).await?;

        // Update StoreInfo.
        let buf = {
            let mut store_info = self.store_info.lock().unwrap();
            let mut store_info = store_info.info_mut().unwrap();
            store_info.object_id_key = Some(object_id_wrapped);
            let mut serialized_info = Vec::new();
            store_info.serialize_with_version(&mut serialized_info)?;
            let mut buf = self.device.allocate_buffer(serialized_info.len());
            buf.as_mut_slice().copy_from_slice(&serialized_info[..]);
            buf
        };

        self.store_info_handle
            .get()
            .unwrap()
            .txn_write(&mut transaction, 0u64, buf.as_ref())
            .await?;
        transaction.commit().await?;

        let mut last_object_id = self.last_object_id.lock().unwrap();
        last_object_id.cipher = Some(Ff1::new(&object_id_unwrapped));
        last_object_id.id = (last_object_id.id + (1 << 32)) & OBJECT_ID_HI_MASK;

        Ok((last_object_id.id & OBJECT_ID_HI_MASK)
            | last_object_id.cipher.as_ref().unwrap().encrypt(last_object_id.id as u32) as u64)
    }

    fn allocator(&self) -> Arc<SimpleAllocator> {
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

    fn update_last_object_id(&self, mut object_id: u64) {
        let mut last_object_id = self.last_object_id.lock().unwrap();
        // For encrypted stores, object_id will be encrypted here, so we must decrypt first.
        if let Some(cipher) = &last_object_id.cipher {
            // If the object ID cipher has been rolled, then it's possible we might see object IDs
            // that were generated using a different cipher so the decrypt here will return the
            // wrong value, but that won't matter because the hi part of the object ID should still
            // discriminate.
            object_id = object_id & OBJECT_ID_HI_MASK | cipher.decrypt(object_id as u32) as u64;
        }
        if object_id > last_object_id.id {
            last_object_id.id = object_id;
        }
    }

    /// Adds the specified object to the graveyard.
    fn add_to_graveyard(&self, transaction: &mut Transaction<'_>, object_id: u64) {
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

    /// Removes the specified object from the graveyard.  NB: Care should be taken when calling
    /// this because graveyard entries are used for purging deleted files *and* for trimming
    /// extents.  For example, consider the following sequence:
    ///
    ///     1. Add Trim graveyard entry.
    ///     2. Replace with Some graveyard entry (see above).
    ///     3. Remove graveyard entry.
    ///
    /// If the desire in #3 is just to cancel the effect of the Some entry, then #3 should
    /// actually be:
    ///
    ///     3. Replace with Trim graveyard entry.
    fn remove_from_graveyard(&self, transaction: &mut Transaction<'_>, object_id: u64) {
        transaction.add(
            self.store_object_id,
            Mutation::replace_or_insert_object(
                ObjectKey::graveyard_entry(self.graveyard_directory_object_id(), object_id),
                ObjectValue::None,
            ),
        );
    }

    // Roll the mutations key.  The new key will be written for the next encrypted mutation.
    async fn roll_mutations_key(&self, crypt: &dyn Crypt) -> Result<(), Error> {
        let (wrapped_key, unwrapped_key) =
            crypt.create_key(self.store_object_id, KeyPurpose::Metadata).await?;

        // The mutations_cipher lock must be held for the duration so that mutations_cipher and
        // store_info are updated atomically.  Otherwise, write_mutation could find a new cipher but
        // end up writing the wrong wrapped key.
        let mut cipher = self.mutations_cipher.lock().unwrap();
        *cipher = Some(StreamCipher::new(&unwrapped_key, 0));
        self.store_info.lock().unwrap().info_mut().unwrap().mutations_key = Some(wrapped_key);
        // mutations_cipher_offset is updated by flush.
        Ok(())
    }
}

#[async_trait]
impl JournalingObject for ObjectStore {
    async fn apply_mutation(
        &self,
        mutation: Mutation,
        context: &ApplyContext<'_, '_>,
        _assoc_obj: AssocObj<'_>,
    ) -> Result<(), Error> {
        if context.mode.is_live() {
            let lock_state = self.lock_state.lock().unwrap();
            match &*lock_state {
                LockState::Locked(_) | LockState::Locking(_) => {
                    assert_matches!(mutation, Mutation::BeginFlush | Mutation::EndFlush)
                }
                LockState::Invalid
                | LockState::Unlocking
                | LockState::Unencrypted
                | LockState::Unlocked(_)
                | LockState::UnlockedReadOnly(..) => {}
                _ => panic!("Unexpected lock state: {:?}", &*lock_state),
            }
        }
        match mutation {
            Mutation::ObjectStore(ObjectStoreMutation { mut item, op }) => {
                item.sequence = context.checkpoint.file_offset;
                match op {
                    Operation::Insert => {
                        // If we are inserting an object record for the first time, it signifies the
                        // birth of the object so we need to adjust the object count.
                        if matches!(item.value, ObjectValue::Object { .. }) {
                            self.store_info.lock().unwrap().adjust_object_count(1);
                            if context.mode.is_replay() {
                                self.update_last_object_id(item.key.object_id);
                            }
                        }
                        self.tree.insert(item).await.expect("Insert overwrote existing object.");
                    }
                    Operation::ReplaceOrInsert => {
                        self.tree.replace_or_insert(item).await;
                    }
                    Operation::Merge => {
                        if item.is_tombstone() {
                            self.store_info.lock().unwrap().adjust_object_count(-1);
                        }
                        let lower_bound = item.key.key_for_merge_into();
                        self.tree.merge_into(item, &lower_bound).await;
                    }
                }
            }
            Mutation::BeginFlush => {
                self.tree.seal().await;
                self.store_info.lock().unwrap().begin_flush();
            }
            Mutation::EndFlush => {
                if context.mode.is_replay() {
                    self.tree.reset_immutable_layers();
                    self.store_info.lock().unwrap().end_flush();
                }
            }
            Mutation::EncryptedObjectStore(data) => {
                self.store_info.lock().unwrap().push_encrypted_mutation(&context.checkpoint, data);
            }
            Mutation::UpdateMutationsKey(UpdateMutationsKey(key)) => {
                if context.mode.is_replay() {
                    self.store_info.lock().unwrap().set_mutations_key(key);
                }
            }
            _ => bail!("unexpected mutation: {:?}", mutation),
        }
        self.counters.lock().unwrap().mutations_applied += 1;
        Ok(())
    }

    fn drop_mutation(&self, _mutation: Mutation, _transaction: &Transaction<'_>) {
        self.counters.lock().unwrap().mutations_dropped += 1;
    }

    /// Push all in-memory structures to the device. This is not necessary for sync since the
    /// journal will take care of it.  This is supposed to be called when there is either memory or
    /// space pressure (flushing the store will persist in-memory data and allow the journal file to
    /// be trimmed).
    ///
    /// Also returns the earliest version of a struct in the filesystem (when known).
    async fn flush(&self) -> Result<Version, Error> {
        return self.flush_with_reason(flush::Reason::Journal).await;
    }

    fn write_mutation(&self, mutation: &Mutation, mut writer: journal::Writer<'_>) {
        match mutation {
            // Encrypt all object store mutations.
            Mutation::ObjectStore(_) => {
                let mut cipher = self.mutations_cipher.lock().unwrap();
                if let Some(cipher) = cipher.as_mut() {
                    // If this is the first time we've used this key, we must write the key out.
                    if cipher.offset() == 0 {
                        writer.write(Mutation::update_mutations_key(
                            self.store_info
                                .lock()
                                .unwrap()
                                .info()
                                .unwrap()
                                .mutations_key
                                .as_ref()
                                .unwrap()
                                .clone(),
                        ));
                    }
                    let mut buffer = Vec::new();
                    mutation.serialize_into(&mut buffer).unwrap();
                    cipher.encrypt(&mut buffer);
                    writer.write(Mutation::EncryptedObjectStore(buffer.into()));
                    return;
                }
            }
            _ => {}
        }
        writer.write(mutation.clone());
    }
}

// TODO(fxbug.dev/95980): MemDataBuffer has size limits so we should check sizes before we use it.
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

/// Argument to the trim_some method.
pub enum TrimMode {
    /// Trim extents beyond the current size.
    UseSize,

    /// Trim extents beyond the supplied offset.
    FromOffset(u64),

    /// Trim all extents and tombstone if complete.
    Tombstone,
}

/// Result of the trim_some method.
pub enum TrimResult {
    /// We reached the limit of the transaction and more extents might follow.
    Incomplete,

    /// We finished this attribute.  Returns the ID of the next attribute for the same object if
    /// there is one.
    Done(Option<u64>),
}

#[cfg(test)]
mod tests {
    use {
        super::{StoreInfo, MAX_STORE_INFO_SERIALIZED_SIZE, OBJECT_ID_HI_MASK},
        crate::{
            crypt::{
                insecure::InsecureCrypt, Crypt, WrappedKey, WrappedKeyBytes, WRAPPED_KEY_SIZE,
            },
            errors::FxfsError,
            filesystem::{
                Filesystem, FxFilesystem, JournalingObject, OpenFxFilesystem, SyncOptions,
            },
            fsck::fsck,
            lsm_tree::types::{Item, ItemRef, LayerIterator},
            object_handle::{ObjectHandle, ReadObjectHandle, WriteObjectHandle, INVALID_OBJECT_ID},
            object_store::{
                directory::Directory,
                object_record::{ObjectKey, ObjectValue},
                transaction::{Options, TransactionHandler},
                volume::root_volume,
                HandleOptions, ObjectStore,
            },
            serialized_types::VersionedLatest,
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
            let root_volume = root_volume(fs.clone()).await.expect("root_volume failed");
            root_volume
                .new_volume("test", Some(Arc::new(InsecureCrypt::new())))
                .await
                .expect("new_volume failed")
                .store_object_id()
        };

        fs.close().await.expect("close failed");
        let device = fs.take_device().await;
        device.reopen(false);
        let fs = FxFilesystem::open(device).await.expect("open failed");

        {
            let store = fs.object_manager().store(store_id).expect("store not found");
            store.unlock(Arc::new(InsecureCrypt::new())).await.expect("unlock failed");
        }
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
            .expect("create_object failed");
            transaction.commit().await.expect("commit failed");

            // Allocate an extent in the file.
            let mut buffer = child.allocate_buffer(8192);
            buffer.as_mut_slice().fill(0xaa);
            child.write_or_append(Some(0), buffer.as_ref()).await.expect("write failed");

            child.object_id()
        };

        root_store.tombstone(child_id, Options::default()).await.expect("tombstone failed");

        // Let fsck check allocations.
        fsck(fs.clone()).await.expect("fsck failed");
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
            .expect("create_object failed");
            transaction.commit().await.expect("commit failed");

            // Allocate an extent in the file.
            let mut buffer = child.allocate_buffer(8192);
            buffer.as_mut_slice().fill(0xaa);
            child.write_or_append(Some(0), buffer.as_ref()).await.expect("write failed");

            child.object_id()
        };

        root_store.tombstone(child_id, Options::default()).await.expect("tombstone failed");
        assert_matches!(
            root_store.tree.find(&ObjectKey::object(child_id)).await.expect("find failed"),
            Some(Item { value: ObjectValue::None, .. })
        );

        root_store.flush().await.expect("flush failed");

        // There should be no records for the object.
        let layers = root_store.tree.layer_set();
        let mut merger = layers.merger();
        let iter =
            merger.seek(Bound::Included(&ObjectKey::object(child_id))).await.expect("seek failed");
        match iter.get() {
            None => {}
            Some(ItemRef { key: ObjectKey { object_id, .. }, .. }) => {
                assert_ne!(*object_id, child_id)
            }
        }
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

        fsck(fs.clone()).await.expect("fsck failed");
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
                device.reopen(false);
                FxFilesystem::open(device).await.expect("FS open failed")
            }

            let fs = reopen(fs).await;

            let (store_object_id, object_id) = {
                let root_volume = root_volume(fs.clone()).await.expect("root_volume failed");
                let store =
                    root_volume.volume("test", Some(crypt.clone())).await.expect("volume failed");
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

                (store.store_object_id(), object.object_id())
            };

            let fs = reopen(fs).await;

            let check_object = |fs: Arc<dyn Filesystem>| {
                let crypt = crypt.clone();
                async move {
                    let root_volume = root_volume(fs).await.expect("root_volume failed");
                    let volume =
                        root_volume.volume("test", Some(crypt)).await.expect("volume failed");

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

            // At this point the "test" volume is locked.  Before checking the object, flush the
            // filesystem.  This should leave a file with encrypted mutations.
            fs.object_manager().flush().await.expect("flush failed");

            assert_ne!(
                fs.object_manager()
                    .store(store_object_id)
                    .unwrap()
                    .store_info()
                    .encrypted_mutations_object_id,
                INVALID_OBJECT_ID
            );

            check_object(fs.clone()).await;

            // Checking the object should have triggered a flush and so now there should be no
            // encrypted mutations object.
            assert_eq!(
                fs.object_manager()
                    .store(store_object_id)
                    .unwrap()
                    .store_info()
                    .encrypted_mutations_object_id,
                INVALID_OBJECT_ID
            );

            let fs = reopen(fs).await;

            fsck(fs.clone()).await.expect("fsck failed");

            let fs = reopen(fs).await;

            check_object(fs.clone()).await;

            fs
        }

        let mut fs = test_filesystem().await;
        let crypt = Arc::new(InsecureCrypt::new());

        {
            let root_volume = root_volume(fs.clone()).await.expect("root_volume failed");
            let _store = root_volume
                .new_volume("test", Some(crypt.clone()))
                .await
                .expect("new_volume failed");
        }

        // Run a few iterations so that we test changes with the stream cipher offset.
        for i in 0..5 {
            fs = one_iteration(fs, crypt.clone(), i).await;
        }
    }

    #[fasync::run(10, test)]
    async fn test_object_id_cipher_roll() {
        let fs = test_filesystem().await;
        let crypt = Arc::new(InsecureCrypt::new());

        {
            let root_volume = root_volume(fs.clone()).await.expect("root_volume failed");
            let store = root_volume
                .new_volume("test", Some(crypt.clone()))
                .await
                .expect("new_volume failed");

            let store_info = store.store_info();

            // Hack the last object ID to force a roll of the object ID cipher.
            {
                let mut last_object_id = store.last_object_id.lock().unwrap();
                assert_eq!(last_object_id.id & OBJECT_ID_HI_MASK, 1u64 << 32);
                last_object_id.id |= 0xffffffff;
            }

            let root_directory = Directory::open(&store, store.root_directory_object_id())
                .await
                .expect("open failed");

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

            assert_eq!(object.object_id() & OBJECT_ID_HI_MASK, 2u64 << 32);

            // Check that the key has been changed.
            assert_ne!(store.store_info().object_id_key, store_info.object_id_key);

            assert_eq!(store.last_object_id.lock().unwrap().id, 2u64 << 32);
        };

        fs.close().await.expect("Close failed");
        let device = fs.take_device().await;
        device.reopen(false);
        let fs = FxFilesystem::open(device).await.expect("open failed");
        let root_volume = root_volume(fs.clone()).await.expect("root_volume failed");
        let store = root_volume.volume("test", Some(crypt.clone())).await.expect("volume failed");

        assert_eq!(store.last_object_id.lock().unwrap().id, 2u64 << 32);
    }

    #[fasync::run(10, test)]
    async fn test_lock_store() {
        let fs = test_filesystem().await;
        let crypt = Arc::new(InsecureCrypt::new());

        let root_volume = root_volume(fs.clone()).await.expect("root_volume failed");
        let store =
            root_volume.new_volume("test", Some(crypt.clone())).await.expect("new_volume failed");
        let root_directory =
            Directory::open(&store, store.root_directory_object_id()).await.expect("open failed");
        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        root_directory
            .create_child_file(&mut transaction, "test")
            .await
            .expect("create_child_file failed");
        transaction.commit().await.expect("commit failed");
        store.lock().await.expect("lock failed");

        store.unlock(crypt).await.expect("unlock failed");
        root_directory.lookup("test").await.expect("lookup failed").expect("not found");
    }

    #[fasync::run(10, test)]
    async fn test_unlock_read_only() {
        let fs = test_filesystem().await;
        let crypt = Arc::new(InsecureCrypt::new());

        let root_volume = root_volume(fs.clone()).await.expect("root_volume failed");
        let store =
            root_volume.new_volume("test", Some(crypt.clone())).await.expect("new_volume failed");
        let root_directory =
            Directory::open(&store, store.root_directory_object_id()).await.expect("open failed");
        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        root_directory
            .create_child_file(&mut transaction, "test")
            .await
            .expect("create_child_file failed");
        transaction.commit().await.expect("commit failed");
        store.lock().await.expect("lock failed");

        store.unlock_read_only(crypt.clone()).await.expect("unlock failed");
        root_directory.lookup("test").await.expect("lookup failed").expect("not found");
        store.lock_read_only().expect("lock failed");
        store.unlock_read_only(crypt).await.expect("unlock failed");
        root_directory.lookup("test").await.expect("lookup failed").expect("not found");
    }

    #[fasync::run(10, test)]
    async fn test_key_rolled_when_unlocked() {
        let fs = test_filesystem().await;
        let crypt = Arc::new(InsecureCrypt::new());

        let object_id;
        {
            let root_volume = root_volume(fs.clone()).await.expect("root_volume failed");
            let store = root_volume
                .new_volume("test", Some(crypt.clone()))
                .await
                .expect("new_volume failed");
            let root_directory = Directory::open(&store, store.root_directory_object_id())
                .await
                .expect("open failed");
            let mut transaction = fs
                .clone()
                .new_transaction(&[], Options::default())
                .await
                .expect("new_transaction failed");
            object_id = root_directory
                .create_child_file(&mut transaction, "test")
                .await
                .expect("create_child_file failed")
                .object_id();
            transaction.commit().await.expect("commit failed");
        }

        fs.close().await.expect("Close failed");
        let mut device = fs.take_device().await;

        // Repeatedly remount so that we can be sure that we can remount when there are many
        // mutations keys.
        for _ in 0..100 {
            device.reopen(false);
            let fs = FxFilesystem::open(device).await.expect("open failed");
            {
                let root_volume = root_volume(fs.clone()).await.expect("root_volume failed");
                let store = root_volume
                    .volume("test", Some(crypt.clone()))
                    .await
                    .expect("open_volume failed");

                // The key should get rolled every time we unlock.
                assert_eq!(store.mutations_cipher.lock().unwrap().as_ref().unwrap().offset(), 0);

                // Make sure there's an encrypted mutation.
                let handle =
                    ObjectStore::open_object(&store, object_id, HandleOptions::default(), None)
                        .await
                        .expect("open_object failed");
                let buffer = handle.allocate_buffer(100);
                handle
                    .write_or_append(Some(0), buffer.as_ref())
                    .await
                    .expect("write_or_append failed");
            }
            fs.close().await.expect("Close failed");
            device = fs.take_device().await;
        }
    }

    #[test]
    fn test_store_info_max_serialized_size() {
        let info = StoreInfo {
            guid: [0xff; 16],
            last_object_id: 0x1234567812345678,
            // Worst case, each layer should be 3/4 the size of the layer below it (because of the
            // compaction policy we're using).  If the smallest layer is 8,192 bytes, then 120
            // layers would take up a size that exceeds a 64 bit unsigned integer, so if this fits,
            // any size should fit.
            layers: vec![0x1234567812345678; 120],
            root_directory_object_id: 0x1234567812345678,
            graveyard_directory_object_id: 0x1234567812345678,
            object_count: 0x1234567812345678,
            mutations_key: Some(WrappedKey {
                wrapping_key_id: 0x1234567812345678,
                key: WrappedKeyBytes([0xff; WRAPPED_KEY_SIZE]),
            }),
            mutations_cipher_offset: 0x1234567812345678,
            encrypted_mutations_object_id: 0x1234567812345678,
            object_id_key: Some(WrappedKey {
                wrapping_key_id: 0x1234567812345678,
                key: WrappedKeyBytes([0xff; WRAPPED_KEY_SIZE]),
            }),
        };
        let mut serialized_info = Vec::new();
        info.serialize_with_version(&mut serialized_info).unwrap();
        assert!(
            serialized_info.len() <= MAX_STORE_INFO_SERIALIZED_SIZE,
            "{}",
            serialized_info.len()
        );
    }
}
