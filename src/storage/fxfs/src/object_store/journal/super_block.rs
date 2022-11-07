// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        errors::FxfsError,
        filesystem::{ApplyContext, ApplyMode, JournalingObject},
        log::*,
        lsm_tree::types::LayerIterator,
        metrics::{traits::Metric as _, UintMetric},
        object_handle::BootstrapObjectHandle,
        object_store::{
            allocator::Reservation,
            journal::{
                reader::{JournalReader, ReadResult},
                writer::JournalWriter,
                JournalCheckpoint,
            },
            object_record::ObjectItem,
            transaction::{AssocObj, Options},
            HandleOptions, Mutation, ObjectStore, StoreObjectHandle,
        },
        range::RangeExt,
        serialized_types::{Version, Versioned, VersionedLatest, EARLIEST_SUPPORTED_VERSION},
    },
    anyhow::{bail, ensure, Context, Error},
    serde::{Deserialize, Serialize},
    std::{
        collections::HashMap,
        convert::TryInto,
        io::{Read, Write},
        ops::{Bound, Range},
        sync::{Arc, Mutex},
        time::SystemTime,
    },
    storage_device::Device,
    type_hash::TypeHash,
    uuid::Uuid,
};

// These only exist in the root store.
const SUPER_BLOCK_A_OBJECT_ID: u64 = 1;
const SUPER_BLOCK_B_OBJECT_ID: u64 = 2;

/// The block size used when reading and writing journal entries.
const SUPER_BLOCK_BLOCK_SIZE: usize = 8192;

/// The superblock is extended in units of `SUPER_BLOCK_CHUNK_SIZE` as required.
const SUPER_BLOCK_CHUNK_SIZE: u64 = 65536;

/// The first 2 * 512 KiB on the disk are reserved for two A/B super-blocks.
const MIN_SUPER_BLOCK_SIZE: u64 = 524_288;

/// All superblocks start with the magic bytes "FxfsSupr".
const SUPER_BLOCK_MAGIC: &[u8; 8] = b"FxfsSupr";

/// An enum representing one of our [SuperBlock] instances.
///
/// This provides hard-coded constants related to the location and properties of the super blocks
/// that are required to bootstrap the filesystem.
#[derive(Copy, Clone, Debug)]
pub enum SuperBlockInstance {
    A,
    B,
}

impl SuperBlockInstance {
    /// Returns the next [SuperBlockInstance] for use in round-robining writes across super-blocks.
    pub fn next(&self) -> SuperBlockInstance {
        match self {
            SuperBlockInstance::A => SuperBlockInstance::B,
            SuperBlockInstance::B => SuperBlockInstance::A,
        }
    }

    pub fn object_id(&self) -> u64 {
        match self {
            SuperBlockInstance::A => SUPER_BLOCK_A_OBJECT_ID,
            SuperBlockInstance::B => SUPER_BLOCK_B_OBJECT_ID,
        }
    }

    /// Returns the byte range where the first extent of the [SuperBlockInstance] is stored.
    /// (Note that a [SuperBlockInstance] may still have multiple extents.)
    pub fn first_extent(&self) -> Range<u64> {
        match self {
            SuperBlockInstance::A => 0..MIN_SUPER_BLOCK_SIZE,
            SuperBlockInstance::B => MIN_SUPER_BLOCK_SIZE..2 * MIN_SUPER_BLOCK_SIZE,
        }
    }
}

/// A super-block structure describing the filesystem.
///
/// We currently store two of these super blocks (A/B) located in two logical consecutive
/// 512kiB extents at the start of the device.
///
/// Immediately following the serialized `SuperBlock` structure below is a stream of serialized
/// operations that are replayed into the root parent `ObjectStore`. Note that the root parent
/// object store exists entirely in RAM until serialized back into the super-block.
///
/// Super blocks are updated alternately with a monotonically increasing generation number.
/// At mount time, the super block used is the valid `SuperBlock` with the highest generation
/// number.
#[derive(Clone, Debug, Default, Eq, PartialEq, Serialize, Deserialize, TypeHash, Versioned)]
pub struct SuperBlock {
    /// The globally unique identifier for the filesystem.
    guid: [u8; 16],

    /// There are two super-blocks which are used in an A/B configuration. The super-block with the
    /// greatest generation number is what is used when mounting an Fxfs image; the other is
    /// discarded.
    pub generation: u64,

