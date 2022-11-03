// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The journal is implemented as an ever extending file which contains variable length records
//! that describe mutations to be applied to various objects.  The journal file consists of
//! blocks, with a checksum at the end of each block, but otherwise it can be considered a
//! continuous stream.
//!
//! The checksum is seeded with the checksum from the previous block.  To free space in the
//! journal, records are replaced with sparse extents when it is known they are no longer
//! needed to mount.
//!
//! At mount time, the journal is replayed: the mutations are applied into memory.
//! Eventually, a checksum failure will indicate no more records exist to be replayed,
//! at which point the mount can continue and the journal will be extended from that point with
//! further mutations as required.

mod checksum_list;
mod reader;
pub mod super_block;
mod writer;

use {
    crate::{
        checksum::Checksum,
        debug_assert_not_too_long,
        errors::FxfsError,
        filesystem::{ApplyContext, ApplyMode, Filesystem, SyncOptions},
        log::*,
        object_handle::{BootstrapObjectHandle, ObjectHandle},
        object_store::{
            allocator::{Allocator, SimpleAllocator},
            extent_record::{Checksums, ExtentKey, ExtentValue, DEFAULT_DATA_ATTRIBUTE_ID},
            graveyard::Graveyard,
            journal::{
                checksum_list::ChecksumList,
                reader::{JournalReader, ReadResult},
                super_block::{SuperBlockInstance, SuperBlockManager},
                writer::JournalWriter,
            },
            object_manager::ObjectManager,
            object_record::{AttributeKey, ObjectKey, ObjectKeyData, ObjectValue},
            transaction::{
                AllocatorMutation, Mutation, ObjectStoreMutation, Options, Transaction,
                TxnMutation, TRANSACTION_MAX_JOURNAL_USAGE,
            },
            HandleOptions, Item, ItemRef, LastObjectId, LockState, NewChildStoreOptions,
            ObjectStore, StoreObjectHandle, INVALID_OBJECT_ID,
        },
        range::RangeExt,
        round::{round_down, round_up},
        serialized_types::{Version, Versioned, LATEST_VERSION},
        trace_duration,
    },
    anyhow::{anyhow, bail, Context, Error},
    async_utils::event::Event,
    futures::{self, future::poll_fn, FutureExt},
    once_cell::sync::OnceCell,
    rand::Rng,
    serde::{Deserialize, Serialize},
    static_assertions::const_assert,
    std::{
        clone::Clone,
        collections::HashMap,
        convert::TryFrom as _,
        ops::Bound,
        sync::{
            atomic::{AtomicBool, Ordering},
            Arc, Mutex,
        },
        task::{Poll, Waker},
        vec::Vec,
    },
    storage_device::buffer::Buffer,
    type_hash::TypeHash,
};

// Exposed for serialized_types.
pub use super_block::{SuperBlock, SuperBlockRecord};

// The journal file is written to in blocks of this size.
const BLOCK_SIZE: u64 = 8192;

// The journal file is extended by this amount when necessary.
const CHUNK_SIZE: u64 = 131_072;
const_assert!(CHUNK_SIZE > TRANSACTION_MAX_JOURNAL_USAGE);

// See the comment for the `reclaim_size` member of Inner.
pub const DEFAULT_RECLAIM_SIZE: u64 = 262_144;

// Temporary space that should be reserved for the journal.  For example: space that is currently
// used in the journal file but cannot be deallocated yet because we are flushing.
pub const RESERVED_SPACE: u64 = 1_048_576;

// Whenever the journal is replayed (i.e. the system is unmounted and remounted), we reset the
// journal stream, at which point any half-complete transactions are discarded.  We indicate a
// journal reset by XORing the previous block's checksum with this mask, and using that value as a
// seed for the next journal block.
const RESET_XOR: u64 = 0xffffffffffffffff;

// To keep track of offsets within a journal file, we need both the file offset and the check-sum of
// the preceding block, since the check-sum of the preceding block is an input to the check-sum of
// every block.
#[derive(Clone, Debug, Default, Deserialize, Eq, PartialEq, Serialize, TypeHash)]
pub struct JournalCheckpoint {
    pub file_offset: u64,

    // Starting check-sum for block that contains file_offset i.e. the checksum for the previous
    // block.
    pub checksum: Checksum,

    // If versioned, the version of elements stored in the journal. e.g. JournalRecord version.
    // This can change across reset events so we store it along with the offset and checksum to
    // know which version to deserialize.
    pub version: Version,
}

#[derive(Clone, Debug, Serialize, Deserialize, TypeHash, Versioned)]
#[cfg_attr(fuzz, derive(arbitrary::Arbitrary))]
pub enum JournalRecord {
    // Indicates no more records in this block.
    EndBlock,
    // Mutation for a particular object.  object_id here is for the collection i.e. the store or
    // allocator.
    Mutation { object_id: u64, mutation: Mutation },
    // Commits records in the transaction.
    Commit,
    // Discard all mutations with offsets greater than or equal to the given offset.
    Discard(u64),
    // Indicates the device was flushed at the given journal offset.
    // Note that this really means that at this point in the journal offset, we can be certain that
    // there's no remaining buffered data in the block device; the buffers and the disk contents are
    // consistent.
    // We insert one of these records *after* a flush along with the *next* transaction to go
    // through.  If that never comes (either due to graceful or hard shutdown), the journal reset
    // on the next mount will serve the same purpose and count as a flush, although it is necessary
    // to defensively flush the device before replaying the journal (if possible, i.e. not
    // read-only) in case the block device connection was reused.
    DidFlushDevice(u64),
}

pub(super) fn journal_handle_options() -> HandleOptions {
    HandleOptions { skip_journal_checks: true, ..Default::default() }
}

/// The journal records a stream of mutations that are to be applied to other objects.  At mount
/// time, these records can be replayed into memory.  It provides a way to quickly persist changes
/// without having to make a large number of writes; they can be deferred to a later time (e.g.
/// when a sufficient number have been queued).  It also provides support for transactions, the
/// ability to have mutations that are to be applied atomically together.
pub struct Journal {
    objects: Arc<ObjectManager>,
    handle: OnceCell<StoreObjectHandle<ObjectStore>>,
    super_block_manager: SuperBlockManager,
    inner: Mutex<Inner>,
    writer_mutex: futures::lock::Mutex<()>,
    sync_mutex: futures::lock::Mutex<()>,
    trace: AtomicBool,
}

struct Inner {
    super_block: SuperBlock,

    // This event is used when we are waiting for a compaction to free up journal space.
    reclaim_event: Option<Event>,

    // The offset that we can zero the journal up to now that it is no longer needed.
    zero_offset: Option<u64>,

    // The journal offset that we most recently flushed to the device.
    device_flushed_offset: u64,

