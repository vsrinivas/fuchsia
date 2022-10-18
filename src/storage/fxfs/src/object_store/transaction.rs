// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        crypt::WrappedKey,
        debug_assert_not_too_long,
        log::*,
        lsm_tree::types::Item,
        object_handle::INVALID_OBJECT_ID,
        object_store::{
            allocator::{AllocatorItem, Reservation},
            object_manager::{reserved_space_from_journal_usage, ObjectManager},
            object_record::{ObjectItem, ObjectKey, ObjectValue},
        },
        serialized_types::Versioned,
    },
    anyhow::Error,
    async_trait::async_trait,
    either::{Either, Left, Right},
    futures::future::poll_fn,
    serde::{Deserialize, Serialize},
    std::{
        cmp::Ordering,
        collections::{
            hash_map::{Entry, HashMap},
            BTreeSet,
        },
        convert::{From, Into},
        ops::{Deref, DerefMut, Range},
        sync::{Arc, Mutex},
        task::{Poll, Waker},
        vec::Vec,
    },
};

/// `Options` are provided to types that expose the `TransactionHandler` trait.
///
/// This allows for special handling of certain transactions such as deletes and the
/// extension of Journal extents. For most other use cases it is appropriate to use
/// `default()` here.
#[derive(Clone, Copy, Default)]
pub struct Options<'a> {
    /// If true, don't check for low journal space.  This should be true for any transactions that
    /// might alleviate journal space (i.e. compaction).
    pub skip_journal_checks: bool,

    /// If true, borrow metadata space from the metadata reservation.  This setting should be set to
    /// true for any transaction that will either not affect space usage after compaction
    /// (e.g. setting attributes), or reduce space usage (e.g. unlinking).  Otherwise, a transaction
    /// might fail with an out-of-space error.
    pub borrow_metadata_space: bool,

    /// If specified, a reservation to be used with the transaction.  If not set, any allocations
    /// that are part of this transaction will have to take their chances, and will fail if there is
    /// no free space.  The intention is that this should be used for things like the journal which
    /// require guaranteed space.
    pub allocator_reservation: Option<&'a Reservation>,
}

// This is the amount of space that we reserve for metadata when we are creating a new transaction.
// A transaction should not take more than this.  This is expressed in terms of space occupied in
// the journal; transactions must not take up more space in the journal than the number below.  The
// amount chosen here must be large enough for the maximum possible transaction that can be created,
// so transactions always need to be bounded which might involve splitting an operation up into
// smaller transactions.
pub const TRANSACTION_MAX_JOURNAL_USAGE: u64 = 24_576;
pub const TRANSACTION_METADATA_MAX_AMOUNT: u64 =
    reserved_space_from_journal_usage(TRANSACTION_MAX_JOURNAL_USAGE);

#[must_use]
pub struct TransactionLocks<'a>(pub WriteGuard<'a>);

impl TransactionLocks<'_> {
    pub async fn commit_prepare(&self) {
        self.0.manager.commit_prepare_keys(&self.0.lock_keys).await;
    }
}

#[async_trait]
pub trait TransactionHandler: Send + Sync {
    /// Initiates a new transaction.  Implementations should check to see that a transaction can be
    /// created (for example, by checking to see that the journaling system can accept more
    /// transactions), and then call Transaction::new.
    async fn new_transaction<'a>(
        self: Arc<Self>,
        lock_keys: &[LockKey],
        options: Options<'a>,
    ) -> Result<Transaction<'a>, Error>;

    /// Acquires transaction locks for |lock_keys| which can later be put into a transaction via
    /// new_transaction_with_locks.
    /// This is useful in situations where the lock needs to be held before the transaction options
    /// can be determined, e.g. to take the allocator reservation.
    async fn transaction_lock<'a>(&'a self, lock_keys: &[LockKey]) -> TransactionLocks<'a>;

    /// Implementations should perform any required journaling and then apply the mutations via
    /// ObjectManager's apply_mutation method.  Any mutations within the transaction should be
    /// removed so that drop_transaction can tell that the transaction was committed.  If
    /// successful, returns the journal offset that the transaction was written to.  `callback` will
    /// be called if the transaction commits successfully and whilst locks are held.  See the
    /// comment in Transaction::commit_with_callback for the reason why it's the type that it is.
    async fn commit_transaction(
        self: Arc<Self>,
        transaction: &mut Transaction<'_>,
        callback: &mut (dyn FnMut(u64) + Send),
    ) -> Result<u64, Error>;

    /// Drops a transaction (rolling back if not committed).  Committing a transaction should have
    /// removed the mutations.  This is called automatically when Transaction is dropped, which is
    /// why this isn't async.
    fn drop_transaction(&self, transaction: &mut Transaction<'_>);

    /// Acquires a read lock for the given keys.  Read locks are only blocked whilst a transaction
    /// is being committed for the same locks.  They are only necessary where consistency is
    /// required between different mutations within a transaction.  For example, a write might
    /// change the size and extents for an object, in which case a read lock is required so that
    /// observed size and extents are seen together or not at all.  Implementations should call
    /// through to LockManager's read_lock implementation.
    async fn read_lock<'a>(&'a self, lock_keys: &[LockKey]) -> ReadGuard<'a>;

    /// Acquires a write lock for the given keys.  Write locks provide exclusive access to the
    /// requested lock keys.
    async fn write_lock<'a>(&'a self, lock_keys: &[LockKey]) -> WriteGuard<'a>;
}

