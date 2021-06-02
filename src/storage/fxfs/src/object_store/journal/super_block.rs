// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        lsm_tree::types::LayerIterator,
        object_handle::{ObjectHandle, ObjectProperties},
        object_store::{
            constants::SUPER_BLOCK_OBJECT_ID,
            journal::{
                reader::{JournalReader, ReadResult},
                writer::JournalWriter,
                JournalCheckpoint,
            },
            record::{ObjectItem, Timestamp},
            transaction::{self, Transaction},
            ObjectStore,
        },
    },
    anyhow::{bail, Error},
    async_trait::async_trait,
    bincode::serialize_into,
    serde::{Deserialize, Serialize},
    std::{
        cmp::min,
        collections::HashMap,
        ops::{Bound, Range},
        sync::Arc,
    },
    storage_device::{
        buffer::{Buffer, BufferRef, MutableBufferRef},
        Device,
    },
};

const SUPER_BLOCK_BLOCK_SIZE: usize = 8192;
const SUPER_BLOCK_CHUNK_SIZE: u64 = 65536;

// The first 512 KiB on the disk are reserved for the super block.
const MIN_SUPER_BLOCK_SIZE: u64 = 524_288;

// A super-block consists of this header followed by records that are to be replayed into the root
// parent object store.
#[derive(Clone, Debug, Default, Eq, PartialEq, Serialize, Deserialize)]
pub struct SuperBlock {
    // TODO(csuter): version stuff
    // TODO(csuter): UUID

    // The root parent store is an in-memory only store and serves as the backing store for the root
    // store and the journal.  The records for this store are serialized into the super-block and
    // mutations are also recorded in the journal.
    pub root_parent_store_object_id: u64,

    // The root object store contains all other metadata objects (including the allocator, the
    // journal and the super-blocks) and is the parent for all other object stores.
    pub root_store_object_id: u64,

    // This is in the root object store.
    pub allocator_object_id: u64,

    // This is in the root parent object store.
    pub journal_object_id: u64,

    // Start checkpoint for the journal file.
    pub journal_checkpoint: JournalCheckpoint,

    // Offset of the journal file when the super-block was written.  If no entry is present in
    // journal_file_offsets for a particular object, then an object might have dependencies on the
    // journal from super_block_journal_file_offset onwards, but not earlier.
    pub super_block_journal_file_offset: u64,

    // object id -> journal file offset. Indicates where each object has been flushed to.
    pub journal_file_offsets: HashMap<u64, u64>,
}

// TODO(csuter): Add support for multiple super-blocks.
pub fn first_extent() -> Range<u64> {
    return 0..MIN_SUPER_BLOCK_SIZE;
}

#[derive(Serialize, Deserialize)]
enum SuperBlockRecord {
    // When reading the super-block we know the initial extent, but not subsequent extents, so these
    // records need to exist to allow us to completely read the super-block.
    Extent(Range<u64>),

    // Following the super-block header are ObjectItem records that are to be replayed into the root
    // parent object store.
    Item(ObjectItem),

    // Marks the end of the full super-block.
    End,
}

// When we are reading the super-block we have to use something special for reading it because we
// don't have an object store we can use.
struct SuperBlockHandle {
    device: Arc<dyn Device>,
    extents: Vec<Range<u64>>,
}

#[async_trait]
impl ObjectHandle for SuperBlockHandle {
    fn object_id(&self) -> u64 {
        SUPER_BLOCK_OBJECT_ID
    }