    /// The root parent store is an in-memory only store and serves as the backing store for the
    /// root store and the journal.  The records for this store are serialized into the super-block
    /// and mutations are also recorded in the journal.
    pub root_parent_store_object_id: u64,

    /// The root parent needs a graveyard and there's nowhere else to store it other than in the
    /// super-block.
    pub root_parent_graveyard_directory_object_id: u64,

    /// The root object store contains all other metadata objects (including the allocator, the
    /// journal and the super-blocks) and is the parent for all other object stores.
    pub root_store_object_id: u64,

    /// This is in the root object store.
    pub allocator_object_id: u64,

    /// This is in the root parent object store.
    pub journal_object_id: u64,

    /// Start checkpoint for the journal file.
    pub journal_checkpoint: JournalCheckpoint,

    /// Offset of the journal file when the super-block was written.  If no entry is present in
    /// journal_file_offsets for a particular object, then an object might have dependencies on the
    /// journal from super_block_journal_file_offset onwards, but not earlier.
    pub super_block_journal_file_offset: u64,

    /// object id -> journal file offset. Indicates where each object has been flushed to.
    pub journal_file_offsets: HashMap<u64, u64>,

    /// Records the amount of borrowed metadata space as applicable at
    /// `super_block_journal_file_offset`.
    pub borrowed_metadata_space: u64,

    /// The earliest version of Fxfs used to create any still-existing struct in the filesystem.
    ///
    /// Note: structs in the filesystem may had been made with various different versions of Fxfs.
    pub earliest_version: Version,
}

#[derive(Serialize, Deserialize, TypeHash, Versioned)]
pub enum SuperBlockRecord {
    // When reading the super-block we know the initial extent, but not subsequent extents, so these
    // records need to exist to allow us to completely read the super-block.
    Extent(Range<u64>),

    // Following the super-block header are ObjectItem records that are to be replayed into the root
    // parent object store.
    ObjectItem(ObjectItem),

    // Marks the end of the full super-block.
    End,
}

struct SuperBlockMetrics {
    /// Time we wrote the most recent superblock in milliseconds since [`std::time::UNIX_EPOCH`].
    /// Uses [`std::time::SystemTime`] as the clock source.
    last_super_block_update_time_ms: UintMetric,

    /// Offset of the most recent superblock we wrote in the journal.
    last_super_block_offset: UintMetric,
}

impl Default for SuperBlockMetrics {
    fn default() -> Self {
        SuperBlockMetrics {
            last_super_block_update_time_ms: UintMetric::new("last_super_block_update_time_ms", 0),
            last_super_block_offset: UintMetric::new("last_super_block_offset", 0),
        }
    }
}

/// This encapsulates the A/B alternating SuperBlock logic.
/// All SuperBlockload/save operations should be via the methods on this type.
pub struct SuperBlockManager {
    next_instance: Arc<Mutex<SuperBlockInstance>>,
    metrics: SuperBlockMetrics,
}

impl SuperBlockManager {
    pub fn new() -> Self {
        Self {
            next_instance: Arc::new(Mutex::new(SuperBlockInstance::A)),
            metrics: Default::default(),
        }
    }

    // TODO(ripper): It would be nice to move create_objects here as well but for now our
    // transaction code requires handles to outlive transactions and so moving it here would
    // likely require us to split the single filesystem creation transaction into several.