/// The journal consists of these records which will be replayed at mount time.  Within a
/// transaction, these are stored as a set which allows some mutations to be deduplicated and found
/// (and we require custom comparison functions below).  For example, we need to be able to find
/// object size changes.
#[derive(Clone, Debug, Eq, Ord, PartialEq, PartialOrd, Serialize, Deserialize, Versioned)]
pub enum Mutation {
    ObjectStore(ObjectStoreMutation),
    EncryptedObjectStore(Box<[u8]>),
    Allocator(AllocatorMutation),
    // Indicates the beginning of a flush.  This would typically involve sealing a tree.
    BeginFlush,
    // Indicates the end of a flush.  This would typically involve replacing the immutable layers
    // with compacted ones.
    EndFlush,
    // Volume has been deleted.  Requires we remove it from the set of managed ObjectStore.
    DeleteVolume,
    UpdateBorrowed(u64),
    UpdateMutationsKey(UpdateMutationsKey),
}

impl Mutation {
    pub fn insert_object(key: ObjectKey, value: ObjectValue) -> Self {
        Mutation::ObjectStore(ObjectStoreMutation {
            item: Item::new(key, value),
            op: Operation::Insert,
        })
    }

    pub fn replace_or_insert_object(key: ObjectKey, value: ObjectValue) -> Self {
        Mutation::ObjectStore(ObjectStoreMutation {
            item: Item::new(key, value),
            op: Operation::ReplaceOrInsert,
        })
    }

    pub fn merge_object(key: ObjectKey, value: ObjectValue) -> Self {
        Mutation::ObjectStore(ObjectStoreMutation {
            item: Item::new(key, value),
            op: Operation::Merge,
        })
    }

    pub fn update_mutations_key(key: WrappedKey) -> Self {
        Mutation::UpdateMutationsKey(UpdateMutationsKey(key))
    }
}

// We have custom comparison functions for mutations that just use the key, rather than the key and
// value that would be used by default so that we can deduplicate and find mutations (see
// get_object_mutation below).

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct ObjectStoreMutation {
    pub item: ObjectItem,
    pub op: Operation,
}

// The different LSM tree operations that can be performed as part of a mutation.
#[derive(Clone, Debug, Serialize, Deserialize)]
pub enum Operation {
    Insert,
    ReplaceOrInsert,
    Merge,
}

impl Ord for ObjectStoreMutation {
    fn cmp(&self, other: &Self) -> Ordering {
        self.item.key.cmp(&other.item.key)
    }
}