    fn allocate_buffer(&self, size: usize) -> Buffer<'_> {
        self.device.allocate_buffer(size)
    }

    fn block_size(&self) -> u32 {
        self.device.block_size()
    }

    async fn read(&self, mut offset: u64, mut buf: MutableBufferRef<'_>) -> Result<usize, Error> {
        let len = buf.len();
        let mut buf_offset = 0;
        let mut file_offset = 0;
        for extent in &self.extents {
            let extent_len = extent.end - extent.start;
            if offset < file_offset + extent_len {
                let device_offset = extent.start + offset - file_offset;
                let to_read = min(extent.end - device_offset, (len - buf_offset) as u64) as usize;
                assert!(buf_offset % self.device.block_size() as usize == 0);
                self.device
                    .read(
                        device_offset,
                        buf.reborrow().subslice_mut(buf_offset..buf_offset + to_read),
                    )
                    .await?;
                buf_offset += to_read;
                if buf_offset == len {
                    break;
                }
                offset += to_read as u64;
            }
            file_offset += extent_len;
        }
        Ok(len)
    }

    async fn txn_write<'a>(
        &'a self,
        _transaction: &mut Transaction<'a>,
        _offset: u64,
        _buf: BufferRef<'_>,
    ) -> Result<(), Error> {
        unreachable!();
    }

    fn get_size(&self) -> u64 {
        unreachable!();
    }

    async fn truncate<'a>(
        &'a self,
        _transaction: &mut Transaction<'a>,
        _length: u64,
    ) -> Result<(), Error> {
        unreachable!();
    }

    async fn preallocate_range<'a>(
        &'a self,
        _transaction: &mut Transaction<'a>,
        _range: Range<u64>,
    ) -> Result<Vec<Range<u64>>, Error> {
        unreachable!();
    }

    async fn update_timestamps<'a>(
        &'a self,
        _transaction: Option<&mut Transaction<'a>>,
        _ctime: Option<Timestamp>,
        _mtime: Option<Timestamp>,
    ) -> Result<(), Error> {
        unreachable!();
    }

    async fn get_properties(&self) -> Result<ObjectProperties, Error> {
        unreachable!();
    }

    async fn new_transaction_with_options<'a>(
        &self,
        _options: transaction::Options<'a>,
    ) -> Result<Transaction<'a>, Error> {
        unreachable!();
    }
}

impl SuperBlock {
    pub(super) fn new(
        root_parent_store_object_id: u64,
        root_store_object_id: u64,
        allocator_object_id: u64,
        journal_object_id: u64,
        journal_checkpoint: JournalCheckpoint,
    ) -> Self {
        SuperBlock {
            root_parent_store_object_id,
            root_store_object_id,
            allocator_object_id,
            journal_object_id,
            journal_checkpoint,
            ..Default::default()
        }
    }

    /// Read the super-block header, and return it and a reader that produces the records that are
    /// to be replayed in to the root parent object store.
    pub async fn read(device: Arc<dyn Device>) -> Result<(SuperBlock, ItemReader), Error> {
        let mut reader = JournalReader::new(
            SuperBlockHandle { device, extents: vec![first_extent()] },
            SUPER_BLOCK_BLOCK_SIZE as u64,
            &JournalCheckpoint::default(),
        );
        let super_block = match reader.deserialize::<SuperBlock>().await? {
            ReadResult::Reset => bail!("Unexpected reset"),
            ReadResult::ChecksumMismatch => bail!("Checksum mismatch"),
            ReadResult::Some(super_block) => super_block,
        };
        Ok((super_block, ItemReader(reader)))
    }

    /// Writes the super-block and the records from the root parent store.
    pub(super) async fn write<'a>(
        &self,
        root_parent_store: &'a ObjectStore,
        handle: impl ObjectHandle,
    ) -> Result<(), Error> {
        assert_eq!(root_parent_store.store_object_id(), self.root_parent_store_object_id);

        let mut writer = JournalWriter::new(SUPER_BLOCK_BLOCK_SIZE, 0);

        serialize_into(&mut writer, &self)?;

        let tree = root_parent_store.tree();
        let layer_set = tree.layer_set();
        let mut merger = layer_set.merger();
        let mut iter = merger.seek(Bound::Unbounded).await?;

        let mut next_extent_offset = MIN_SUPER_BLOCK_SIZE;

        while let Some(item_ref) = iter.get() {
            if writer.journal_file_checkpoint().file_offset
                >= next_extent_offset - SUPER_BLOCK_CHUNK_SIZE
            {
                let mut transaction = handle.new_transaction().await?;
                let allocated = handle
                    .preallocate_range(
                        &mut transaction,
                        next_extent_offset..next_extent_offset + SUPER_BLOCK_CHUNK_SIZE,
                    )
                    .await?;
                transaction.commit().await;
                for device_range in allocated {
                    next_extent_offset += device_range.end - device_range.start;
                    serialize_into(&mut writer, &SuperBlockRecord::Extent(device_range))?;
                }
            }
            serialize_into(&mut writer, &SuperBlockRecord::Item(item_ref.cloned()))?;
            iter.advance().await?;
        }
        serialize_into(&mut writer, &SuperBlockRecord::End)?;
        writer.pad_to_block()?;
        writer.flush_buffer(&handle).await?;
        Ok(())
    }
}