    // If true, indicates a DidFlushDevice record is pending.
    needs_did_flush_device: bool,

    // The writer for the journal.
    writer: JournalWriter,

    // Set when a reset is encountered during a read.
    // Used at write pre_commit() time to ensure we write a version first thing after a reset.
    output_reset_version: bool,

    // Waker for the flush task.
    flush_waker: Option<Waker>,

    // Indicates the journal has been terminated.
    terminate: bool,

    // Disable compactions.
    disable_compactions: bool,

    // True if compactions are running.
    compaction_running: bool,

    // Waker for the sync task for when it's waiting for the flush task to finish.
    sync_waker: Option<Waker>,

    // The last offset we flushed to the journal file.
    flushed_offset: u64,

    // If, after replaying, we have to discard a number of mutations (because they don't validate),
    // this offset specifies where we need to discard back to.  This is so that when we next replay,
    // we ignore those mutations and continue with new good mutations.
    discard_offset: Option<u64>,

    // In the steady state, the journal should fluctuate between being approximately half of this
    // number and this number.  New super-blocks will be written every time about half of this
    // amount is written to the journal.
    reclaim_size: u64,
}

impl Inner {
    fn terminate(&mut self) {
        self.terminate = true;
        if let Some(waker) = self.flush_waker.take() {
            waker.wake();
        }
        if let Some(waker) = self.sync_waker.take() {
            waker.wake();
        }
        self.reclaim_event = None;
    }
}

pub struct JournalOptions {
    /// In the steady state, the journal should fluctuate between being approximately half of this
    /// number and this number.  New super-blocks will be written every time about half of this
    /// amount is written to the journal.
    pub reclaim_size: u64,
}

impl Default for JournalOptions {
    fn default() -> Self {
        JournalOptions { reclaim_size: DEFAULT_RECLAIM_SIZE }
    }
}

impl Journal {
    pub fn new(objects: Arc<ObjectManager>, options: JournalOptions) -> Journal {
        let starting_checksum = rand::thread_rng().gen();
        Journal {
            objects: objects,
            handle: OnceCell::new(),
            super_block_manager: SuperBlockManager::new(),
            inner: Mutex::new(Inner {
                super_block: SuperBlock::default(),
                reclaim_event: None,
                zero_offset: None,
                device_flushed_offset: 0,
                needs_did_flush_device: false,
                writer: JournalWriter::new(BLOCK_SIZE as usize, starting_checksum),
                output_reset_version: false,
                flush_waker: None,
                terminate: false,
                disable_compactions: false,
                compaction_running: false,
                sync_waker: None,
                flushed_offset: 0,
                discard_offset: None,
                reclaim_size: options.reclaim_size,
            }),
            writer_mutex: futures::lock::Mutex::new(()),
            sync_mutex: futures::lock::Mutex::new(()),
            trace: AtomicBool::new(false),
        }
    }

    pub fn set_trace(&self, trace: bool) {
        let old_value = self.trace.swap(trace, Ordering::Relaxed);
        if trace != old_value {
            info!(trace, "J: trace");
        }
    }

    /// Used during replay to validate a mutation.  This should return false if the mutation is not
    /// valid and should not be applied.  This could be for benign reasons: e.g. the device flushed
    /// data out-of-order, or because of a malicious actor.
    fn validate_mutation(&self, mutation: &Mutation) -> bool {
        match mutation {
            Mutation::ObjectStore(ObjectStoreMutation {
                item:
                    Item {
                        key:
                            ObjectKey {
                                data:
                                    ObjectKeyData::Attribute(
                                        _,
                                        AttributeKey::Extent(ExtentKey { range }),
                                    ),
                                ..
                            },
                        value:
                            ObjectValue::Extent(ExtentValue::Some {
                                checksums: Checksums::Fletcher(checksums),
                                ..
                            }),
                        ..
                    },
                ..
            }) => {
                if checksums.len() == 0 {
                    return false;
                }
                let len =
                    if let Some(len) = range.length().ok().and_then(|l| usize::try_from(l).ok()) {
                        len
                    } else {
                        return false;
                    };
                if len % checksums.len() != 0 {
                    return false;
                }
                if (len / checksums.len()) % 4 != 0 {
                    return false;
                }
            }
            Mutation::ObjectStore(_) => {}
            Mutation::EncryptedObjectStore(_) => {}
            Mutation::Allocator(AllocatorMutation::Allocate { device_range, owner_object_id }) => {
                return device_range.valid() && *owner_object_id != INVALID_OBJECT_ID;
            }
            Mutation::Allocator(AllocatorMutation::Deallocate {
                device_range,
                owner_object_id,
            }) => {
                return device_range.valid() && *owner_object_id != INVALID_OBJECT_ID;
            }
            Mutation::Allocator(AllocatorMutation::MarkForDeletion(owner_object_id)) => {
                return *owner_object_id != INVALID_OBJECT_ID;
            }
            Mutation::Allocator(AllocatorMutation::SetLimit { owner_object_id, .. }) => {
                return *owner_object_id != INVALID_OBJECT_ID;
            }
            Mutation::BeginFlush => {}
            Mutation::EndFlush => {}
            Mutation::DeleteVolume => {}
            Mutation::UpdateBorrowed(_) => {}
            Mutation::UpdateMutationsKey(_) => {}
        }
        true
    }

    // Assumes that `mutation` has been validated.
    fn update_checksum_list(
        &self,
        journal_offset: u64,
        owner_object_id: u64,
        mutation: &Mutation,
        checksum_list: &mut ChecksumList,
    ) {
        match mutation {
            Mutation::ObjectStore(ObjectStoreMutation {
                item:
                    Item {
                        key:
                            ObjectKey {
                                data:
                                    ObjectKeyData::Attribute(
                                        _,
                                        AttributeKey::Extent(ExtentKey { range }),
                                    ),
                                ..
                            },
                        value:
                            ObjectValue::Extent(ExtentValue::Some {
                                device_offset,
                                checksums: Checksums::Fletcher(checksums),
                                ..
                            }),
                        ..
                    },
                ..
            }) => {
                checksum_list.push(
                    journal_offset,
                    owner_object_id,
                    *device_offset..*device_offset + range.length().unwrap(),
                    checksums,
                );
            }
            Mutation::ObjectStore(_) => {}
            Mutation::Allocator(AllocatorMutation::Deallocate { device_range, .. }) => {
                checksum_list.mark_deallocated(journal_offset, device_range.clone().into());
            }
            _ => {}
        }
    }