impl PartialOrd for ObjectStoreMutation {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

impl PartialEq for ObjectStoreMutation {
    fn eq(&self, other: &Self) -> bool {
        self.item.key.eq(&other.item.key)
    }
}

impl Eq for ObjectStoreMutation {}

impl Ord for AllocatorItem {
    fn cmp(&self, other: &Self) -> Ordering {
        self.key.cmp(&other.key)
    }
}

impl PartialOrd for AllocatorItem {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

/// Same as std::ops::Range but with Ord and PartialOrd support, sorted first by start of the range,
/// then by the end.
#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
pub struct DeviceRange(pub Range<u64>);

impl Deref for DeviceRange {
    type Target = Range<u64>;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl DerefMut for DeviceRange {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.0
    }
}

impl From<Range<u64>> for DeviceRange {
    fn from(range: Range<u64>) -> Self {
        DeviceRange(range)
    }
}

impl Into<Range<u64>> for DeviceRange {
    fn into(self) -> Range<u64> {
        self.0
    }
}

impl Ord for DeviceRange {
    fn cmp(&self, other: &Self) -> Ordering {
        self.start.cmp(&other.start).then(self.end.cmp(&other.end))
    }
}

impl PartialOrd for DeviceRange {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

#[derive(Clone, Debug, Eq, Ord, PartialEq, PartialOrd, Serialize, Deserialize)]
pub enum AllocatorMutation {
    Allocate {
        device_range: DeviceRange,
        owner_object_id: u64,
    },
    Deallocate {
        device_range: DeviceRange,
        owner_object_id: u64,
    },
    SetLimit {
        owner_object_id: u64,
        bytes: u64,
    },
    /// Marks all extents with a given owner_object_id for deletion.
    /// Used to free space allocated to encrypted ObjectStore where we may not have the key.
    /// Note that the actual deletion time is undefined so this should never be used where an
    /// ObjectStore is still in use due to a high risk of corruption. Similarly, owner_object_id
    /// should never be reused for the same reasons.
    MarkForDeletion(u64),
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct UpdateMutationsKey(pub WrappedKey);

impl Ord for UpdateMutationsKey {
    fn cmp(&self, other: &Self) -> Ordering {
        (self as *const UpdateMutationsKey).cmp(&(other as *const _))
    }
}

impl PartialOrd for UpdateMutationsKey {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

impl Eq for UpdateMutationsKey {}

impl PartialEq for UpdateMutationsKey {
    fn eq(&self, other: &Self) -> bool {
        std::ptr::eq(self, other)
    }
}

/// When creating a transaction, locks typically need to be held to prevent two or more writers
/// trying to make conflicting mutations at the same time.  LockKeys are used for this.
#[derive(Clone, Debug, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub enum LockKey {
    /// Used to lock changes to a particular object attribute (e.g. writes).
    ObjectAttribute { store_object_id: u64, object_id: u64, attribute_id: u64 },

    /// Used to lock changes to a particular object (e.g. adding a child to a directory).
    Object { store_object_id: u64, object_id: u64 },

    /// Used to lock changes to the root volume (e.g. adding or removing a volume).
    RootVolume,

    /// Locks the entire filesystem.
    Filesystem,

    /// Used to lock cached writes to an object attribute.
    CachedWrite { store_object_id: u64, object_id: u64, attribute_id: u64 },

    /// Used to lock flushing an object.
    Flush { object_id: u64 },
}

impl LockKey {
    pub fn object_attribute(store_object_id: u64, object_id: u64, attribute_id: u64) -> Self {
        LockKey::ObjectAttribute { store_object_id, object_id, attribute_id }
    }

    pub fn object(store_object_id: u64, object_id: u64) -> Self {
        LockKey::Object { store_object_id, object_id }
    }

    pub fn cached_write(store_object_id: u64, object_id: u64, attribute_id: u64) -> Self {
        LockKey::CachedWrite { store_object_id, object_id, attribute_id }
    }

    pub fn flush(object_id: u64) -> Self {
        LockKey::Flush { object_id }
    }
}

/// Mutations can be associated with an object so that when mutations are applied, updates can be
/// applied to in-memory structures.  For example, we cache object sizes, so when a size change is
/// applied, we can update the cached object size.
pub trait AssociatedObject: Send + Sync {
    fn will_apply_mutation(&self, _mutation: &Mutation, _object_id: u64, _manager: &ObjectManager) {
    }
}

pub enum AssocObj<'a> {
    None,
    Borrowed(&'a (dyn AssociatedObject)),
    Owned(Box<dyn AssociatedObject>),
}

impl AssocObj<'_> {
    pub fn map<R, F: FnOnce(&dyn AssociatedObject) -> R>(&self, f: F) -> Option<R> {
        match self {
            AssocObj::None => None,
            AssocObj::Borrowed(ref b) => Some(f(*b)),
            AssocObj::Owned(ref o) => Some(f(o.as_ref())),
        }
    }
}

pub struct TxnMutation<'a> {
    // This, at time of writing, is either the object ID of an object store, or the object ID of the
    // allocator.  In the case of an object mutation, there's another object ID in the mutation
    // record that would be for the object actually being changed.
    pub object_id: u64,

    // The actual mutation.  This gets serialized to the journal.
    pub mutation: Mutation,