    /// Loads both A/B super blocks and root_parent ObjectStores and and returns the newest valid
    /// pair. Also ensures the next superblock updated via |save| will be the other instance.
    pub async fn load(
        &self,
        device: Arc<dyn Device>,
        block_size: u64,
    ) -> Result<(SuperBlock, ObjectStore), Error> {
        let (super_block, current_super_block, root_parent) = match futures::join!(
            SuperBlock::read(device.clone(), block_size, SuperBlockInstance::A),
            SuperBlock::read(device.clone(), block_size, SuperBlockInstance::B)
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
        info!(?super_block, ?current_super_block, "loaded super-block");
        *self.next_instance.lock().unwrap() = current_super_block.next();
        Ok((super_block, root_parent))
    }

    /// Writes the provided superblock and root_parent ObjectStore to the device.
    /// Requires that the filesystem is fully loaded and writable as this may require allocation.
    pub async fn save(
        &self,
        super_block: &SuperBlock,
        root_parent: Arc<ObjectStore>,
    ) -> Result<(), Error> {
        let filesystem = root_parent.filesystem();
        let root_store = filesystem.root_store();
        let object_id = {
            let mut next_instance = self.next_instance.lock().unwrap();
            let object_id = next_instance.object_id();
            *next_instance = next_instance.next();
            object_id
        };
        let handle = ObjectStore::open_object(
            &root_store,
            object_id,
            HandleOptions { skip_journal_checks: true, ..Default::default() },
            None,
        )
        .await?;
        super_block.write(&root_parent, handle).await?;
        self.metrics.last_super_block_offset.set(super_block.super_block_journal_file_offset);
        self.metrics.last_super_block_update_time_ms.set(
            SystemTime::now()
                .duration_since(SystemTime::UNIX_EPOCH)
                .unwrap()
                .as_millis()
                .try_into()
                .unwrap_or(0u64),
        );
        Ok(())
    }
}

impl SuperBlock {
    /// Creates a new SuperBlock with random GUID.
    pub fn new(
        root_parent_store_object_id: u64,
        root_parent_graveyard_directory_object_id: u64,
        root_store_object_id: u64,
        allocator_object_id: u64,
        journal_object_id: u64,
        journal_checkpoint: JournalCheckpoint,
        earliest_version: Version,
    ) -> Self {
        let uuid = Uuid::new_v4();
        SuperBlock {
            guid: *uuid.as_bytes(),
            generation: 1u64,
            root_parent_store_object_id,
            root_parent_graveyard_directory_object_id,
            root_store_object_id,
            allocator_object_id,
            journal_object_id,
            journal_checkpoint,
            earliest_version,
            ..Default::default()
        }
    }

    /// Reads a SuperBlock instance and root_parent ObjectStore from a device.
    async fn read(
        device: Arc<dyn Device>,
        block_size: u64,
        instance: SuperBlockInstance,
    ) -> Result<(SuperBlock, SuperBlockInstance, ObjectStore), Error> {
        let (super_block, mut reader) = SuperBlock::read_header(device.clone(), instance)
            .await
            .context("Failed to read superblocks")?;
        let root_parent = ObjectStore::new_root_parent(
            device,
            block_size,
            super_block.root_parent_store_object_id,
        );
        root_parent.set_graveyard_directory_object_id(
            super_block.root_parent_graveyard_directory_object_id,
        );

        loop {
            let (mutation, sequence) = match reader.next_item().await? {
                SuperBlockItem::End => break,
                SuperBlockItem::Object(item) => {
                    (Mutation::insert_object(item.key, item.value), item.sequence)
                }
            };
            root_parent
                .apply_mutation(
                    mutation,
                    &ApplyContext {
                        mode: ApplyMode::Replay,
                        checkpoint: JournalCheckpoint {
                            file_offset: sequence,
                            ..Default::default()
                        },
                    },
                    AssocObj::None,
                )
                .await?;
        }

        Ok((super_block, instance, root_parent))
    }

    /// Shreds the super-block, rendering it unreadable.  This is used in mkfs to ensure that we
    /// wipe out any stale super-blocks when rewriting Fxfs.
    /// This isn't a secure shred in any way, it just ensures the super-block is not recognized as a
    /// super-block.
    pub async fn shred<S: AsRef<ObjectStore> + Send + Sync + 'static>(
        handle: StoreObjectHandle<S>,
    ) -> Result<(), Error> {
        let mut buf =
            handle.store().device().allocate_buffer(handle.store().device().block_size() as usize);
        buf.as_mut_slice().fill(0u8);
        handle.overwrite(0, buf.as_mut()).await
    }