pub struct ItemReader(JournalReader<SuperBlockHandle>);

impl ItemReader {
    pub async fn next_item(&mut self) -> Result<Option<ObjectItem>, Error> {
        loop {
            match self.0.deserialize().await? {
                ReadResult::Reset => bail!("Unexpected reset"),
                ReadResult::ChecksumMismatch => bail!("Checksum mismatch"),
                ReadResult::Some(SuperBlockRecord::Extent(extent)) => {
                    self.0.handle().extents.push(extent)
                }
                ReadResult::Some(SuperBlockRecord::Item(item)) => return Ok(Some(item)),
                ReadResult::Some(SuperBlockRecord::End) => return Ok(None),
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::{SuperBlock, MIN_SUPER_BLOCK_SIZE},
        crate::{
            lsm_tree::types::LayerIterator,
            object_handle::ObjectHandle,
            object_store::{
                allocator::Allocator,
                constants::SUPER_BLOCK_OBJECT_ID,
                filesystem::Filesystem,
                journal::{journal_handle_options, JournalCheckpoint},
                testing::{fake_allocator::FakeAllocator, fake_filesystem::FakeFilesystem},
                transaction::{Options, TransactionHandler},
                HandleOptions, ObjectStore,
            },
        },
        fuchsia_async as fasync,
        std::{ops::Bound, sync::Arc},
        storage_device::{fake_device::FakeDevice, DeviceHolder},
    };

    const TEST_DEVICE_BLOCK_SIZE: u32 = 512;

    #[fasync::run_singlethreaded(test)]
    async fn test_read_written_super_block() {
        let device = DeviceHolder::new(FakeDevice::new(2048, TEST_DEVICE_BLOCK_SIZE));
        let fs = FakeFilesystem::new(device);
        let allocator = Arc::new(FakeAllocator::new());
        fs.object_manager().set_allocator(allocator.clone());
        let root_parent_store = ObjectStore::new_empty(None, 2, fs.clone());
        fs.object_manager().set_root_parent_store(root_parent_store.clone());
        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        let root_store = root_parent_store
            .create_child_store_with_id(&mut transaction, 3)
            .await
            .expect("create_child_store failed");
        const JOURNAL_OBJECT_ID: u64 = 4;

        // Create a large number of objects in the root parent store so that we test handling of
        // extents.
        for _ in 0..8000 {
            let mut transaction = fs
                .clone()
                .new_transaction(&[], Options::default())
                .await
                .expect("new_transaction failed");
            ObjectStore::create_object(
                &root_parent_store,
                &mut transaction,
                HandleOptions::default(),
            )
            .await
            .expect("create_object failed");
            transaction.commit().await;
        }

        let mut super_block = SuperBlock::new(
            root_parent_store.store_object_id(),
            root_store.store_object_id(),
            allocator.object_id(),
            JOURNAL_OBJECT_ID,
            JournalCheckpoint { file_offset: 1234, checksum: 5678 },
        );
        super_block.journal_file_offsets.insert(1, 2);

        let handle; // extend will borrow handle and needs to outlive transaction.
        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        handle = ObjectStore::create_object_with_id(
            &root_store,
            &mut transaction,
            SUPER_BLOCK_OBJECT_ID,
            journal_handle_options(),
        )
        .await
        .expect("create_object_with_id failed");
        handle.extend(&mut transaction, super::first_extent()).await.expect("extend failed");

        transaction.commit().await;

        let layer_set = root_parent_store.tree().layer_set();
        let mut merger = layer_set.merger();

        super_block.write(&root_parent_store, handle).await.expect("write failed");

        // Make sure we did actually extend the super block.
        let handle =
            ObjectStore::open_object(&root_store, SUPER_BLOCK_OBJECT_ID, HandleOptions::default())
                .await
                .expect("open_object failed");
        assert!(handle.get_size() > MIN_SUPER_BLOCK_SIZE);

        let mut written_super_block = SuperBlock::read(fs.device()).await.expect("read failed");

        assert_eq!(written_super_block.0, super_block);

        // Check that the records match what we expect in the root parent store.
        let mut iter = merger.seek(Bound::Unbounded).await.expect("seek failed");
        while let Some(item) = written_super_block.1.next_item().await.expect("next_item failed") {
            assert_eq!(item.as_item_ref(), iter.get().expect("missing item"));
            iter.advance().await.expect("advance failed");
        }
    }
}