    // An optional associated object for the mutation.  During replay, there will always be no
    // associated object.
    pub associated_object: AssocObj<'a>,
}

// We store TxnMutation in a set, and for that, we only use object_id and mutation and not the
// associated object.
impl Ord for TxnMutation<'_> {
    fn cmp(&self, other: &Self) -> Ordering {
        self.object_id.cmp(&other.object_id).then_with(|| self.mutation.cmp(&other.mutation))
    }
}

impl PartialOrd for TxnMutation<'_> {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

impl PartialEq for TxnMutation<'_> {
    fn eq(&self, other: &Self) -> bool {
        self.object_id.eq(&other.object_id) && self.mutation.eq(&other.mutation)
    }
}

impl Eq for TxnMutation<'_> {}

impl std::fmt::Debug for TxnMutation<'_> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("TxnMutation")
            .field("object_id", &self.object_id)
            .field("mutation", &self.mutation)
            .finish()
    }
}

pub enum MetadataReservation {
    // Metadata space for this transaction is being borrowed from ObjectManager's metadata
    // reservation.
    Borrowed,

    // A metadata reservation was made when the transaction was created.
    Reservation(Reservation),

    // The metadata space is being _held_ within `allocator_reservation`.
    Hold(u64),
}

/// A transaction groups mutation records to be committed as a group.
pub struct Transaction<'a> {
    handler: Arc<dyn TransactionHandler>,

    /// The mutations that make up this transaction.
    pub mutations: BTreeSet<TxnMutation<'a>>,

    // The locks that this transaction currently holds.
    txn_locks: Vec<LockKey>,

    // The read locks that this transaction currently holds.
    read_locks: Vec<LockKey>,

    /// If set, an allocator reservation that should be used for allocations.
    pub allocator_reservation: Option<&'a Reservation>,

    /// The reservation for the metadata for this transaction.
    pub metadata_reservation: MetadataReservation,
}

impl<'a> Transaction<'a> {
    /// Creates a new transaction.  This should typically be called by a TransactionHandler's
    /// implementation of new_transaction.  The read locks are acquired before the transaction
    /// locks (see LockManager for the semantics of the different kinds of locks).
    pub async fn new<H: TransactionHandler + AsRef<LockManager> + 'static>(
        handler: Arc<H>,
        metadata_reservation: MetadataReservation,
        read_locks: &[LockKey],
        txn_locks: &[LockKey],
    ) -> Transaction<'a> {
        let (read_locks, txn_locks) = {
            let lock_manager: &LockManager = handler.as_ref().as_ref();
            let mut read_guard = debug_assert_not_too_long!(lock_manager.read_lock(read_locks));
            let mut write_guard = debug_assert_not_too_long!(lock_manager.txn_lock(txn_locks));
            (std::mem::take(&mut read_guard.lock_keys), std::mem::take(&mut write_guard.lock_keys))
        };
        Transaction {
            handler,
            mutations: BTreeSet::new(),
            txn_locks,
            read_locks,
            allocator_reservation: None,
            metadata_reservation,
        }
    }

    /// Adds a mutation to this transaction.  If the mutation already exists, it is replaced and the
    /// old mutation is returned.
    pub fn add(&mut self, object_id: u64, mutation: Mutation) -> Option<Mutation> {
        assert!(object_id != INVALID_OBJECT_ID);
        self.mutations
            .replace(TxnMutation { object_id, mutation, associated_object: AssocObj::None })
            .map(|m| m.mutation)
    }

    /// Removes a mutation that matches `mutation`.
    pub fn remove(&mut self, object_id: u64, mutation: Mutation) {
        self.mutations.remove(&TxnMutation {
            object_id,
            mutation,
            associated_object: AssocObj::None,
        });
    }

    /// Adds a mutation with an associated object.
    pub fn add_with_object(
        &mut self,
        object_id: u64,
        mutation: Mutation,
        associated_object: AssocObj<'a>,
    ) {
        assert!(object_id != INVALID_OBJECT_ID);
        self.mutations.replace(TxnMutation { object_id, mutation, associated_object });
    }

    /// Returns true if this transaction has no mutations.
    pub fn is_empty(&self) -> bool {
        self.mutations.is_empty()
    }

    /// Searches for an existing object mutation within the transaction that has the given key and
    /// returns it if found.
    pub fn get_object_mutation(
        &self,
        object_id: u64,
        key: ObjectKey,
    ) -> Option<&ObjectStoreMutation> {
        if let Some(TxnMutation { mutation: Mutation::ObjectStore(mutation), .. }) =
            self.mutations.get(&TxnMutation {
                object_id,
                mutation: Mutation::insert_object(key, ObjectValue::None),
                associated_object: AssocObj::None,
            })
        {
            Some(mutation)
        } else {
            None
        }
    }

    /// Commits a transaction.  If successful, returns the journal offset of the transaction.
    pub async fn commit(mut self) -> Result<u64, Error> {
        debug!(txn = ?&self, "Commit");
        self.handler.clone().commit_transaction(&mut self, &mut |_| {}).await
    }

    /// Commits and then runs the callback whilst locks are held.  The callback accepts a single
    /// parameter which is the journal offset of the transaction.
    pub async fn commit_with_callback<R: Send>(
        mut self,
        f: impl FnOnce(u64) -> R + Send,
    ) -> Result<R, Error> {
        debug!(txn = ?&self, "Commit");
        // It's not possible to pass an FnOnce via a trait without boxing it, but we don't want to
        // do that (for performance reasons), hence the reason for the following.
        let mut f = Some(f);
        let mut result = None;
        self.handler
            .clone()
            .commit_transaction(&mut self, &mut |offset| {
                result = Some(f.take().unwrap()(offset));
            })
            .await?;
        Ok(result.unwrap())
    }
}