    /// Read the super-block header, and return it and a reader that produces the records that are
    /// to be replayed in to the root parent object store.
    async fn read_header(
        device: Arc<dyn Device>,
        target_super_block: SuperBlockInstance,
    ) -> Result<(SuperBlock, ItemReader), Error> {
        let mut handle = BootstrapObjectHandle::new(target_super_block.object_id(), device);
        handle.push_extent(target_super_block.first_extent());
        let mut reader = JournalReader::new(
            handle,
            SUPER_BLOCK_BLOCK_SIZE as u64,
            &JournalCheckpoint::default(),
        );

        reader.fill_buf().await?;
        let mut super_block;
        let super_block_version;
        reader.consume({
            let mut cursor = std::io::Cursor::new(reader.buffer());
            // Validate magic bytes.
            let mut magic_bytes: [u8; 8] = [0; 8];
            cursor.read_exact(&mut magic_bytes)?;
            if magic_bytes.as_slice() != SUPER_BLOCK_MAGIC.as_slice() {
                bail!(format!("Invalid magic: {:?}", magic_bytes));
            }
            (super_block, super_block_version) = SuperBlock::deserialize_with_version(&mut cursor)?;

            if super_block_version < EARLIEST_SUPPORTED_VERSION {
                bail!(format!("Unsupported SuperBlock version: {:?}", super_block_version));
            }

            // NOTE: It is possible that data was written to the journal with an old version
            // but no compaction ever happened, so the journal version could potentially be older
            // than the layer file versions.
            if super_block.journal_checkpoint.version < EARLIEST_SUPPORTED_VERSION {
                bail!(format!(
                    "Unsupported JournalCheckpoint version: {:?}",
                    super_block.journal_checkpoint.version
                ));
            }

            if super_block.earliest_version < EARLIEST_SUPPORTED_VERSION {
                bail!(format!(
                    "Filesystem contains struct with unsupported version: {:?}",
                    super_block.earliest_version
                ));
            }

            cursor.position() as usize
        });
        // If guid is zeroed (e.g. in a newly imaged system), assign one randomly.
        if super_block.guid == [0; 16] {
            let uuid = Uuid::new_v4();
            super_block.guid = *uuid.as_bytes();
        }
        reader.set_version(super_block_version);
        Ok((super_block, ItemReader { reader }))
    }

    /// Writes the super-block and the records from the root parent store to |handle|.
    async fn write<'a, S: AsRef<ObjectStore> + Send + Sync + 'static>(
        &self,
        root_parent_store: &'a ObjectStore,
        handle: StoreObjectHandle<S>,
    ) -> Result<(), Error> {
        assert_eq!(root_parent_store.store_object_id(), self.root_parent_store_object_id);

        let object_manager = root_parent_store.filesystem().object_manager().clone();
        // TODO(fxbug.dev/95404): Don't use the same code here for Journal and SuperBlock. They
        // aren't the same things and it is already getting convoluted. e.g of diff stream content:
        //   Superblock:  (Magic, Ver, Header(Ver), SuperBlockRecord(Ver)*, ...)
        //   Journal:     (Ver, JournalRecord(Ver)*, RESET, Ver2, JournalRecord(Ver2)*, ...)
        // We should abstract away the checksum code and implement these separately.
        let mut writer = SuperBlockWriter::new(handle, object_manager.metadata_reservation());

        writer.writer.write_all(SUPER_BLOCK_MAGIC)?;
        self.serialize_with_version(&mut writer.writer)?;

        let tree = root_parent_store.tree();
        let layer_set = tree.layer_set();
        let mut merger = layer_set.merger();
        let mut iter = merger.seek(Bound::Unbounded).await?;

        while let Some(item_ref) = iter.get() {
            writer.maybe_extend().await?;
            SuperBlockRecord::ObjectItem(item_ref.cloned()).serialize_into(&mut writer.writer)?;
            iter.advance().await?;
        }

        SuperBlockRecord::End.serialize_into(&mut writer.writer)?;
        writer.writer.pad_to_block()?;
        writer.flush_buffer().await
    }
}

struct SuperBlockWriter<'a, S: AsRef<ObjectStore> + Send + Sync + 'static> {
    handle: StoreObjectHandle<S>,
    writer: JournalWriter,
    next_extent_offset: u64,
    reservation: &'a Reservation,
}

