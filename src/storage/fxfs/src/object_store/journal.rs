// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The journal is implemented as an ever extending file which contains variable length records that
// describe mutations to be applied to various objects.  The journal file consists of blocks, with a
// checksum at the end of each block, but otherwise it can be considered a continuous stream.  The
// checksum is seeded with the checksum from the previous block.  To free space in the journal,
// records are replaced with sparse extents when it is known they are no longer needed to mount.  At
// mount time, the journal is replayed: the mutations are applied into memory.  Eventually, a
// checksum failure will indicate no more records exist to be replayed, at which point the mount can
// continue and the journal will be extended from that point with further mutations as required.
//
// The super-block contains the starting offset and checksum for the journal file and sufficient
// information to locate the initial extents for the journal.  The super-block is written using the
// same per-block checksum that is used for the journal file.

pub mod checksum_list;
mod handle;
mod reader;
pub mod super_block;
mod writer;

use {
    crate::{
        debug_assert_not_too_long,
        errors::FxfsError,
        lsm_tree::LSMTree,
        object_handle::ObjectHandle,
        object_store::{
            allocator::{Allocator, SimpleAllocator},
            constants::{SUPER_BLOCK_A_OBJECT_ID, SUPER_BLOCK_B_OBJECT_ID},
            directory::Directory,
            filesystem::{Filesystem, Mutations, SyncOptions},
            graveyard::Graveyard,
            journal::{
                checksum_list::ChecksumList,
                handle::Handle,
                reader::{JournalReader, ReadResult},
                super_block::{SuperBlock, SuperBlockCopy},
                writer::JournalWriter,
            },
            merge::{self},
            object_manager::ObjectManager,
            record::{ExtentKey, ObjectKey, DEFAULT_DATA_ATTRIBUTE_ID},
            round_down,
            transaction::{
                AssocObj, Mutation, ObjectStoreMutation, Options, Transaction, TxnMutation,
            },
            HandleOptions, ObjectStore, StoreObjectHandle,
        },
        trace_duration,
    },
    anyhow::{anyhow, bail, Context, Error},
    async_utils::event::Event,
    bincode::serialize_into,
    byteorder::{ByteOrder, LittleEndian},
    futures::{self},
    once_cell::sync::OnceCell,
    rand::Rng,
    serde::{Deserialize, Serialize},
    std::{
        clone::Clone,
        ops::Bound,
        sync::{Arc, Mutex},
        vec::Vec,
    },
};

// The journal file is written to in blocks of this size.
const BLOCK_SIZE: u64 = 8192;

// The journal file is extended by this amount when necessary.
const CHUNK_SIZE: u64 = 131_072;

// In the steady state, the journal should fluctuate between being approximately half of this number
// and this number.  New super-blocks will be written every time about half of this amount is
// written to the journal.
pub const RECLAIM_SIZE: u64 = 262_144;

// Temporary space that should be reserved for the journal.  For example: space that is currently
// used in the journal file but cannot be deallocated yet because we are flushing.
pub const RESERVED_SPACE: u64 = 1_048_576;

// After replaying the journal, it's possible that the stream doesn't end cleanly, in which case the
// next journal block needs to indicate this.  This is done by pretending the previous block's
// checksum is xored with this value, and using that as the seed for the next journal block.
const RESET_XOR: u64 = 0xffffffffffffffff;

type Checksum = u64;

// To keep track of offsets within a journal file, we need both the file offset and the check-sum of
// the preceding block, since the check-sum of the preceding block is an input to the check-sum of
// every block.
#[derive(Clone, Debug, Default, Deserialize, Eq, PartialEq, Serialize)]
pub struct JournalCheckpoint {
    pub file_offset: u64,

    // Starting check-sum for block that contains file_offset i.e. the checksum for the previous
    // block.
    pub checksum: Checksum,
}

impl JournalCheckpoint {
    fn new(file_offset: u64, checksum: Checksum) -> JournalCheckpoint {
        JournalCheckpoint { file_offset, checksum }
    }
}