impl Drop for Transaction<'_> {
    fn drop(&mut self) {
        // Call the TransactionHandler implementation of drop_transaction which should, as a
        // minimum, call LockManager's drop_transaction to ensure the locks are released.
        debug!(txn = ?&self, "Drop");
        self.handler.clone().drop_transaction(self);
    }
}

impl std::fmt::Debug for Transaction<'_> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("Transaction")
            .field("mutations", &self.mutations)
            .field("txn_locks", &self.txn_locks)
            .field("read_locks", &self.read_locks)
            .field("reservation", &self.allocator_reservation)
            .finish()
    }
}

/// LockManager holds the locks that transactions might have taken.  A TransactionManager
/// implementation would typically have one of these.  Three different kinds of locks are supported.
/// There are read locks and write locks, which are as one would expect.  The third kind of lock is
/// a _transaction_ lock.  When first acquired, these block other writes but do not block reads.
/// When it is time to commit a transaction, these locks are upgraded to full write locks and then
/// dropped after committing.  This way, reads are only blocked for the shortest possible time.  It
/// follows that write locks should be used sparingly.
pub struct LockManager {
    locks: Mutex<Locks>,
}

struct Locks {
    sequence: u64,
    keys: HashMap<LockKey, LockEntry>,
}

impl Locks {
    fn drop_read_locks(&mut self, lock_keys: Vec<LockKey>) {
        for lock in lock_keys {
            match self.keys.entry(lock) {
                Entry::Vacant(_) => unreachable!(),
                Entry::Occupied(mut occupied) => {
                    let entry = occupied.get_mut();
                    entry.read_count -= 1;
                    if entry.read_count == 0 {
                        match entry.state {
                            LockState::ReadLock => {
                                occupied.remove_entry();
                            }
                            LockState::Locked => {}
                            LockState::WantWrite(ref waker) => waker.wake_by_ref(),
                            LockState::WriteLock => unreachable!(),
                        }
                    }
                }
            }
        }
    }

    fn drop_write_locks(&mut self, lock_keys: Vec<LockKey>) {
        for lock in lock_keys {
            match self.keys.entry(lock) {
                Entry::Vacant(_) => unreachable!(),
                Entry::Occupied(mut occupied) => {
                    let entry = occupied.get_mut();
                    let wakers = std::mem::take(&mut entry.wakers);
                    match entry.state {
                        LockState::WriteLock => {
                            occupied.remove_entry();
                        }
                        LockState::Locked | LockState::WantWrite(_) => {
                            // There might be active readers referencing the same lock key, so we
                            // shouldn't remove it from the lock-set yet.  The last reader will
                            // remove the entry (See Guard::drop).
                            if entry.read_count == 0 {
                                occupied.remove_entry();
                            } else {
                                entry.state = LockState::ReadLock;
                                self.sequence += 1;
                                entry.sequence = self.sequence;
                            }
                        }
                        LockState::ReadLock => unreachable!(),
                    }
                    for waker in wakers {
                        waker.wake();
                    }
                }
            }
        }
    }
}