impl<'a, S: AsRef<ObjectStore> + Send + Sync + 'static> SuperBlockWriter<'a, S> {
    fn new(handle: StoreObjectHandle<S>, reservation: &'a Reservation) -> Self {
        Self {
            handle,
            writer: JournalWriter::new(SUPER_BLOCK_BLOCK_SIZE, 0),
            next_extent_offset: MIN_SUPER_BLOCK_SIZE,
            reservation,
        }
    }

    async fn maybe_extend(&mut self) -> Result<(), Error> {
        if self.writer.journal_file_checkpoint().file_offset
            < self.next_extent_offset - SUPER_BLOCK_CHUNK_SIZE
        {
            return Ok(());
        }
        let mut transaction = self
            .handle
            .new_transaction_with_options(Options {
                skip_journal_checks: true,
                borrow_metadata_space: true,
                allocator_reservation: Some(self.reservation),
                ..Default::default()
            })
            .await?;
        let allocated = self
            .handle
            .preallocate_range(
                &mut transaction,
                self.next_extent_offset..self.next_extent_offset + SUPER_BLOCK_CHUNK_SIZE,
            )
            .await?;
        transaction.commit().await?;
        for device_range in allocated {
            self.next_extent_offset += device_range.end - device_range.start;
            SuperBlockRecord::Extent(device_range).serialize_into(&mut self.writer)?;
        }
        Ok(())
    }

    async fn flush_buffer(&mut self) -> Result<(), Error> {
        let (offset, mut buf) = self.writer.take_buffer(&self.handle).unwrap();
        self.handle.overwrite(offset, buf.as_mut()).await
    }
}

#[derive(Debug)]
pub enum SuperBlockItem {
    End,
    Object(ObjectItem),
}

pub struct ItemReader {
    reader: JournalReader<BootstrapObjectHandle>,
}