// All journal blocks are covered by a fletcher64 checksum as the last 8 bytes in a block.
pub fn fletcher64(buf: &[u8], previous: u64) -> u64 {
    assert!(buf.len() % 4 == 0);
    let mut lo = previous as u32;
    let mut hi = (previous >> 32) as u32;
    for chunk in buf.chunks(4) {
        lo = lo.wrapping_add(LittleEndian::read_u32(chunk));
        hi = hi.wrapping_add(lo);
    }
    (hi as u64) << 32 | lo as u64
}

#[derive(Clone, Debug, Serialize, Deserialize)]
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
    // TODO(csuter): I think with a bit of refactoring, this lock doesn't need to be async.
    writer: futures::lock::Mutex<JournalWriter>,
    handle: OnceCell<StoreObjectHandle<ObjectStore>>,
    inner: Mutex<Inner>,
    extension_mutex: futures::lock::Mutex<()>,
    sync_mutex: futures::lock::Mutex<()>,
}

struct Inner {
    super_block: SuperBlock,
    super_block_to_write: SuperBlockCopy,

    // This event is used when we are waiting for a compaction to free up journal space.
    reclaim_event: Option<Event>,

    // The offset that we can zero the journal up to now that it is no longer needed.
    zero_offset: Option<u64>,
}

impl Journal {
    pub fn new(objects: Arc<ObjectManager>) -> Journal {
        let starting_checksum = rand::thread_rng().gen();
        Journal {
            objects: objects,
            writer: futures::lock::Mutex::new(JournalWriter::new(
                BLOCK_SIZE as usize,
                starting_checksum,
            )),
            handle: OnceCell::new(),
            inner: Mutex::new(Inner {
                super_block: SuperBlock::default(),
                super_block_to_write: SuperBlockCopy::A,
                reclaim_event: None,
                zero_offset: None,
            }),
            extension_mutex: futures::lock::Mutex::new(()),
            sync_mutex: futures::lock::Mutex::new(()),
        }
    }

    pub fn journal_file_offset(&self) -> u64 {
        self.inner.lock().unwrap().super_block.super_block_journal_file_offset
    }

    async fn load_superblock(
        &self,
        filesystem: Arc<dyn Filesystem>,
        target_super_block: SuperBlockCopy,
    ) -> Result<(SuperBlock, SuperBlockCopy, Arc<ObjectStore>), Error> {
        let device = filesystem.device();
        let (super_block, mut reader) = SuperBlock::read(device, target_super_block)
            .await
            .context("Failed to read superblocks")?;

        // Sanity-check the super-block before we attempt to use it. Further validation has to be
        // done after we replay the items in |reader|, since they could involve super-block
        // mutations.
        if super_block.magic != super_block::SUPER_BLOCK_MAGIC {
            return Err(anyhow!(FxfsError::Inconsistent))
                .context(format!("Invalid magic, super_block: {:?}", super_block));
        } else if super_block.major_version != super_block::SUPER_BLOCK_MAJOR_VERSION {
            return Err(anyhow!(FxfsError::InvalidVersion)).context(format!(
                "Invalid version (has {}, want {})",
                super_block.major_version,
                super_block::SUPER_BLOCK_MAJOR_VERSION
            ));
        }

        let root_parent = ObjectStore::new_empty(
            None,
            super_block.root_parent_store_object_id,
            filesystem.clone(),
        );

        while let Some(item) = reader.next_item().await? {
            let mutation = Mutation::insert_object(item.key, item.value);
            root_parent.apply_mutation(mutation, None, 0, AssocObj::None).await;
        }

        // TODO(jfsulliv): Upgrade minor revision as needed.

        Ok((super_block, target_super_block, root_parent))
    }