    /// Reads the latest super-block, and then replays journaled records.
    pub async fn replay(
        &self,
        filesystem: Arc<dyn Filesystem>,
        on_new_allocator: Option<Box<dyn Fn(Arc<SimpleAllocator>) + Send + Sync>>,
    ) -> Result<(), Error> {
        trace_duration!("Journal::replay");
        let block_size = filesystem.block_size();

        let (super_block, root_parent) =
            self.super_block_manager.load(filesystem.device(), block_size).await?;

        let root_parent = Arc::new(ObjectStore::attach_filesystem(root_parent, filesystem.clone()));

        self.objects.set_root_parent_store(root_parent.clone());
        let allocator =
            Arc::new(SimpleAllocator::new(filesystem.clone(), super_block.allocator_object_id));
        if let Some(on_new_allocator) = on_new_allocator {
            on_new_allocator(allocator.clone());
        }
        self.objects.set_allocator(allocator.clone());
        self.objects.set_borrowed_metadata_space(super_block.borrowed_metadata_space);
        self.objects.set_last_end_offset(super_block.super_block_journal_file_offset);
        {
            let mut inner = self.inner.lock().unwrap();
            inner.super_block = super_block.clone();
        }
        let root_store = ObjectStore::new(
            Some(root_parent.clone()),
            super_block.root_store_object_id,
            filesystem.clone(),
            None,
            None,
            LockState::Unencrypted,
            LastObjectId::default(),
        );
        self.objects.set_root_store(root_store);

        let device = filesystem.device();

        let mut handle;
        {
            let root_parent_layer = root_parent.tree().mutable_layer();
            let mut iter = root_parent_layer
                .seek(Bound::Included(&ObjectKey::attribute(
                    super_block.journal_object_id,
                    DEFAULT_DATA_ATTRIBUTE_ID,
                    AttributeKey::Extent(ExtentKey::search_key_from_offset(round_down(
                        super_block.journal_checkpoint.file_offset,
                        BLOCK_SIZE,
                    ))),
                )))
                .await
                .context("Failed to seek root parent store")?;
            let start_offset = if let Some(ItemRef {
                key:
                    ObjectKey {
                        data:
                            ObjectKeyData::Attribute(
                                DEFAULT_DATA_ATTRIBUTE_ID,
                                AttributeKey::Extent(ExtentKey { range }),
                            ),
                        ..
                    },
                ..
            }) = iter.get()
            {
                range.start
            } else {
                0
            };
            handle = BootstrapObjectHandle::new_with_start_offset(
                super_block.journal_object_id,
                device.clone(),
                start_offset,
            );
            while let Some(item) = iter.get() {
                if !match item.into() {
                    Some((
                        object_id,
                        DEFAULT_DATA_ATTRIBUTE_ID,
                        ExtentKey { range },
                        ExtentValue::Some { device_offset, .. },
                    )) if object_id == super_block.journal_object_id => {
                        if range.start != start_offset + handle.get_size() {
                            bail!(anyhow!(FxfsError::Inconsistent).context(format!(
                                "Unexpected journal extent {:?}, expected start: {}",
                                item,
                                handle.get_size()
                            )));
                        }
                        handle.push_extent(
                            *device_offset
                                ..*device_offset + range.length().context("Invalid extent")?,
                        );
                        true
                    }
                    _ => false,
                } {
                    break;
                }
                iter.advance().await.context("Failed to advance root parent store iterator")?;
            }
        }
        let mut reader =
            JournalReader::new(handle, self.block_size(), &super_block.journal_checkpoint);
        let mut transactions = Vec::new();
        let mut current_transaction = None;
        let mut device_flushed_offset = super_block.super_block_journal_file_offset;
        // Maps from owner_object_id to offset at which it was deleted. Required for checksum
        // validation.
        let mut marked_for_deletion: HashMap<u64, u64> = HashMap::new();
        loop {
            // Cache the checkpoint before we deserialize a record.
            let checkpoint = reader.journal_file_checkpoint();
            let result =
                reader.deserialize().await.context("Failed to deserialize journal record")?;
            match result {
                ReadResult::Reset => {
                    let version;
                    reader.consume({
                        let mut cursor = std::io::Cursor::new(reader.buffer());
                        version = Version::deserialize_from(&mut cursor)
                            .context("Failed to deserialize version")?;
                        cursor.position() as usize
                    });
                    reader.set_version(version);
                    if current_transaction.is_some() {
                        current_transaction = None;
                        transactions.pop();
                    }
                    let offset = reader.journal_file_checkpoint().file_offset;
                    if offset > device_flushed_offset {
                        device_flushed_offset = offset;
                    }
                }
                ReadResult::Some(record) => {
                    match record {
                        JournalRecord::EndBlock => {
                            reader.skip_to_end_of_block();
                        }
                        JournalRecord::Mutation { object_id, mutation } => {
                            let current_transaction = match current_transaction.as_mut() {
                                None => {
                                    transactions.push((checkpoint, Vec::new(), 0));
                                    current_transaction = transactions.last_mut();
                                    current_transaction.as_mut().unwrap()
                                }
                                Some(transaction) => transaction,
                            };
                            // If this mutation doesn't need to be applied, don't bother adding it
                            // to the transaction.
                            if self.should_apply(object_id, &current_transaction.0) {
                                current_transaction.1.push((object_id, mutation));
                            }
                        }
                        JournalRecord::Commit => {
                            if let Some((_checkpoint, mutations, ref mut end_offset)) =
                                current_transaction.take()
                            {
                                for (object_id, mutation) in mutations {
                                    // Snoop the mutations for any that might apply to the journal
                                    // file so that we can pass them to the reader so that it can
                                    // read the journal file.
                                    if *object_id == super_block.root_parent_store_object_id {
                                        if let Mutation::ObjectStore(ObjectStoreMutation {
                                            item:
                                                Item {
                                                    key:
                                                        ObjectKey {
                                                            object_id,
                                                            data:
                                                                ObjectKeyData::Attribute(
                                                                    DEFAULT_DATA_ATTRIBUTE_ID,
                                                                    AttributeKey::Extent(
                                                                        ExtentKey { range },
                                                                    ),
                                                                ),
                                                            ..
                                                        },
                                                    value:
                                                        ObjectValue::Extent(ExtentValue::Some {
                                                            device_offset,
                                                            ..
                                                        }),
                                                    ..
                                                },
                                            ..
                                        }) = mutation
                                        {
                                            let handle = &mut reader.handle();
                                            if *object_id != handle.object_id() {
                                                continue;
                                            }
                                            if range.start
                                                != handle.start_offset() + handle.get_size()
                                            {
                                                bail!(anyhow!(FxfsError::Inconsistent).context(
                                                    format!(
                                                        "Unexpected journal extent {:?} -> {}, \
                                                        expected start: {}",
                                                        range,
                                                        device_offset,
                                                        handle.get_size()
                                                    )
                                                ));
                                            }
                                            handle.push_extent(
                                                *device_offset
                                                    ..*device_offset
                                                        + range
                                                            .length()
                                                            .context("Invalid extent")?,
                                            );
                                        }
                                    }
                                    // If a MarkForDeletion mutation is found, we want to skip
                                    // checksum validation of prior writes.
                                    if let Mutation::Allocator(
                                        AllocatorMutation::MarkForDeletion(owner_object_id),
                                    ) = mutation
                                    {
                                        marked_for_deletion.insert(
                                            *owner_object_id,
                                            reader.journal_file_checkpoint().file_offset,
                                        );
                                    }
                                }
                                *end_offset = reader.journal_file_checkpoint().file_offset;
                            }
                        }
                        JournalRecord::Discard(offset) => {
                            if let Some(transaction) = current_transaction.as_ref() {
                                if transaction.0.file_offset < offset {
                                    // Odd, but OK.
                                    continue;
                                }
                            }
                            current_transaction = None;
                            while let Some(transaction) = transactions.last() {
                                if transaction.0.file_offset < offset {
                                    break;
                                }
                                transactions.pop();
                            }
                            reader.handle().discard_extents(offset);
                        }
                        JournalRecord::DidFlushDevice(offset) => {
                            if offset > device_flushed_offset {
                                device_flushed_offset = offset;
                            }
                        }
                    }
                }
                // This is expected when we reach the end of the journal stream.
                ReadResult::ChecksumMismatch => break,
            }
        }

        // Discard any uncommitted transaction.
        if current_transaction.is_some() {
            transactions.pop();
        }

        // Validate all the mutations.
        let mut checksum_list = ChecksumList::new(device_flushed_offset);
        let mut valid_to = reader.journal_file_checkpoint().file_offset;
        'bad_replay: for (checkpoint, mutations, _) in &transactions {
            for (object_id, mutation) in mutations {
                if !self.validate_mutation(&mutation) {
                    info!(?mutation, "Stopping replay at bad mutation");
                    valid_to = checkpoint.file_offset;
                    break 'bad_replay;
                }
                self.update_checksum_list(
                    checkpoint.file_offset,
                    *object_id,
                    &mutation,
                    &mut checksum_list,
                );
            }
        }