impl ItemReader {
    pub async fn next_item(&mut self) -> Result<SuperBlockItem, Error> {
        loop {
            match self.reader.deserialize().await? {
                ReadResult::Reset => bail!("Unexpected reset"),
                ReadResult::ChecksumMismatch => bail!("Checksum mismatch"),
                ReadResult::Some(SuperBlockRecord::Extent(extent)) => {
                    ensure!(extent.is_valid(), FxfsError::Inconsistent);
                    self.reader.handle().push_extent(extent)
                }
                ReadResult::Some(SuperBlockRecord::ObjectItem(item)) => {
                    return Ok(SuperBlockItem::Object(item))
                }
                ReadResult::Some(SuperBlockRecord::End) => return Ok(SuperBlockItem::End),
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::{SuperBlock, SuperBlockInstance, SuperBlockItem, MIN_SUPER_BLOCK_SIZE},
        crate::{
            filesystem::{Filesystem, FxFilesystem},
            lsm_tree::types::LayerIterator,
            object_store::{
                allocator::{Allocator, SimpleAllocator},
                journal::{journal_handle_options, JournalCheckpoint},
                testing::fake_filesystem::FakeFilesystem,
                transaction::{Options, TransactionHandler},
                HandleOptions, NewChildStoreOptions, ObjectHandle, ObjectStore, StoreObjectHandle,
            },
            serialized_types::LATEST_VERSION,
        },
        fuchsia_async as fasync,
        std::{ops::Bound, sync::Arc},
        storage_device::{fake_device::FakeDevice, DeviceHolder},
    };

    const TEST_DEVICE_BLOCK_SIZE: u32 = 512;

    async fn filesystem_and_super_block_handles(
    ) -> (Arc<FakeFilesystem>, StoreObjectHandle<ObjectStore>, StoreObjectHandle<ObjectStore>) {
        let device = DeviceHolder::new(FakeDevice::new(8192, TEST_DEVICE_BLOCK_SIZE));
        let fs = FakeFilesystem::new(device);
        let allocator = Arc::new(SimpleAllocator::new(fs.clone(), 2));
        fs.object_manager().set_allocator(allocator.clone());
        fs.object_manager().init_metadata_reservation().expect("init_metadata_reservation failed");
        let root_parent_store = ObjectStore::new_empty(None, 3, fs.clone());
        fs.object_manager().set_root_parent_store(root_parent_store.clone());
        let root_store;
        let handle_a; // extend will borrow handle and needs to outlive transaction.
        let handle_b; // extend will borrow handle and needs to outlive transaction.
        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        root_store = root_parent_store
            .new_child_store(
                &mut transaction,
                NewChildStoreOptions { object_id: 4, ..Default::default() },
            )
            .await
            .expect("new_child_store failed");
        fs.object_manager().set_root_store(root_store.clone());
        allocator.create(&mut transaction).await.expect("allocator create failed");
        root_store.create(&mut transaction).await.expect("create failed");
        handle_a = ObjectStore::create_object_with_id(
            &root_store,
            &mut transaction,
            SuperBlockInstance::A.object_id(),
            journal_handle_options(),
            None,
        )
        .await
        .expect("create_object_with_id failed");
        handle_a
            .extend(&mut transaction, super::SuperBlockInstance::A.first_extent())
            .await
            .expect("extend failed");
        handle_b = ObjectStore::create_object_with_id(
            &root_store,
            &mut transaction,
            SuperBlockInstance::B.object_id(),
            journal_handle_options(),
            None,
        )
        .await
        .expect("create_object_with_id failed");
        handle_b
            .extend(&mut transaction, super::SuperBlockInstance::B.first_extent())
            .await
            .expect("extend failed");

        transaction.commit().await.expect("commit failed");

        (fs, handle_a, handle_b)
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_read_written_super_block() {
        let (fs, handle_a, handle_b) = filesystem_and_super_block_handles().await;
        const JOURNAL_OBJECT_ID: u64 = 5;

        // Create a large number of objects in the root parent store so that we test handling of
        // extents.
        let mut journal_offset = 0;
        for _ in 0..32000 {
            let mut transaction = fs
                .clone()
                .new_transaction(&[], Options::default())
                .await
                .expect("new_transaction failed");
            ObjectStore::create_object(
                &fs.object_manager().root_parent_store(),
                &mut transaction,
                HandleOptions::default(),
                None,
            )
            .await
            .expect("create_object failed");
            journal_offset = transaction.commit().await.expect("commit failed");
        }

        let mut super_block_a = SuperBlock::new(
            fs.object_manager().root_parent_store().store_object_id(),
            /* root_parent_graveyard_directory_object_id: */ 1000,
            fs.root_store().store_object_id(),
            fs.allocator().object_id(),
            JOURNAL_OBJECT_ID,
            JournalCheckpoint { file_offset: 1234, checksum: 5678, version: LATEST_VERSION },
            /* earliest_version: */ LATEST_VERSION,
        );
        super_block_a.super_block_journal_file_offset = journal_offset + 1;
        let mut super_block_b = super_block_a.clone();
        super_block_b.journal_file_offsets.insert(1, 2);
        super_block_b.generation += 1;

        // Check that a non-zero GUID has been assigned.
        assert_ne!(super_block_a.guid, [0; 16]);

        let layer_set = fs.object_manager().root_parent_store().tree().layer_set();
        let mut merger = layer_set.merger();

        super_block_a
            .write(fs.object_manager().root_parent_store().as_ref(), handle_a)
            .await
            .expect("write failed");
        super_block_b
            .write(fs.object_manager().root_parent_store().as_ref(), handle_b)
            .await
            .expect("write failed");

        // Make sure we did actually extend the super block.
        let handle = ObjectStore::open_object(
            &fs.root_store(),
            SuperBlockInstance::A.object_id(),
            HandleOptions::default(),
            None,
        )
        .await
        .expect("open_object failed");
        assert!(handle.get_size() > MIN_SUPER_BLOCK_SIZE);

        let mut written_super_block_a =
            SuperBlock::read_header(fs.device(), SuperBlockInstance::A).await.expect("read failed");

        assert_eq!(written_super_block_a.0, super_block_a);
        let written_super_block_b =
            SuperBlock::read_header(fs.device(), SuperBlockInstance::B).await.expect("read failed");
        assert_eq!(written_super_block_b.0, super_block_b);

        // Check that the records match what we expect in the root parent store.
        let mut iter = merger.seek(Bound::Unbounded).await.expect("seek failed");

        let mut written_item = written_super_block_a.1.next_item().await.expect("next_item failed");

        while let Some(item) = iter.get() {
            if let SuperBlockItem::Object(i) = written_item {
                assert_eq!(i.as_item_ref(), item);
            } else {
                panic!("missing item: {:?}", item);
            }
            iter.advance().await.expect("advance failed");
            written_item = written_super_block_a.1.next_item().await.expect("next_item failed");
        }

        if let SuperBlockItem::End = written_item {
        } else {
            panic!("unexpected extra item: {:?}", written_item);
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_guid_assign_on_read() {
        let (fs, handle_a, _handle_b) = filesystem_and_super_block_handles().await;
        const JOURNAL_OBJECT_ID: u64 = 5;
        let mut super_block_a = SuperBlock::new(
            fs.object_manager().root_parent_store().store_object_id(),
            /* root_parent_graveyard_directory_object_id: */ 1000,
            fs.root_store().store_object_id(),
            fs.allocator().object_id(),
            JOURNAL_OBJECT_ID,
            JournalCheckpoint { file_offset: 1234, checksum: 5678, version: LATEST_VERSION },
            /* earliest_version: */ LATEST_VERSION,
        );
        // Ensure the superblock has no set GUID.
        super_block_a.guid = [0; 16];
        super_block_a
            .write(fs.object_manager().root_parent_store().as_ref(), handle_a)
            .await
            .expect("write failed");
        let super_block =
            SuperBlock::read_header(fs.device(), SuperBlockInstance::A).await.expect("read failed");
        // Ensure a GUID has been assigned.
        assert_ne!(super_block.0.guid, [0; 16]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_init_wipes_superblocks() {
        let device = DeviceHolder::new(FakeDevice::new(8192, TEST_DEVICE_BLOCK_SIZE));

        let fs = FxFilesystem::new_empty(device).await.expect("new_empty failed");
        let root_store = fs.root_store();
        // Generate enough work to induce a journal flush and thus a new superblock being written.
        for _ in 0..8000 {
            let mut transaction = fs
                .clone()
                .new_transaction(&[], Options::default())
                .await
                .expect("new_transaction failed");
            ObjectStore::create_object(
                &root_store,
                &mut transaction,
                HandleOptions::default(),
                None,
            )
            .await
            .expect("create_object failed");
            transaction.commit().await.expect("commit failed");
        }
        fs.close().await.expect("Close failed");
        let device = fs.take_device().await;
        device.reopen(false);

        SuperBlock::read_header(device.clone(), SuperBlockInstance::A).await.expect("read failed");
        SuperBlock::read_header(device.clone(), SuperBlockInstance::B).await.expect("read failed");

        // Re-initialize the filesystem.  The A block should be reset and the B block should be
        // wiped.
        let fs = FxFilesystem::new_empty(device).await.expect("new_empty failed");
        fs.close().await.expect("Close failed");
        let device = fs.take_device().await;
        device.reopen(false);

        SuperBlock::read_header(device.clone(), SuperBlockInstance::A).await.expect("read failed");
        SuperBlock::read_header(device.clone(), SuperBlockInstance::B)
            .await
            .map(|_| ())
            .expect_err("Super-block B was readable after a re-format");
    }
    #[fasync::run_singlethreaded(test)]
    async fn test_alternating_super_blocks() {
        let device = DeviceHolder::new(FakeDevice::new(8192, TEST_DEVICE_BLOCK_SIZE));

        let fs = FxFilesystem::new_empty(device).await.expect("new_empty failed");
        fs.close().await.expect("Close failed");
        let device = fs.take_device().await;
        device.reopen(false);

        let (super_block_a, _) = SuperBlock::read_header(device.clone(), SuperBlockInstance::A)
            .await
            .expect("read failed");

        // The second super-block won't be valid at this time so there's no point reading it.

        let fs = FxFilesystem::open(device).await.expect("open failed");
        let root_store = fs.root_store();
        // Generate enough work to induce a journal flush.
        for _ in 0..8000 {
            let mut transaction = fs
                .clone()
                .new_transaction(&[], Options::default())
                .await
                .expect("new_transaction failed");
            ObjectStore::create_object(
                &root_store,
                &mut transaction,
                HandleOptions::default(),
                None,
            )
            .await
            .expect("create_object failed");
            transaction.commit().await.expect("commit failed");
        }
        fs.close().await.expect("Close failed");
        let device = fs.take_device().await;
        device.reopen(false);

        let (super_block_a_after, _) =
            SuperBlock::read_header(device.clone(), SuperBlockInstance::A)
                .await
                .expect("read failed");
        let (super_block_b_after, _) =
            SuperBlock::read_header(device.clone(), SuperBlockInstance::B)
                .await
                .expect("read failed");

        // It's possible that multiple super-blocks were written, so cater for that.

        // The sequence numbers should be one apart.
        assert_eq!(
            (super_block_b_after.generation as i64 - super_block_a_after.generation as i64).abs(),
            1
        );

        // At least one super-block should have been written.
        assert!(
            std::cmp::max(super_block_a_after.generation, super_block_b_after.generation)
                > super_block_a.generation
        );

        // They should have the same oddness.
        assert_eq!(super_block_a_after.generation & 1, super_block_a.generation & 1);
    }
}