#[derive(Debug)]
struct LockEntry {
    sequence: u64,
    read_count: u64,
    state: LockState,
    wakers: Vec<Waker>,
}

#[derive(Clone, Debug)]
enum LockState {
    // In this state, there are only readers.
    ReadLock,

    // This state is used for transactions to lock other writers, but it still allows readers.
    Locked,

    // This state is used to block new readers.  When all existing readers are done, the lock
    // should be promoted to a write lock.
    WantWrite(Waker),

    // A writer has exclusive access; all other readers and writers are blocked.
    WriteLock,
}

impl LockManager {
    pub fn new() -> Self {
        LockManager { locks: Mutex::new(Locks { sequence: 0, keys: HashMap::new() }) }
    }

    /// Acquires the locks.  It is the caller's responsibility to ensure that drop_transaction is
    /// called when a transaction is dropped i.e. implementers of TransactionHandler's
    /// drop_transaction method should call LockManager's drop_transaction method.
    pub async fn txn_lock<'a>(&'a self, lock_keys: &[LockKey]) -> WriteGuard<'a> {
        self.lock(lock_keys, LockState::Locked).await.right().unwrap()
    }

    // `state` indicates the kind of lock required.  ReadLock means acquire a read lock.  Locked
    // means lock other writers, but still allow readers.  WriteLock means acquire a write lock.
    async fn lock<'a>(
        &'a self,
        lock_keys: &[LockKey],
        target_state: LockState,
    ) -> Either<ReadGuard<'a>, WriteGuard<'a>> {
        let mut guard = match &target_state {
            LockState::ReadLock => Left(ReadGuard { manager: self, lock_keys: Vec::new() }),
            LockState::Locked | LockState::WriteLock => {
                Right(WriteGuard { manager: self, lock_keys: Vec::new() })
            }
            LockState::WantWrite(_) => panic!("Use LockState::WriteLocked"),
        };
        let guard_keys = match &mut guard {
            Left(g) => &mut g.lock_keys,
            Right(g) => &mut g.lock_keys,
        };
        let mut lock_keys = lock_keys.to_vec();
        lock_keys.sort_unstable();
        lock_keys.dedup();
        for lock in lock_keys {
            let mut waker_sequence = 0;
            let mut waker_index = 0;
            let mut want_write = false;
            poll_fn(|cx| {
                let mut locks = self.locks.lock().unwrap();
                let Locks { sequence, keys } = &mut *locks;
                match keys.entry(lock.clone()) {
                    Entry::Vacant(vacant) => {
                        *sequence += 1;
                        vacant.insert(LockEntry {
                            sequence: *sequence,
                            read_count: if let LockState::ReadLock = target_state {
                                guard_keys.push(lock.clone());
                                1
                            } else {
                                guard_keys.push(lock.clone());
                                0
                            },
                            state: target_state.clone(),
                            wakers: Vec::new(),
                        });
                        Poll::Ready(())
                    }
                    Entry::Occupied(mut occupied) => {
                        let entry = occupied.get_mut();
                        if want_write {
                            if entry.read_count == 0 {
                                entry.state = LockState::WriteLock;
                                Poll::Ready(())
                            } else {
                                entry.state = LockState::WantWrite(cx.waker().clone());
                                Poll::Pending
                            }
                        } else {
                            match (&entry.state, &target_state) {
                                (LockState::ReadLock, LockState::WriteLock) => {
                                    entry.state = LockState::WantWrite(cx.waker().clone());
                                    want_write = true;
                                    guard_keys.push(lock.clone());
                                    Poll::Pending
                                }
                                (LockState::ReadLock, _)
                                | (LockState::Locked, LockState::ReadLock) => {
                                    if let LockState::ReadLock = target_state {
                                        entry.read_count += 1;
                                        guard_keys.push(lock.clone());
                                    } else {
                                        entry.state = target_state.clone();
                                        guard_keys.push(lock.clone());
                                    }
                                    Poll::Ready(())
                                }
                                _ => {
                                    if entry.sequence == waker_sequence {
                                        entry.wakers[waker_index] = cx.waker().clone();
                                    } else {
                                        waker_index = entry.wakers.len();
                                        waker_sequence = *sequence;
                                        entry.wakers.push(cx.waker().clone());
                                    }
                                    Poll::Pending
                                }
                            }
                        }
                    }
                }
            })
            .await;
        }
        guard
    }

    /// This should be called by a TransactionHandler drop_transaction implementation.
    pub fn drop_transaction(&self, transaction: &mut Transaction<'_>) {
        let mut locks = self.locks.lock().unwrap();
        locks.drop_write_locks(std::mem::take(&mut transaction.txn_locks));
        locks.drop_read_locks(std::mem::take(&mut transaction.read_locks));
    }

    /// Prepares to commit by waiting for readers to finish.
    pub async fn commit_prepare(&self, transaction: &Transaction<'_>) {
        self.commit_prepare_keys(&transaction.txn_locks).await;
    }

    async fn commit_prepare_keys(&self, lock_keys: &[LockKey]) {
        for lock in lock_keys {
            poll_fn(|cx| {
                let mut locks = self.locks.lock().unwrap();
                let entry = locks.keys.get_mut(&lock).expect("key missing!");
                if entry.read_count > 0 {
                    entry.state = LockState::WantWrite(cx.waker().clone());
                    Poll::Pending
                } else {
                    entry.state = LockState::WriteLock;
                    Poll::Ready(())
                }
            })
            .await;
        }
    }

    pub async fn read_lock<'a>(&'a self, lock_keys: &[LockKey]) -> ReadGuard<'a> {
        self.lock(lock_keys, LockState::ReadLock).await.left().unwrap()
    }

    pub async fn write_lock<'a>(&'a self, lock_keys: &[LockKey]) -> WriteGuard<'a> {
        self.lock(lock_keys, LockState::WriteLock).await.right().unwrap()
    }
}