        // Validate the checksums.
        let valid_to = checksum_list
            .verify(device.as_ref(), marked_for_deletion, valid_to)
            .await
            .context("Failed to validate checksums")?;

        // Apply the mutations.
        let last_checkpoint = if transactions.is_empty() {
            super_block.journal_checkpoint.clone()
        } else {
            // Loop used here in place of for {} else {}
            #[allow(clippy::never_loop)]
            'outer: loop {
                for (checkpoint, mutations, end_offset) in transactions {
                    if checkpoint.file_offset >= valid_to {
                        break 'outer checkpoint;
                    }
                    self.objects
                        .replay_mutations(
                            mutations,
                            &ApplyContext { mode: ApplyMode::Replay, checkpoint },
                            end_offset,
                        )
                        .await
                        .context("Failed to replay mutations")?;
                }
                break reader.journal_file_checkpoint();
            }
        };

        let root_store = self.objects.root_store();
        root_store
            .on_replay_complete()
            .await
            .context("Failed to complete replay for root store")?;
        allocator.open().await.context("Failed to open allocator")?;

        // Configure the journal writer so that we can continue.
        {
            if last_checkpoint.file_offset < super_block.super_block_journal_file_offset {
                return Err(anyhow!(FxfsError::Inconsistent).context(format!(
                    "journal replay cut short; journal finishes at {}, but super-block was \
                     written at {}",
                    last_checkpoint.file_offset, super_block.super_block_journal_file_offset
                )));
            }
            let handle = ObjectStore::open_object(
                &root_parent,
                super_block.journal_object_id,
                journal_handle_options(),
                None,
            )
            .await
            .context(format!(
                "Failed to open journal file (object id: {})",
                super_block.journal_object_id
            ))?;
            let _ = self.handle.set(handle);
            let mut inner = self.inner.lock().unwrap();
            let mut reader_checkpoint = reader.journal_file_checkpoint();
            // Reset the stream to indicate that we've remounted the journal.
            reader_checkpoint.checksum ^= RESET_XOR;
            reader_checkpoint.version = LATEST_VERSION;
            inner.device_flushed_offset = device_flushed_offset;
            let mut writer_checkpoint = reader_checkpoint.clone();
            writer_checkpoint.file_offset =
                round_up(writer_checkpoint.file_offset, BLOCK_SIZE).unwrap();
            inner.flushed_offset = writer_checkpoint.file_offset;
            inner.writer.seek(writer_checkpoint);
            inner.output_reset_version = true;
            if last_checkpoint.file_offset < inner.flushed_offset {
                inner.discard_offset = Some(last_checkpoint.file_offset);
            }
        }

        self.objects
            .on_replay_complete()
            .await
            .context("Failed to complete replay for object manager")?;

        if last_checkpoint.file_offset != reader.journal_file_checkpoint().file_offset {
            info!(
                checkpoint = last_checkpoint.file_offset,
                discarded_to = reader.journal_file_checkpoint().file_offset,
                "replay complete"
            );
        } else {
            info!(checkpoint = reader.journal_file_checkpoint().file_offset, "replay complete");
        }
        Ok(())
    }

    /// Creates an empty filesystem with the minimum viable objects (including a root parent and
    /// root store but no further child stores).
    pub async fn init_empty(&self, filesystem: Arc<dyn Filesystem>) -> Result<(), Error> {
        // The following constants are only used at format time. When mounting, the recorded values
        // in the superblock should be used.  The root parent store does not have a parent, but
        // needs an object ID to be registered with ObjectManager, so it cannot collide (i.e. have
        // the same object ID) with any objects in the root store that use the journal to track
        // mutations.
        const INIT_ROOT_PARENT_STORE_OBJECT_ID: u64 = 3;
        const INIT_ROOT_STORE_OBJECT_ID: u64 = 4;
        const INIT_ALLOCATOR_OBJECT_ID: u64 = 5;

        info!(device_size = filesystem.device().size(), "Formatting");

        let checkpoint = JournalCheckpoint {
            version: LATEST_VERSION,
            ..self.inner.lock().unwrap().writer.journal_file_checkpoint()
        };

        let root_parent =
            ObjectStore::new_empty(None, INIT_ROOT_PARENT_STORE_OBJECT_ID, filesystem.clone());
        self.objects.set_root_parent_store(root_parent.clone());

        let allocator =
            Arc::new(SimpleAllocator::new(filesystem.clone(), INIT_ALLOCATOR_OBJECT_ID));
        self.objects.set_allocator(allocator.clone());
        self.objects.init_metadata_reservation()?;

        let journal_handle;
        let super_block_a_handle;
        let super_block_b_handle;
        let root_store;
        let mut transaction = filesystem
            .clone()
            .new_transaction(&[], Options { skip_journal_checks: true, ..Default::default() })
            .await?;
        root_store = root_parent
            .new_child_store(
                &mut transaction,
                NewChildStoreOptions { object_id: INIT_ROOT_STORE_OBJECT_ID, ..Default::default() },
            )
            .await
            .context("new_child_store")?;
        self.objects.set_root_store(root_store.clone());

        allocator.create(&mut transaction).await?;

        // Create the super-block objects...
        super_block_a_handle = ObjectStore::create_object_with_id(
            &root_store,
            &mut transaction,
            SuperBlockInstance::A.object_id(),
            HandleOptions::default(),
            None,
        )
        .await
        .context("create super block")?;
        super_block_a_handle
            .extend(&mut transaction, SuperBlockInstance::A.first_extent())
            .await
            .context("extend super block")?;
        super_block_b_handle = ObjectStore::create_object_with_id(
            &root_store,
            &mut transaction,
            SuperBlockInstance::B.object_id(),
            HandleOptions::default(),
            None,
        )
        .await
        .context("create super block")?;
        super_block_b_handle
            .extend(&mut transaction, SuperBlockInstance::B.first_extent())
            .await
            .context("extend super block")?;

        // the journal object...
        journal_handle = ObjectStore::create_object(
            &root_parent,
            &mut transaction,
            journal_handle_options(),
            None,
        )
        .await
        .context("create journal")?;
        journal_handle
            .preallocate_range(&mut transaction, 0..self.chunk_size())
            .await
            .context("preallocate journal")?;

        // Write the root store object info.
        root_store.create(&mut transaction).await?;

        // The root parent graveyard.
        root_parent
            .set_graveyard_directory_object_id(Graveyard::create(&mut transaction, &root_parent));

        transaction.commit().await?;

        self.inner.lock().unwrap().super_block = SuperBlock::new(
            root_parent.store_object_id(),
            root_parent.graveyard_directory_object_id(),
            root_store.store_object_id(),
            allocator.object_id(),
            journal_handle.object_id(),
            checkpoint,
            /* earliest_version: */ LATEST_VERSION,
        );

        // Initialize the journal writer.
        let _ = self.handle.set(journal_handle);
        self.write_super_block().await?;
        SuperBlock::shred(super_block_b_handle).await
    }

    /// Commits a transaction.  This is not thread safe; the caller must take appropriate locks.
    pub async fn commit(&self, transaction: &mut Transaction<'_>) -> Result<u64, Error> {
        if transaction.is_empty() {
            return Ok(self.inner.lock().unwrap().writer.journal_file_checkpoint().file_offset);
        }

        self.pre_commit().await?;
        Ok(debug_assert_not_too_long!(self.write_and_apply_mutations(transaction)))
    }

    // Before we commit, we might need to extend the journal or write pending records to the
    // journal.
    async fn pre_commit(&self) -> Result<(), Error> {
        let handle;

        let (size, zero_offset) = {
            let mut inner = self.inner.lock().unwrap();

            // If this is the first write after a RESET, we need to output version first.
            if std::mem::take(&mut inner.output_reset_version) {
                LATEST_VERSION.serialize_into(&mut inner.writer)?;
            }

            if let Some(discard_offset) = inner.discard_offset {
                JournalRecord::Discard(discard_offset).serialize_into(&mut inner.writer)?;
                inner.discard_offset = None;
            }

            if inner.needs_did_flush_device {
                let offset = inner.device_flushed_offset;
                JournalRecord::DidFlushDevice(offset).serialize_into(&mut inner.writer)?;
                inner.needs_did_flush_device = false;
            }

            handle = match self.handle.get() {
                None => return Ok(()),
                Some(x) => x,
            };

            let file_offset = inner.writer.journal_file_checkpoint().file_offset;

            let size = handle.get_size();
            let size = if file_offset + self.chunk_size() > size { Some(size) } else { None };

            if size.is_none() && inner.zero_offset.is_none() {
                return Ok(());
            }

            (size, inner.zero_offset)
        };

        let mut transaction = handle
            .new_transaction_with_options(Options {
                skip_journal_checks: true,
                borrow_metadata_space: true,
                allocator_reservation: Some(self.objects.metadata_reservation()),
                ..Default::default()
            })
            .await?;
        if let Some(size) = size {
            handle.preallocate_range(&mut transaction, size..size + self.chunk_size()).await?;
        }
        if let Some(zero_offset) = zero_offset {
            handle.zero(&mut transaction, 0..zero_offset).await?;
        }

        // We can't use regular transaction commit, because that can cause re-entrancy issues, so
        // instead we just apply the transaction directly here.
        self.write_and_apply_mutations(&mut transaction).await;

        let mut inner = self.inner.lock().unwrap();

        // Make sure the transaction to extend the journal made it to the journal within the old
        // size, since otherwise, it won't be possible to replay.
        if let Some(size) = size {
            assert!(inner.writer.journal_file_checkpoint().file_offset < size);
        }

        if inner.zero_offset == zero_offset {
            inner.zero_offset = None;
        }

        Ok(())
    }

    // Determines whether a mutation at the given checkpoint should be applied.  During replay, not
    // all records should be applied because the object store or allocator might already contain the
    // mutation.  After replay, that obviously isn't the case and we want to apply all mutations.
    fn should_apply(&self, object_id: u64, journal_file_checkpoint: &JournalCheckpoint) -> bool {
        let super_block = &self.inner.lock().unwrap().super_block;
        let offset = super_block
            .journal_file_offsets
            .get(&object_id)
            .cloned()
            .unwrap_or(super_block.super_block_journal_file_offset);
        journal_file_checkpoint.file_offset >= offset
    }

    async fn write_super_block(&self) -> Result<(), Error> {
        let root_parent_store = self.objects.root_parent_store();

        // We need to flush previous writes to the device since the new super-block we are writing
        // relies on written data being observable, and we also need to lock the root parent store
        // so that no new entries are written to it whilst we are writing the super-block, and for
        // that we use the write lock.
        let _write_guard;
        let (checkpoint, borrowed) = {
            let _sync_guard = debug_assert_not_too_long!(self.sync_mutex.lock());
            _write_guard = debug_assert_not_too_long!(self.writer_mutex.lock());
            let result = self.pad_to_block()?;
            self.flush_device(result.0.file_offset).await?;
            result
        };

        let mut new_super_block = self.inner.lock().unwrap().super_block.clone();

        let old_super_block_offset = new_super_block.journal_checkpoint.file_offset;

        let (journal_file_offsets, min_checkpoint) = self.objects.journal_file_offsets();

        new_super_block.generation =
            new_super_block.generation.checked_add(1).ok_or(FxfsError::Inconsistent)?;
        new_super_block.super_block_journal_file_offset = checkpoint.file_offset;
        new_super_block.journal_checkpoint = min_checkpoint.unwrap_or(checkpoint);
        new_super_block.journal_checkpoint.version = LATEST_VERSION;
        new_super_block.journal_file_offsets = journal_file_offsets;
        new_super_block.borrowed_metadata_space = borrowed;

        self.super_block_manager.save(&new_super_block, root_parent_store).await?;

        {
            let mut inner = self.inner.lock().unwrap();
            inner.super_block = new_super_block;
            inner.zero_offset = Some(round_down(old_super_block_offset, BLOCK_SIZE));
        }

        Ok(())
    }

    /// Flushes any buffered journal data to the device.  Note that this does not flush the device
    /// unless the flush_device option is set, in which case data should have been persisted to
    /// lower layers.  If a precondition is supplied, it is evaluated and the sync will be skipped
    /// if it returns false.  This allows callers to check a condition whilst a lock is held.  If a
    /// sync is performed, this function returns the checkpoint that was flushed and the amount of
    /// borrowed metadata space at the point it was flushed.
    pub async fn sync(
        &self,
        options: SyncOptions<'_>,
    ) -> Result<Option<(JournalCheckpoint, u64)>, Error> {
        let _guard = debug_assert_not_too_long!(self.sync_mutex.lock());

        let (checkpoint, borrowed) = {
            if let Some(precondition) = options.precondition {
                if !precondition() {
                    return Ok(None);
                }
            }

            // This guard is required so that we don't insert an EndBlock record in the middle of a
            // transaction.
            let _guard = debug_assert_not_too_long!(self.writer_mutex.lock());

            self.pad_to_block()?
        };

        if options.flush_device {
            self.flush_device(checkpoint.file_offset).await?;
        }

        Ok(Some((checkpoint, borrowed)))
    }

    // Returns the checkpoint as it was prior to padding.  This is done because the super block
    // needs to record where the last transaction ends and it's the next transaction that pays the
    // price of the padding.
    fn pad_to_block(&self) -> Result<(JournalCheckpoint, u64), Error> {
        let mut inner = self.inner.lock().unwrap();
        let checkpoint = inner.writer.journal_file_checkpoint();
        if checkpoint.file_offset % BLOCK_SIZE != 0 {
            JournalRecord::EndBlock.serialize_into(&mut inner.writer)?;
            inner.writer.pad_to_block()?;
            if let Some(waker) = inner.flush_waker.take() {
                waker.wake();
            }
        }
        Ok((checkpoint, self.objects.borrowed_metadata_space()))
    }

    async fn flush_device(&self, checkpoint_offset: u64) -> Result<(), Error> {
        debug_assert_not_too_long!(poll_fn(|ctx| {
            let mut inner = self.inner.lock().unwrap();
            if inner.flushed_offset >= checkpoint_offset {
                Poll::Ready(Ok(()))
            } else if inner.terminate {
                Poll::Ready(Err(FxfsError::JournalFlushError))
            } else {
                inner.sync_waker = Some(ctx.waker().clone());
                Poll::Pending
            }
        }))?;

        let needs_flush = self.inner.lock().unwrap().device_flushed_offset < checkpoint_offset;
        if needs_flush {
            let trace = self.trace.load(Ordering::Relaxed);
            if trace {
                info!("J: start flush device");
            }
            self.handle.get().unwrap().flush_device().await?;
            if trace {
                info!("J: end flush device");
            }

            // We need to write a DidFlushDevice record at some point, but if we are in the
            // process of shutting down the filesystem, we want to leave the journal clean to
            // avoid there being log messages complaining about unwritten journal data, so we
            // queue it up so that the next transaction will trigger this record to be written.
            // If we are shutting down, that will never happen but since the DidFlushDevice
            // message is purely advisory (it reduces the number of checksums we have to verify
            // during replay), it doesn't matter if it isn't written.
            {
                let mut inner = self.inner.lock().unwrap();
                inner.device_flushed_offset = checkpoint_offset;
                inner.needs_did_flush_device = true;
            }

            // Tell the allocator that we flushed the device so that it can now start using
            // space that was deallocated.
            self.objects.allocator().did_flush_device(checkpoint_offset).await;
            if trace {
                info!("J: did flush device");
            }
        }

        Ok(())
    }

    /// Returns a copy of the super-block.
    pub fn super_block(&self) -> SuperBlock {
        self.inner.lock().unwrap().super_block.clone()
    }

    /// Waits for there to be sufficient space in the journal.
    pub async fn check_journal_space(&self) -> Result<(), Error> {
        loop {
            let _ = debug_assert_not_too_long!({
                let mut inner = self.inner.lock().unwrap();
                if inner.terminate {
                    // If the flush error is set, this will never make progress, since we can't
                    // extend the journal any more.
                    break Err(anyhow!(FxfsError::JournalFlushError).context("Journal closed"));
                }
                if self.objects.last_end_offset() - inner.super_block.journal_checkpoint.file_offset
                    < inner.reclaim_size
                {
                    break Ok(());
                }
                if inner.disable_compactions {
                    break Err(anyhow!(FxfsError::JournalFlushError).context("Journal closed"));
                }
                let event = inner.reclaim_event.get_or_insert_with(|| Event::new());
                event.wait_or_dropped()
            });
        }
    }

    fn block_size(&self) -> u64 {
        BLOCK_SIZE
    }

    fn chunk_size(&self) -> u64 {
        CHUNK_SIZE
    }

    async fn write_and_apply_mutations(&self, transaction: &mut Transaction<'_>) -> u64 {
        let checkpoint_before;
        let checkpoint_after;
        {
            let _guard = debug_assert_not_too_long!(self.writer_mutex.lock());
            checkpoint_before = {
                let mut inner = self.inner.lock().unwrap();
                let checkpoint = inner.writer.journal_file_checkpoint();
                for TxnMutation { object_id, mutation, .. } in &transaction.mutations {
                    self.objects.write_mutation(
                        *object_id,
                        mutation,
                        Writer(*object_id, &mut inner.writer),
                    );
                }
                checkpoint
            };
            // The call here isn't drop-safe.  If the future gets dropped, it will leave things in a
            // bad state and subsequent threads might try and commit another transaction which has
            // the potential to fire assertions.  With that said, this should only occur if there
            // has been another panic, since we take care not to drop futures at other other times.
            let maybe_mutation =
                self.objects.apply_transaction(transaction, &checkpoint_before).await.expect(
                    "apply_transaction should not fail in live mode;\
                     filesystem will be in an inconsistent state",
                );
            checkpoint_after = {
                let mut inner = self.inner.lock().unwrap();
                if let Some(mutation) = maybe_mutation {
                    inner.writer.write_record(&JournalRecord::Mutation { object_id: 0, mutation });
                }
                inner.writer.write_record(&JournalRecord::Commit);

                inner.writer.journal_file_checkpoint()
            };
        }
        self.objects.did_commit_transaction(
            transaction,
            &checkpoint_before,
            checkpoint_after.file_offset,
        );

        if let Some(waker) = self.inner.lock().unwrap().flush_waker.take() {
            waker.wake();
        }

        checkpoint_before.file_offset
    }

    /// This task will flush journal data to the device when there is data that needs flushing, and
    /// trigger compactions when short of journal space.  It will return after the terminate method
    /// has been called, or an error is encountered with either flushing or compaction.
    pub async fn flush_task(&self) {
        let mut flush_fut = None;
        let mut compact_fut = None;
        let mut flush_error = false;
        poll_fn(|ctx| loop {
            {
                let mut inner = self.inner.lock().unwrap();
                if flush_fut.is_none() && !flush_error {
                    if let Some(handle) = self.handle.get() {
                        if let Some((offset, buf)) = inner.writer.take_buffer(handle) {
                            flush_fut = Some(self.flush(offset, buf).boxed());
                        }
                    }
                }
                if inner.terminate && flush_fut.is_none() && compact_fut.is_none() {
                    return Poll::Ready(());
                }
                // The / 2 is here because after compacting, we cannot reclaim the space until the
                // _next_ time we flush the device since the super-block is not guaranteed to
                // persist until then.
                if compact_fut.is_none()
                    && !inner.terminate
                    && !inner.disable_compactions
                    && self.objects.last_end_offset()
                        - inner.super_block.journal_checkpoint.file_offset
                        > inner.reclaim_size / 2
                {
                    compact_fut = Some(self.compact().boxed());
                    inner.compaction_running = true;
                }
                inner.flush_waker = Some(ctx.waker().clone());
            }
            let mut pending = true;
            if let Some(fut) = flush_fut.as_mut() {
                if let Poll::Ready(result) = fut.poll_unpin(ctx) {
                    if let Err(e) = result {
                        info!(error = e.as_value(), "Flush error");
                        self.inner.lock().unwrap().terminate();
                        flush_error = true;
                    }
                    flush_fut = None;
                    pending = false;
                }
            }
            if let Some(fut) = compact_fut.as_mut() {
                if let Poll::Ready(result) = fut.poll_unpin(ctx) {
                    let mut inner = self.inner.lock().unwrap();
                    if let Err(e) = result {
                        info!(error = e.as_value(), "Compaction error");
                        inner.terminate();
                    }
                    compact_fut = None;
                    inner.compaction_running = false;
                    inner.reclaim_event = None;
                    pending = false;
                }
            }
            if pending {
                return Poll::Pending;
            }
        })
        .await;
    }

    async fn flush(&self, offset: u64, mut buf: Buffer<'_>) -> Result<(), Error> {
        let len = buf.len() as u64;
        self.handle.get().unwrap().overwrite(offset, buf.as_mut()).await?;
        let mut inner = self.inner.lock().unwrap();
        if let Some(waker) = inner.sync_waker.take() {
            waker.wake();
        }
        inner.flushed_offset = offset + len;
        Ok(())
    }

    async fn compact(&self) -> Result<(), Error> {
        let trace = self.trace.load(Ordering::Relaxed);
        debug!("Compaction starting");
        if trace {
            info!("J: start compaction");
        }
        trace_duration!("Journal::compact");
        let earliest_version = self.objects.flush().await?;
        self.inner.lock().unwrap().super_block.earliest_version = earliest_version;
        self.write_super_block().await?;
        if trace {
            info!("J: end compaction");
        }
        debug!("Compaction finished");
        Ok(())
    }

    pub async fn stop_compactions(&self) {
        loop {
            let _ = debug_assert_not_too_long!({
                let mut inner = self.inner.lock().unwrap();
                inner.disable_compactions = true;
                if !inner.compaction_running {
                    return;
                }
                let event = inner.reclaim_event.get_or_insert_with(|| Event::new());
                event.wait_or_dropped()
            });
        }
    }

    /// Terminate all journal activity.
    pub fn terminate(&self) {
        self.inner.lock().unwrap().terminate();
    }
}