    /// Reads the latest super-block, and then replays journaled records.
    pub async fn replay(&self, filesystem: Arc<dyn Filesystem>) -> Result<(), Error> {
        trace_duration!("Journal::replay");
        let (super_block, current_super_block, root_parent) = match futures::join!(
            self.load_superblock(filesystem.clone(), SuperBlockCopy::A),
            self.load_superblock(filesystem.clone(), SuperBlockCopy::B)
        ) {
            (Err(e1), Err(e2)) => {
                bail!("Failed to load both superblocks due to {:?}\nand\n{:?}", e1, e2)
            }
            (Ok(result), Err(_)) => result,
            (Err(_), Ok(result)) => result,
            (Ok(result1), Ok(result2)) => {
                // Break the tie by taking the super-block with the greatest generation.
                if result2.0.generation > result1.0.generation {
                    result2
                } else {
                    result1
                }
            }
        };

        log::info!(
            "replaying journal, superblock: {:?} (copy {:?})",
            super_block,
            current_super_block
        );

        self.objects.set_root_parent_store(root_parent.clone());
        let allocator = Arc::new(SimpleAllocator::new(
            filesystem.clone(),
            super_block.allocator_object_id,
            false,
        ));
        self.objects.set_allocator(allocator.clone());
        self.objects.set_borrowed_metadata_space(super_block.borrowed_metadata_space);
        self.objects.set_last_end_offset(super_block.super_block_journal_file_offset);
        {
            let mut inner = self.inner.lock().unwrap();
            inner.super_block = super_block.clone();
            inner.super_block_to_write = current_super_block.next();
        }
        let root_store = ObjectStore::new(
            Some(root_parent.clone()),
            super_block.root_store_object_id,
            filesystem.clone(),
            None,
            LSMTree::new(merge::merge),
        );
        self.objects.set_root_store(root_store);

        let device = filesystem.device();

        let mut handle;
        {
            let root_parent_layer = root_parent.tree().mutable_layer();
            let mut iter = root_parent_layer
                .seek(Bound::Included(&ObjectKey::with_extent_key(
                    super_block.journal_object_id,
                    DEFAULT_DATA_ATTRIBUTE_ID,
                    ExtentKey::search_key_from_offset(round_down(
                        super_block.journal_checkpoint.file_offset,
                        BLOCK_SIZE,
                    )),
                )))
                .await?;
            handle = Handle::new(super_block.journal_object_id, device.clone());
            while let Some(item) = iter.get() {
                if !handle.try_push_extent_from_object_item(item)? {
                    break;
                }
                iter.advance().await?;
            }
        }
        let mut reader =
            JournalReader::new(handle, self.block_size(), &super_block.journal_checkpoint);
        let mut checksum_list = ChecksumList::new();
        let mut transactions = Vec::new();
        let mut current_transaction = None;
        let mut end_block = false;
        let mut flushed_offset = super_block.super_block_journal_file_offset;
        loop {
            let current_checkpoint = reader.journal_file_checkpoint();
            match reader.deserialize().await? {
                ReadResult::Reset => {
                    if current_transaction.is_some() {
                        current_transaction = None;
                        transactions.pop();
                    }
                }
                ReadResult::Some(record) => {
                    end_block = false;
                    match record {
                        JournalRecord::EndBlock => {
                            reader.skip_to_end_of_block();
                            end_block = true;
                        }
                        JournalRecord::Mutation { object_id, mutation } => {
                            let current_transaction = match current_transaction.as_mut() {
                                None => {
                                    transactions.push((current_checkpoint, Vec::new(), 0));
                                    current_transaction = transactions.last_mut();
                                    current_transaction.as_mut().unwrap()
                                }
                                Some(transaction) => transaction,
                            };
                            if !self
                                .objects
                                .validate_mutation(
                                    current_transaction.0.file_offset,
                                    object_id,
                                    &mutation,
                                    &mut checksum_list,
                                )
                                .await?
                            {
                                log::debug!("Stopping replay at bad mutation: {:?}", mutation);
                                break;
                            }
                            // If this mutation doesn't need to be applied, don't bother adding it
                            // to the transaction.
                            if self.should_apply(object_id, &current_transaction.0) {
                                current_transaction.1.push((object_id, mutation));
                            }
                        }
                        JournalRecord::Commit => {
                            if let Some((checkpoint, mutations, ref mut end_offset)) =
                                current_transaction.take()
                            {
                                for (object_id, mutation) in mutations {
                                    // Snoop the mutations for any that might apply to the journal
                                    // file so that we can pass them to the reader so that it can
                                    // read the journal file.
                                    if *object_id == super_block.root_parent_store_object_id
                                        && self.should_apply(*object_id, &checkpoint)
                                    {
                                        if let Mutation::ObjectStore(ObjectStoreMutation {
                                            item,
                                            ..
                                        }) = mutation
                                        {
                                            reader.handle().try_push_extent_from_object_item(
                                                item.as_item_ref(),
                                            )?;
                                        }
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
                        }
                        JournalRecord::DidFlushDevice(offset) => {
                            if offset > flushed_offset {
                                flushed_offset = offset;
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

        // Validate the checksums.
        let journal_offset = checksum_list
            .verify(device.as_ref(), flushed_offset, reader.journal_file_checkpoint().file_offset)
            .await?;

        // Apply the mutations.
        let mut last_checkpoint = if transactions.is_empty() {
            super_block.journal_checkpoint.clone()
        } else {
            'outer: loop {
                for (checkpoint, mutations, end_offset) in transactions {
                    if checkpoint.file_offset >= journal_offset {
                        break 'outer checkpoint;
                    }
                    self.objects.replay_mutations(mutations, checkpoint, end_offset).await;
                }
                break reader.journal_file_checkpoint();
            }
        };

        // Configure the journal writer so that we can continue.
        {
            if last_checkpoint.file_offset < super_block.super_block_journal_file_offset {
                return Err(anyhow!(FxfsError::Inconsistent).context(format!(
                    "journal replay cut short; journal finishes at {}, but super-block was \
                     written at {}",
                    last_checkpoint.file_offset, super_block.super_block_journal_file_offset
                )));
            }
            allocator.ensure_open().await?;
            let handle = ObjectStore::open_object(
                &root_parent,
                super_block.journal_object_id,
                journal_handle_options(),
            )
            .await?;
            let _ = self.handle.set(handle);
            let mut writer = self.writer.lock().await;
            // If the last entry wasn't an end_block, then we need to reset the stream.
            if !end_block {
                last_checkpoint.checksum ^= RESET_XOR;
            }
            let offset = last_checkpoint.file_offset;
            writer.seek_to_checkpoint(last_checkpoint);
            if offset < reader.journal_file_checkpoint().file_offset {
                // TODO(csuter): We need to make sure that this is tested.  If a corruption test
                // does not trigger this, we may have to add a targeted test.
                serialize_into(&mut *writer, &JournalRecord::Discard(offset))?;
            }
        }

        let root_store = self.objects.root_store();
        root_store.ensure_open().await?;
        self.objects.register_graveyard(
            Graveyard::open(&self.objects.root_store(), root_store.graveyard_directory_object_id())
                .await
                .context(format!(
                    "failed to open graveyard (object_id: {})",
                    root_store.graveyard_directory_object_id()
                ))?,
        );

        log::info!("replay done");
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

        let checkpoint = self.writer.lock().await.journal_file_checkpoint();

        let root_parent =
            ObjectStore::new_empty(None, INIT_ROOT_PARENT_STORE_OBJECT_ID, filesystem.clone());
        self.objects.set_root_parent_store(root_parent.clone());

        let allocator =
            Arc::new(SimpleAllocator::new(filesystem.clone(), INIT_ALLOCATOR_OBJECT_ID, true));
        self.objects.set_allocator(allocator.clone());

        let journal_handle;
        let super_block_a_handle;
        let super_block_b_handle;
        let root_store;
        let mut transaction = filesystem
            .new_transaction(&[], Options { skip_journal_checks: true, ..Default::default() })
            .await?;
        root_store = root_parent
            .create_child_store_with_id(&mut transaction, INIT_ROOT_STORE_OBJECT_ID)
            .await
            .context("create root store")?;
        self.objects.set_root_store(root_store.clone());

        // Create the super-block objects...
        super_block_a_handle = ObjectStore::create_object_with_id(
            &root_store,
            &mut transaction,
            SUPER_BLOCK_A_OBJECT_ID,
            HandleOptions::default(),
        )
        .await
        .context("create super block")?;
        super_block_a_handle
            .extend(&mut transaction, SuperBlockCopy::A.first_extent())
            .await
            .context("extend super block")?;
        super_block_b_handle = ObjectStore::create_object_with_id(
            &root_store,
            &mut transaction,
            SUPER_BLOCK_B_OBJECT_ID,
            HandleOptions::default(),
        )
        .await
        .context("create super block")?;
        super_block_b_handle
            .extend(&mut transaction, SuperBlockCopy::B.first_extent())
            .await
            .context("extend super block")?;

        // the journal object...
        journal_handle =
            ObjectStore::create_object(&root_parent, &mut transaction, journal_handle_options())
                .await
                .context("create journal")?;
        journal_handle
            .preallocate_range(&mut transaction, 0..self.chunk_size())
            .await
            .context("preallocate journal")?;

        // the root store's graveyard and root directory...
        let graveyard = Graveyard::create(&mut transaction, &root_store).await?;
        root_store.set_graveyard_directory_object_id(&mut transaction, graveyard.object_id());
        self.objects.register_graveyard(graveyard);

        let root_directory = Directory::create(&mut transaction, &root_store)
            .await
            .context("create root directory")?;
        root_store.set_root_directory_object_id(&mut transaction, root_directory.object_id());

        transaction.commit().await;

        // Cache the super-block.
        {
            let mut inner = self.inner.lock().unwrap();
            inner.super_block = SuperBlock::new(
                root_parent.store_object_id(),
                root_store.store_object_id(),
                allocator.object_id(),
                journal_handle.object_id(),
                checkpoint,
            );
            inner.super_block_to_write = SuperBlockCopy::A;
        }

        allocator.ensure_open().await?;

        // Initialize the journal writer.
        let _ = self.handle.set(journal_handle);

        self.write_super_block().await
    }

    /// Commits a transaction.
    pub async fn commit(&self, transaction: &mut Transaction<'_>) {
        if transaction.is_empty() {
            return;
        }
        // TODO(csuter): handle the case where we are unable to extend the journal file.
        if !transaction.skip_journal_extension {
            self.maybe_extend_journal_file().await.unwrap();
        }
        // TODO(csuter): writing to the journal here can be asynchronous.
        let mut writer = self.writer.lock().await;
        debug_assert_not_too_long!(self.write_and_apply_mutations(&mut *writer, transaction));

        if let Some(handle) = self.handle.get() {
            // TODO(jfsulliv): We should separate writing to the journal buffer from flushing the
            // journal buffer (i.e. consider doing this in a background task). Flushing here is
            // prone to deadlock, since |flush_buffer| itself creates a transaction which locks the
            // journal handle.
            // TODO(csuter): Add a test for the aforementioned deadlock condition.
            if let Err(e) = writer.flush_buffer(handle).await {
                // TODO(csuter): if writes to the journal start failing then we should prevent the
                // creation of new transactions.
                log::warn!("journal write failed: {}", e);
            }
        }
    }

    async fn maybe_extend_journal_file(&self) -> Result<(), Error> {
        // TODO(csuter): this currently assumes that a transaction can fit in CHUNK_SIZE.
        let mut extension_guard = None;
        let handle = match self.handle.get() {
            None => return Ok(()),
            Some(x) => x,
        };

        let (size, zero_offset) = loop {
            // TODO(csuter): we could maybe use self.objects.last_end_offset() instead here.
            let file_offset = debug_assert_not_too_long!(self.writer.lock())
                .journal_file_checkpoint()
                .file_offset;
            let size = handle.get_size();

            let size = if file_offset + self.chunk_size() > size { Some(size) } else { None };
            let zero_offset = self.inner.lock().unwrap().zero_offset.clone();

            if size.is_none() && zero_offset.is_none() {
                return Ok(());
            }
            if extension_guard.is_some() {
                break (size, zero_offset);
            }
            extension_guard = Some(debug_assert_not_too_long!(self.extension_mutex.lock()));
        };

        let mut transaction = handle
            .new_transaction_with_options(Options {
                skip_journal_checks: true,
                borrow_metadata_space: true,
                allocator_reservation: Some(self.objects.metadata_reservation()),
                ..Default::default()
            })
            .await?;
        transaction.skip_journal_extension = true;
        if let Some(size) = size {
            handle.preallocate_range(&mut transaction, size..size + self.chunk_size()).await?;
        }
        if let Some(zero_offset) = zero_offset {
            handle.zero(&mut transaction, 0..zero_offset).await?;
        }
        transaction.commit().await;

        // TODO(csuter): See if we can add an assertion that checks we managed to fit the
        // transaction that extends the journal within the old space.  It's tricky at this point
        // because as soon as we commit the transaction above, something else can slip in and start
        // using up the journal space.

        let mut inner = self.inner.lock().unwrap();
        if inner.zero_offset == zero_offset {
            inner.zero_offset = None;
        }

        Ok(())
    }

    // Determines whether a mutation at the given checkpoint should be applied.  During replay, not
    // all records should be applied because the object store or allocator might already contain the
    // mutation.  After replay, that obviously isn't the case and we want to apply all mutations.
    // Regardless, we want to keep track of the earliest mutation in the journal for a given object.
    fn should_apply(&self, object_id: u64, journal_file_checkpoint: &JournalCheckpoint) -> bool {
        let super_block = &self.inner.lock().unwrap().super_block;
        let offset = super_block
            .journal_file_offsets
            .get(&object_id)
            .cloned()
            .unwrap_or(super_block.super_block_journal_file_offset);
        journal_file_checkpoint.file_offset >= offset
    }

    pub async fn write_super_block(&self) -> Result<(), Error> {
        let root_parent_store = self.objects.root_parent_store();

        // First we must lock the root parent store so that no new entries are written to it.
        let mutable_layer = root_parent_store.tree().mutable_layer();
        let _guard = mutable_layer.lock_writes();

        // We need to flush previous writes to the device since the new super-block we are writing
        // relies on written data being observable.
        let (checkpoint, borrowed) =
            self.sync(SyncOptions { flush_device: true, ..Default::default() }).await?.unwrap();

        let (mut new_super_block, super_block_to_write) = {
            let inner = self.inner.lock().unwrap();
            (inner.super_block.clone(), inner.super_block_to_write)
        };
        let old_super_block_offset = new_super_block.journal_checkpoint.file_offset;

        let (journal_file_offsets, min_checkpoint) = self.objects.journal_file_offsets();

        // TODO(jfsulliv): Handle overflow.
        new_super_block.generation = new_super_block.generation.checked_add(1).unwrap();
        new_super_block.super_block_journal_file_offset = checkpoint.file_offset;
        new_super_block.journal_checkpoint = min_checkpoint.unwrap_or(checkpoint);
        new_super_block.journal_file_offsets = journal_file_offsets;
        new_super_block.borrowed_metadata_space = borrowed;

        // TODO(csuter); the super-block needs space reserved for it.
        new_super_block
            .write(
                &root_parent_store,
                ObjectStore::open_object(
                    &self.objects.root_store(),
                    super_block_to_write.object_id(),
                    journal_handle_options(),
                )
                .await?,
            )
            .await?;

        {
            let mut inner = self.inner.lock().unwrap();
            inner.super_block = new_super_block;
            inner.super_block_to_write = super_block_to_write.next();
            inner.zero_offset = Some(round_down(old_super_block_offset, BLOCK_SIZE));
            if let Some(event) = inner.reclaim_event.take() {
                event.signal();
            }
        }

        Ok(())
    }

    /// Flushes any buffered journal data to the device.  Note that this does not flush the device
    /// so it still does not guarantee data will have been persisted to lower layers.  If a
    /// precondition is supplied, it is evaluated and the sync will be skipped if it returns false.
    /// This allows callers to check a condition whilst a lock is held.  If a sync is performed,
    /// this function returns the checkpoint that was flushed and the amount of borrowed metadata
    /// space at the point it was flushed.
    pub async fn sync(
        &self,
        options: SyncOptions<'_>,
    ) -> Result<Option<(JournalCheckpoint, u64)>, Error> {
        let _guard = self.sync_mutex.lock().await;

        if let Some(precondition) = options.precondition {
            if !precondition() {
                return Ok(None);
            }
        }

        // TODO(csuter): We should optimize for the case where nothing needs to be done.
        let (checkpoint, borrowed) = {
            let mut writer = debug_assert_not_too_long!(self.writer.lock());
            serialize_into(&mut *writer, &JournalRecord::EndBlock)?;
            writer.pad_to_block()?;
            (
                writer.flush_buffer(self.handle.get().unwrap()).await?,
                self.objects.borrowed_metadata_space(),
            )
        };
        if options.flush_device {
            self.handle.get().unwrap().flush_device().await?;
            // If we are about to write a super-block, we could skip writing this record since it is
            // implicit, but it is probably not worth that optimisation.
            {
                let mut writer = self.writer.lock().await;
                serialize_into(
                    &mut *writer,
                    &JournalRecord::DidFlushDevice(checkpoint.file_offset),
                )?;
            }
            // Tell the allocator that we flushed the device so that it can now start using space
            // that was deallocated.
            self.objects.allocator().did_flush_device(checkpoint.file_offset).await;
        }
        Ok(Some((checkpoint, borrowed)))
    }

    /// Returns a copy of the super-block.
    pub fn super_block(&self) -> SuperBlock {
        self.inner.lock().unwrap().super_block.clone()
    }

    /// Returns whether or not a flush should be performed.  This is only updated after committing a
    /// transaction.
    pub fn should_flush(&self) -> bool {
        // The / 2 is here because after compacting, we cannot reclaim the space until the
        // _next_ time we flush the device since the super-block is not guaranteed to persist
        // until then.
        self.objects.last_end_offset()
            - self.inner.lock().unwrap().super_block.journal_checkpoint.file_offset
            > RECLAIM_SIZE / 2
    }

    /// Waits for there to be sufficient space in the journal.
    pub async fn check_journal_space(&self) {
        loop {
            debug_assert_not_too_long!({
                let mut inner = self.inner.lock().unwrap();
                if self.objects.last_end_offset() - inner.super_block.journal_checkpoint.file_offset
                    < RECLAIM_SIZE
                {
                    break;
                }
                let event = inner.reclaim_event.get_or_insert_with(|| Event::new());
                event.wait()
            });
        }
    }
    fn block_size(&self) -> u64 {
        BLOCK_SIZE
    }

    fn chunk_size(&self) -> u64 {
        CHUNK_SIZE
    }

    async fn write_and_apply_mutations(
        &self,
        writer: &mut JournalWriter,
        transaction: &mut Transaction<'_>,
    ) {
        let checkpoint = writer.journal_file_checkpoint();
        writer.write_mutations(transaction);
        if let Some(mutation) = self.objects.apply_transaction(transaction, &checkpoint).await {
            writer.write_record(&JournalRecord::Mutation { object_id: 0, mutation });
        }
        writer.write_record(&JournalRecord::Commit);
        self.objects.did_commit_transaction(
            transaction,
            &checkpoint,
            writer.journal_file_checkpoint().file_offset,
        );
    }
}

impl JournalWriter {
    // Extends JournalWriter to write mutations.
    fn write_mutations<'a>(&mut self, transaction: &Transaction<'_>) {
        for TxnMutation { object_id, mutation, .. } in &transaction.mutations {
            self.write_record(&JournalRecord::Mutation {
                object_id: *object_id,
                mutation: mutation.clone(),
            });
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        crate::{
            object_handle::{ObjectHandle, ObjectHandleExt},
            object_store::{
                directory::Directory,
                filesystem::{Filesystem, FxFilesystem, SyncOptions},
                fsck::fsck,
                journal::super_block::{SuperBlock, SuperBlockCopy},
                transaction::{Options, TransactionHandler},
                HandleOptions, ObjectStore,
            },
        },
        fuchsia_async as fasync,
        storage_device::{fake_device::FakeDevice, DeviceHolder},
    };

    const TEST_DEVICE_BLOCK_SIZE: u32 = 512;

    #[fasync::run_singlethreaded(test)]
    async fn test_alternating_super_blocks() {
        let device = DeviceHolder::new(FakeDevice::new(8192, TEST_DEVICE_BLOCK_SIZE));

        let fs = FxFilesystem::new_empty(device).await.expect("new_empty failed");
        fs.close().await.expect("Close failed");
        let device = fs.take_device().await;
        device.reopen();

        let (super_block_a, _) =
            SuperBlock::read(device.clone(), SuperBlockCopy::A).await.expect("read failed");
        let (super_block_b, _) =
            SuperBlock::read(device.clone(), SuperBlockCopy::B).await.expect("read failed");
        assert!(super_block_a.generation > super_block_b.generation);

        let fs = FxFilesystem::open(device).await.expect("open failed");
        let root_store = fs.root_store();
        // Generate enough work to induce a journal flush.
        for _ in 0..1000 {
            let mut transaction = fs
                .clone()
                .new_transaction(&[], Options::default())
                .await
                .expect("new_transaction failed");
            ObjectStore::create_object(&root_store, &mut transaction, HandleOptions::default())
                .await
                .expect("create_object failed");
            transaction.commit().await;
        }
        fs.close().await.expect("Close failed");
        let device = fs.take_device().await;
        device.reopen();

        let (super_block_a, _) =
            SuperBlock::read(device.clone(), SuperBlockCopy::A).await.expect("read failed");
        let (super_block_b, _) =
            SuperBlock::read(device.clone(), SuperBlockCopy::B).await.expect("read failed");
        assert!(super_block_b.generation > super_block_a.generation);
    }

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

            transaction.commit().await;
            let mut buf = handle.allocate_buffer(TEST_DATA.len());
            buf.as_mut_slice().copy_from_slice(TEST_DATA);
            handle.write(0, buf.as_ref()).await.expect("write failed");
            // As this is the first sync, this will actually trigger a new super-block, but normally
            // this would not be the case.
            fs.sync(SyncOptions::default()).await.expect("sync failed");
            handle.object_id()
        };

        {
            fs.close().await.expect("Close failed");
            let device = fs.take_device().await;
            device.reopen();
            let fs = FxFilesystem::open(device).await.expect("open failed");
            let handle =
                ObjectStore::open_object(&fs.root_store(), object_id, HandleOptions::default())
                    .await
                    .expect("open_object failed");
            let mut buf = handle.allocate_buffer(TEST_DEVICE_BLOCK_SIZE as usize);
            assert_eq!(handle.read(0, buf.as_mut()).await.expect("read failed"), TEST_DATA.len());
            assert_eq!(&buf.as_slice()[..TEST_DATA.len()], TEST_DATA);
            fsck(&fs).await.expect("fsck failed");
            fs.close().await.expect("Close failed");
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_reset() {
        const TEST_DATA: &[u8] = b"hello";

        let device = DeviceHolder::new(FakeDevice::new(16384, TEST_DEVICE_BLOCK_SIZE));

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
            transaction.commit().await;
            let mut buf = handle.allocate_buffer(TEST_DATA.len());
            buf.as_mut_slice().copy_from_slice(TEST_DATA);
            handle.write(0, buf.as_ref()).await.expect("write failed");
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
                transaction.commit().await;
                let mut buf = handle.allocate_buffer(TEST_DATA.len());
                buf.as_mut_slice().copy_from_slice(TEST_DATA);
                handle.write(0, buf.as_ref()).await.expect("write failed");
                object_ids.push(handle.object_id());
            }
        }
        fs.close().await.expect("Close failed");
        let device = fs.take_device().await;
        device.reopen();
        let fs = FxFilesystem::open(device).await.expect("open failed");
        fsck(&fs).await.expect("fsck failed");
        {
            let root_store = fs.root_store();
            // Check the first two objects which should exist.
            for &object_id in &object_ids[0..1] {
                let handle =
                    ObjectStore::open_object(&root_store, object_id, HandleOptions::default())
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
            transaction.commit().await;
            let mut buf = handle.allocate_buffer(TEST_DATA.len());
            buf.as_mut_slice().copy_from_slice(TEST_DATA);
            handle.write(0, buf.as_ref()).await.expect("write failed");
            fs.sync(SyncOptions::default()).await.expect("sync failed");
            object_ids.push(handle.object_id());
        }

        fs.close().await.expect("Close failed");
        let device = fs.take_device().await;
        device.reopen();
        let fs = FxFilesystem::open(device).await.expect("open failed");
        {
            fsck(&fs).await.expect("fsck failed");

            // Check the first two and the last objects.
            for &object_id in object_ids[0..1].iter().chain(object_ids.last().cloned().iter()) {
                let handle =
                    ObjectStore::open_object(&fs.root_store(), object_id, HandleOptions::default())
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
        fs.close().await.expect("Close failed");
    }
}