#[must_use]
pub struct ReadGuard<'a> {
    manager: &'a LockManager,
    lock_keys: Vec<LockKey>,
}

impl Drop for ReadGuard<'_> {
    fn drop(&mut self) {
        let mut locks = self.manager.locks.lock().unwrap();
        locks.drop_read_locks(std::mem::take(&mut self.lock_keys));
    }
}

#[must_use]
pub struct WriteGuard<'a> {
    manager: &'a LockManager,
    lock_keys: Vec<LockKey>,
}

impl Drop for WriteGuard<'_> {
    fn drop(&mut self) {
        let mut locks = self.manager.locks.lock().unwrap();
        locks.drop_write_locks(std::mem::take(&mut self.lock_keys));
    }
}

#[cfg(test)]
mod tests {
    use {
        super::{LockKey, LockManager, LockState, Mutation, Options, TransactionHandler},
        crate::filesystem::FxFilesystem,
        fuchsia_async as fasync,
        futures::{channel::oneshot::channel, future::FutureExt, join},
        std::{sync::Mutex, task::Poll, time::Duration},
        storage_device::{fake_device::FakeDevice, DeviceHolder},
    };

    #[fasync::run_singlethreaded(test)]
    async fn test_simple() {
        let device = DeviceHolder::new(FakeDevice::new(4096, 1024));
        let fs = FxFilesystem::new_empty(device).await.expect("new_empty failed");
        let mut t = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        t.add(1, Mutation::BeginFlush);
        assert!(!t.is_empty());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_locks() {
        let device = DeviceHolder::new(FakeDevice::new(4096, 1024));
        let fs = FxFilesystem::new_empty(device).await.expect("new_empty failed");
        let (send1, recv1) = channel();
        let (send2, recv2) = channel();
        let (send3, recv3) = channel();
        let done = Mutex::new(false);
        join!(
            async {
                let _t = fs
                    .clone()
                    .new_transaction(&[LockKey::object_attribute(1, 2, 3)], Options::default())
                    .await
                    .expect("new_transaction failed");
                send1.send(()).unwrap(); // Tell the next future to continue.
                send3.send(()).unwrap(); // Tell the last future to continue.
                recv2.await.unwrap();
                // This is a halting problem so all we can do is sleep.
                fasync::Timer::new(Duration::from_millis(100)).await;
                assert!(!*done.lock().unwrap());
            },
            async {
                recv1.await.unwrap();
                // This should not block since it is a different key.
                let _t = fs
                    .clone()
                    .new_transaction(&[LockKey::object_attribute(2, 2, 3)], Options::default())
                    .await
                    .expect("new_transaction failed");
                // Tell the first future to continue.
                send2.send(()).unwrap();
            },
            async {
                // This should block until the first future has completed.
                recv3.await.unwrap();
                let _t = fs
                    .clone()
                    .new_transaction(&[LockKey::object_attribute(1, 2, 3)], Options::default())
                    .await;
                *done.lock().unwrap() = true;
            }
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_read_lock_after_write_lock() {
        let device = DeviceHolder::new(FakeDevice::new(4096, 1024));
        let fs = FxFilesystem::new_empty(device).await.expect("new_empty failed");
        let (send1, recv1) = channel();
        let (send2, recv2) = channel();
        let done = Mutex::new(false);
        join!(
            async {
                let t = fs
                    .clone()
                    .new_transaction(&[LockKey::object_attribute(1, 2, 3)], Options::default())
                    .await
                    .expect("new_transaction failed");
                send1.send(()).unwrap(); // Tell the next future to continue.
                recv2.await.unwrap();
                t.commit().await.expect("commit failed");
                *done.lock().unwrap() = true;
            },
            async {
                recv1.await.unwrap();
                // Reads should not be blocked until the transaction is committed.
                let _guard = fs.read_lock(&[LockKey::object_attribute(1, 2, 3)]).await;
                // Tell the first future to continue.
                send2.send(()).unwrap();
                // It shouldn't proceed until we release our read lock, but it's a halting
                // problem, so sleep.
                fasync::Timer::new(Duration::from_millis(100)).await;
                assert!(!*done.lock().unwrap());
            },
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_write_lock_after_read_lock() {
        let device = DeviceHolder::new(FakeDevice::new(4096, 1024));
        let fs = FxFilesystem::new_empty(device).await.expect("new_empty failed");
        let (send1, recv1) = channel();
        let (send2, recv2) = channel();
        let done = Mutex::new(false);
        join!(
            async {
                // Reads should not be blocked until the transaction is committed.
                let _guard = fs.read_lock(&[LockKey::object_attribute(1, 2, 3)]).await;
                // Tell the next future to continue and then nwait.
                send1.send(()).unwrap();
                recv2.await.unwrap();
                // It shouldn't proceed until we release our read lock, but it's a halting
                // problem, so sleep.
                fasync::Timer::new(Duration::from_millis(100)).await;
                assert!(!*done.lock().unwrap());
            },
            async {
                recv1.await.unwrap();
                let t = fs
                    .clone()
                    .new_transaction(&[LockKey::object_attribute(1, 2, 3)], Options::default())
                    .await
                    .expect("new_transaction failed");
                send2.send(()).unwrap(); // Tell the first future to continue;
                t.commit().await.expect("commit failed");
                *done.lock().unwrap() = true;
            },
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_drop_uncommitted_transaction() {
        let device = DeviceHolder::new(FakeDevice::new(4096, 1024));
        let fs = FxFilesystem::new_empty(device).await.expect("new_empty failed");
        let key = LockKey::object(1, 1);

        // Dropping while there's a reader.
        {
            let mut write_lock = fs
                .clone()
                .new_transaction(&[key.clone()], Options::default())
                .await
                .expect("new_transaction failed");
            let _read_lock = fs.read_lock(&[key.clone()]).await;
            fs.clone().drop_transaction(&mut write_lock);
        }
        // Dropping while there's no reader.
        let mut write_lock = fs
            .clone()
            .new_transaction(&[key.clone()], Options::default())
            .await
            .expect("new_transaction failed");
        fs.clone().drop_transaction(&mut write_lock);
        // Make sure we can take the lock again (i.e. it was actually released).
        fs.clone()
            .new_transaction(&[key.clone()], Options::default())
            .await
            .expect("new_transaction failed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_drop_waiting_write_lock() {
        let manager = LockManager::new();
        let keys = &[LockKey::object(1, 1)];
        {
            let _guard = manager.lock(keys, LockState::ReadLock).await;
            if let Poll::Ready(_) = futures::poll!(manager.lock(keys, LockState::WriteLock).boxed())
            {
                assert!(false);
            }
        }
        let _ = manager.lock(keys, LockState::WriteLock).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_write_lock_blocks_everything() {
        let manager = LockManager::new();
        let keys = &[LockKey::object(1, 1)];
        {
            let _guard = manager.lock(keys, LockState::WriteLock).await;
            if let Poll::Ready(_) = futures::poll!(manager.lock(keys, LockState::WriteLock).boxed())
            {
                assert!(false);
            }
            if let Poll::Ready(_) = futures::poll!(manager.lock(keys, LockState::ReadLock).boxed())
            {
                assert!(false);
            }
        }
        {
            let _guard = manager.lock(keys, LockState::WriteLock).await;
        }
        {
            let _guard = manager.lock(keys, LockState::ReadLock).await;
        }
    }
}