/// Wrapper to allow records to be written to the journal.
pub struct Writer<'a>(u64, &'a mut JournalWriter);

impl Writer<'_> {
    pub fn write(&mut self, mutation: Mutation) {
        self.1.write_record(&JournalRecord::Mutation { object_id: self.0, mutation });
    }
}

#[cfg(test)]
mod tests {
    use {
        crate::{
            filesystem::{Filesystem, FxFilesystem, SyncOptions},
            fsck::fsck,
            object_handle::{ObjectHandle, ReadObjectHandle, WriteObjectHandle},
            object_store::{
                directory::Directory,
                transaction::{Options, TransactionHandler},
                HandleOptions, ObjectStore,
            },
        },
        fuchsia_async as fasync,
        storage_device::{fake_device::FakeDevice, DeviceHolder},
    };

    const TEST_DEVICE_BLOCK_SIZE: u32 = 512;

    #[fasync::run_singlethreaded(test)]
    async fn test_replay() {
        const TEST_DATA: &[u8] = b"hello";

        let device = DeviceHolder::new(FakeDevice::new(8192, TEST_DEVICE_BLOCK_SIZE));

        let fs = FxFilesystem::new_empty(device).await.expect("new_empty failed");

        let object_id = {
            let root_store = fs.root_store();
            let root_directory =
                Directory::open(&root_store, root_store.root_directory_object_id())
                    .await
                    .expect("open failed");
            let mut transaction = fs
                .clone()
                .new_transaction(&[], Options::default())
                .await
                .expect("new_transaction failed");
            let handle = root_directory
                .create_child_file(&mut transaction, "test")
                .await
                .expect("create_child_file failed");

            transaction.commit().await.expect("commit failed");
            let mut buf = handle.allocate_buffer(TEST_DATA.len());
            buf.as_mut_slice().copy_from_slice(TEST_DATA);
            handle.write_or_append(Some(0), buf.as_ref()).await.expect("write failed");
            // As this is the first sync, this will actually trigger a new super-block, but normally
            // this would not be the case.
            fs.sync(SyncOptions::default()).await.expect("sync failed");
            handle.object_id()
        };

        {
            fs.close().await.expect("Close failed");
            let device = fs.take_device().await;
            device.reopen(false);
            let fs = FxFilesystem::open(device).await.expect("open failed");
            let handle = ObjectStore::open_object(
                &fs.root_store(),
                object_id,
                HandleOptions::default(),
                None,
            )
            .await
            .expect("open_object failed");
            let mut buf = handle.allocate_buffer(TEST_DEVICE_BLOCK_SIZE as usize);
            assert_eq!(handle.read(0, buf.as_mut()).await.expect("read failed"), TEST_DATA.len());
            assert_eq!(&buf.as_slice()[..TEST_DATA.len()], TEST_DATA);
            fsck(fs.clone()).await.expect("fsck failed");
            fs.close().await.expect("Close failed");
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_reset() {
        const TEST_DATA: &[u8] = b"hello";

        let device = DeviceHolder::new(FakeDevice::new(32768, TEST_DEVICE_BLOCK_SIZE));

        let mut object_ids = Vec::new();

        let fs = FxFilesystem::new_empty(device).await.expect("new_empty failed");
        {
            let root_store = fs.root_store();
            let root_directory =
                Directory::open(&root_store, root_store.root_directory_object_id())
                    .await
                    .expect("open failed");
            let mut transaction = fs
                .clone()
                .new_transaction(&[], Options::default())
                .await
                .expect("new_transaction failed");
            let handle = root_directory
                .create_child_file(&mut transaction, "test")
                .await
                .expect("create_child_file failed");
            transaction.commit().await.expect("commit failed");
            let mut buf = handle.allocate_buffer(TEST_DATA.len());
            buf.as_mut_slice().copy_from_slice(TEST_DATA);
            handle.write_or_append(Some(0), buf.as_ref()).await.expect("write failed");
            fs.sync(SyncOptions::default()).await.expect("sync failed");
            object_ids.push(handle.object_id());

            // Create a lot of objects but don't sync at the end. This should leave the filesystem
            // with a half finished transaction that cannot be replayed.
            for i in 0..1000 {
                let mut transaction = fs
                    .clone()
                    .new_transaction(&[], Options::default())
                    .await
                    .expect("new_transaction failed");
                let handle = root_directory
                    .create_child_file(&mut transaction, &format!("{}", i))
                    .await
                    .expect("create_child_file failed");
                transaction.commit().await.expect("commit failed");
                let mut buf = handle.allocate_buffer(TEST_DATA.len());
                buf.as_mut_slice().copy_from_slice(TEST_DATA);
                handle.write_or_append(Some(0), buf.as_ref()).await.expect("write failed");
                object_ids.push(handle.object_id());
            }
        }
        fs.close().await.expect("fs close failed");
        let device = fs.take_device().await;
        device.reopen(false);
        let fs = FxFilesystem::open(device).await.expect("open failed");
        fsck(fs.clone()).await.expect("fsck failed");
        {
            let root_store = fs.root_store();
            // Check the first two objects which should exist.
            for &object_id in &object_ids[0..1] {
                let handle = ObjectStore::open_object(
                    &root_store,
                    object_id,
                    HandleOptions::default(),
                    None,
                )
                .await
                .expect("open_object failed");
                let mut buf = handle.allocate_buffer(TEST_DEVICE_BLOCK_SIZE as usize);
                assert_eq!(
                    handle.read(0, buf.as_mut()).await.expect("read failed"),
                    TEST_DATA.len()
                );
                assert_eq!(&buf.as_slice()[..TEST_DATA.len()], TEST_DATA);
            }

            // Write one more object and sync.
            let root_directory =
                Directory::open(&root_store, root_store.root_directory_object_id())
                    .await
                    .expect("open failed");
            let mut transaction = fs
                .clone()
                .new_transaction(&[], Options::default())
                .await
                .expect("new_transaction failed");
            let handle = root_directory
                .create_child_file(&mut transaction, "test2")
                .await
                .expect("create_child_file failed");
            transaction.commit().await.expect("commit failed");
            let mut buf = handle.allocate_buffer(TEST_DATA.len());
            buf.as_mut_slice().copy_from_slice(TEST_DATA);
            handle.write_or_append(Some(0), buf.as_ref()).await.expect("write failed");
            fs.sync(SyncOptions::default()).await.expect("sync failed");
            object_ids.push(handle.object_id());
        }

        fs.close().await.expect("close failed");
        let device = fs.take_device().await;
        device.reopen(false);
        let fs = FxFilesystem::open(device).await.expect("open failed");
        {
            fsck(fs.clone()).await.expect("fsck failed");

            // Check the first two and the last objects.
            for &object_id in object_ids[0..1].iter().chain(object_ids.last().cloned().iter()) {
                let handle = ObjectStore::open_object(
                    &fs.root_store(),
                    object_id,
                    HandleOptions::default(),
                    None,
                )
                .await
                .expect(&format!("open_object failed (object_id: {})", object_id));
                let mut buf = handle.allocate_buffer(TEST_DEVICE_BLOCK_SIZE as usize);
                assert_eq!(
                    handle.read(0, buf.as_mut()).await.expect("read failed"),
                    TEST_DATA.len()
                );
                assert_eq!(&buf.as_slice()[..TEST_DATA.len()], TEST_DATA);
            }
        }
        fs.close().await.expect("close failed");
    }
}

#[cfg(fuzz)]
mod fuzz {
    use fuzz::fuzz;

    #[fuzz]
    fn fuzz_journal_bytes(input: Vec<u8>) {
        use {
            crate::filesystem::FxFilesystem,
            fuchsia_async as fasync,
            std::io::Write,
            storage_device::{fake_device::FakeDevice, DeviceHolder},
        };

        fasync::SendExecutor::new(4).unwrap().run(async move {
            let device = DeviceHolder::new(FakeDevice::new(32768, 512));
            let fs = FxFilesystem::new_empty(device).await.expect("new_empty failed");
            fs.journal().inner.lock().unwrap().writer.write_all(&input).expect("write failed");
            fs.close().await.expect("close failed");
            let device = fs.take_device().await;
            device.reopen(false);
            if let Ok(fs) = FxFilesystem::open(device).await {
                fs.close().await.expect("close failed");
            }
        });
    }

    #[fuzz]
    fn fuzz_journal(input: Vec<super::JournalRecord>) {
        use {
            crate::filesystem::FxFilesystem,
            fuchsia_async as fasync,
            storage_device::{fake_device::FakeDevice, DeviceHolder},
        };

        fasync::SendExecutor::new(4).unwrap().run(async move {
            let device = DeviceHolder::new(FakeDevice::new(32768, 512));
            let fs = FxFilesystem::new_empty(device).await.expect("new_empty failed");
            {
                let mut inner = fs.journal().inner.lock().unwrap();
                for record in &input {
                    inner.writer.write_record(record);
                }
            }
            fs.close().await.expect("close failed");
            let device = fs.take_device().await;
            device.reopen(false);
            if let Ok(fs) = FxFilesystem::open(device).await {
                fs.close().await.expect("close failed");
            }
        });
    }
}
