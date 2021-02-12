use {
    crate::{
        object_handle::ObjectHandle,
        object_store::{
            allocator::{Allocator, AllocatorItem},
            constants::{MIN_SUPER_BLOCK_SIZE, ROOT_PARENT_STORE_OBJECT_ID, SUPER_BLOCK_OBJECT_ID},
            filesystem::{StoreManager, SyncOptions},
            record::{decode_extent, ExtentKey, ObjectItem, ObjectKey, ObjectValue},
            Device, HandleOptions, ObjectStore, StoreObjectHandle, StoreOptions,
        },
    },
    anyhow::Error,
    bincode::{deserialize_from, serialize_into},
    byteorder::{ByteOrder, LittleEndian, WriteBytesExt},
    rand::Rng,
    serde::{Deserialize, Serialize},
    std::{
        clone::Clone,
        cmp::min,
        collections::HashMap,
        io::Write,
        ops::Range,
        slice,
        sync::{Arc, Mutex, MutexGuard},
        vec::Vec,
    },
};

// TODO: Check all occurrences of unwrap()

pub const BLOCK_SIZE: u64 = 4096; // TODO
const BLOCKS_PER_FILE_CHUNK: u64 = 16; // TODO
const RESET_XOR: u64 = 0xffffffffffffffff;

// LogReader and LogWriter expect some properties of log that are same. For not, log might not function properly
// if they change. This trait defines few such shared properties.
trait LogProperties {
    fn block_size(&self) -> u64 {
        BLOCK_SIZE
    }

    fn blocks_per_file_chunk(&self) -> u64 {
        BLOCKS_PER_FILE_CHUNK
    }

    fn file_chunk_size(&self) -> u64 {
        self.blocks_per_file_chunk() * self.block_size()
    }
}

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
pub struct LogCheckpoint {
    file_offset: u64,

    // Starting check-sum for block that contains file_offset.
    check_sum: u64,
}

impl LogCheckpoint {
    fn new(file_offset: u64, check_sum: u64) -> LogCheckpoint {
        LogCheckpoint { file_offset, check_sum }
    }
}

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
pub struct SuperBlock {
    // TODO: version stuff
    // TODO: UUID
    root_store_object_id: u64,
    allocator_object_id: u64,
    log_object_id: u64,

    // Start checkpoint for the log file.
    log_checkpoint: LogCheckpoint,

    // Offset of the log file when the super-block was written.
    super_block_log_file_offset: u64,

    // object id -> log file offset. Indicates where each object has been flushed to.
    log_file_offsets: HashMap<u64, u64>,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Extent {
    // The file range for this extent.
    range: Range<u64>,

    // The device offset for this extent.
    device_offset: u64,
}

impl Extent {
    fn first_super_block_extent() -> Extent {
        Extent { range: 0..MIN_SUPER_BLOCK_SIZE, device_offset: 0 }
    }
}

fn range_len(range: &Range<u64>) -> u64 {
    range.end - range.start
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub enum Mutation {
    // Inserts a record.
    Insert {
        item: ObjectItem,
    },
    // Inserts or replaces a record.
    ReplaceOrInsert {
        // TODO: Do we need both insert and ReplaceOrInsert?
        item: ObjectItem,
    },
    // Inserts or replaces an extent.
    ReplaceExtent {
        item: ObjectItem,
    },
    Deallocate(AllocatorItem),
}

#[derive(Clone, Debug, Serialize, Deserialize)]
enum LogRecord {
    // Indicates no more records in this block.
    EndBlock,

    // Mutation for a particular object.
    Mutation { object_id: u64, mutation: Mutation },

    // Adds an extent to this log file. This only applies to the super-block.
    AddExtent(Extent), // TODO: rename

    // Commits records in the transaction.
    Commit,
}

pub struct Transaction {
    mutations: Vec<(u64, Mutation)>,
}

impl Transaction {
    pub fn new() -> Transaction {
        Transaction { mutations: Vec::new() }
    }

    pub fn add(&mut self, object_id: u64, mutation: Mutation) {
        self.mutations.push((object_id, mutation));
    }
}

// TODO: endianness
pub fn fletcher64(buf: &[u8], previous: u64) -> u64 {
    assert!(buf.len() % 4 == 0);
    let u32buf = unsafe { slice::from_raw_parts(buf.as_ptr() as *const u32, buf.len() / 4) };
    let mut lo = previous as u32;
    let mut hi = (previous >> 32) as u32;
    for &i in u32buf {
        lo = lo.wrapping_add(i);
        hi = hi.wrapping_add(lo);
    }
    (hi as u64) << 32 | lo as u64
}

fn log_handle_options() -> HandleOptions {
    HandleOptions { overwrite: true, ..Default::default() }
}

trait LogReaderHandle: ObjectHandle {
    fn as_object_handle(self: Box<Self>) -> Box<dyn ObjectHandle>;

    fn add_extent(&mut self, extent: Extent);
}

struct SuperBlockHandle {
    device: Arc<dyn Device>,
    extents: Vec<Extent>,
}

impl LogReaderHandle for SuperBlockHandle {
    fn as_object_handle(self: Box<Self>) -> Box<dyn ObjectHandle> {
        self
    }

    fn add_extent(&mut self, extent: Extent) {
        self.extents.push(extent);
    }
}

impl ObjectHandle for SuperBlockHandle {
    fn object_id(&self) -> u64 {
        SUPER_BLOCK_OBJECT_ID
    }

    fn read(&self, mut offset: u64, buf: &mut [u8]) -> std::io::Result<usize> {
        println!("reading superblock {:?} @ {:?}, extents={:?}", buf.len(), offset, self.extents);
        let mut buf_offset = 0;
        for extent in &self.extents {
            if offset < extent.range.end {
                let device_offset = extent.device_offset + offset - extent.range.start;
                let end =
                    min(extent.range.end - offset + buf_offset as u64, buf.len() as u64) as usize;
                self.device.read(device_offset, &mut buf[buf_offset..end])?;
                buf_offset = end;
                if buf_offset == buf.len() {
                    break;
                }
                offset = extent.range.end;
            }
        }
        Ok(buf.len())
    }

    fn write(&self, _offset: u64, _buf: &[u8]) -> std::io::Result<()> {
        unreachable!();
    }

    fn get_size(&self) -> u64 {
        unreachable!();
    }

    fn preallocate_range(
        &self,
        _range: Range<u64>,
        _transaction: &mut Transaction,
    ) -> std::io::Result<Vec<std::ops::Range<u64>>> {
        unreachable!();
    }
}

struct LogWriter {
    handle: Option<Box<dyn ObjectHandle>>,
    offset: u64,
    last_check_sum: u64,
    buf: Vec<u8>,
    reset: bool,
}

impl LogProperties for LogWriter {}

impl LogWriter {
    fn new(handle: Option<Box<dyn ObjectHandle>>, last_check_sum: u64) -> LogWriter {
        LogWriter { handle, offset: 0, last_check_sum, buf: Vec::new(), reset: false }
    }

    fn pad_to_block(&mut self) -> std::io::Result<()> {
        let align = self.buf.len() % self.block_size() as usize;
        if align > 0 {
            self.write(&vec![0; self.block_size() as usize - 8 - align])?;
        }
        Ok(())
    }

    fn log_file_checkpoint(&self) -> LogCheckpoint {
        // println!("log_file_checkpoint: {:?} {:?}", self.offset, self.buf.len());
        LogCheckpoint::new(self.offset + self.buf.len() as u64, self.last_check_sum)
    }

    fn write_transaction(&mut self, transaction: &Transaction) -> LogCheckpoint {
        let log_file_checkpoint = self.log_file_checkpoint();
        {
            for mutation in &transaction.mutations {
                // TODO: we might not need the clone here.
                serialize_into(
                    &mut *self,
                    &LogRecord::Mutation { object_id: mutation.0, mutation: mutation.1.clone() },
                )
                .unwrap();
            }
            serialize_into(self, &LogRecord::Commit).unwrap(); // TODO
        }
        log_file_checkpoint
    }
}

impl std::io::Write for LogWriter {
    fn write(&mut self, buf: &[u8]) -> std::io::Result<usize> {
        let bs = self.block_size() as usize;
        let mut offset = 0;
        while offset < buf.len() {
            let space = bs - 8 - self.buf.len() % bs;
            let to_copy = min(space, buf.len() - offset);
            self.buf.write(&buf[offset..offset + to_copy])?;
            if to_copy == space {
                let end = self.buf.len();
                let start = end + 8 - bs;
                self.last_check_sum = fletcher64(&self.buf[start..end], self.last_check_sum);
                if self.reset {
                    self.last_check_sum ^= RESET_XOR;
                    self.reset = false;
                }
                self.buf.write_u64::<LittleEndian>(self.last_check_sum)?;
                // println!("wrote checksum {:?}", self.last_check_sum);
            }
            offset += to_copy;
        }
        self.flush()?;
        Ok(buf.len())
    }

    fn flush(&mut self) -> std::io::Result<()> {
        assert!(self.offset % self.block_size() == 0);
        let block_size = self.block_size();
        if let Some(ref mut handle) = self.handle {
            let bs = block_size as usize;
            // TODO: Zero copy/move.
            let len = self.buf.len() - self.buf.len() % bs;
            if len > 0 {
                // println!("writing {:?} @ {:?}", len, self.offset);
                handle.write(self.offset, &self.buf[..len])?;
                self.offset += len as u64;
                self.buf.drain(..len);
            }
        }
        Ok(())
    }
}

pub struct Log(Mutex<Option<InitializedLog>>);

impl LogProperties for Log {}

impl Log {
    pub fn new() -> Log {
        Log(Mutex::new(None))
    }

    pub fn replay(
        self: &Arc<Self>,
        device: Arc<dyn Device>,
        stores: Arc<StoreManager>,
        allocator: Arc<dyn Allocator>,
    ) -> Result<(), Error> {
        println!("replay");
        let mut reader = LogReader::new(Box::new(SuperBlockHandle {
            device: device.clone(),
            extents: vec![Extent::first_super_block_extent()],
        }));
        let super_block: SuperBlock = deserialize_from(&mut reader)?;
        println!("super-block: {:?}", super_block);
        stores.new_store(&ObjectStore::new_empty(
            None,
            ROOT_PARENT_STORE_OBJECT_ID,
            device,
            &allocator,
            &self,
            StoreOptions::default(),
        ));
        // Skip to the end of the block; super-block always occupies whole block.
        reader.skip_to_end_of_block();
        let mut mutations = Vec::new();
        let mut log_file_checkpoint = None;
        let mut end_block = false;
        let mut found_end_of_super_block = false;
        loop {
            let current_checkpoint = Some(reader.log_file_checkpoint());
            match deserialize_from(&mut reader) {
                Err(e) => {
                    // TODO: Need to handle how to continue when it's corrupt.
                    if reader.bad_check_sum && found_end_of_super_block {
                        if reader.found_reset {
                            // Handle log reset.
                            mutations.clear();
                            reader.reset();
                            continue;
                        } else {
                            // EOF
                            break;
                        }
                    }
                    return Err(e)?;
                }
                Ok(record) => {
                    // println!("record: {:?}", record);
                    end_block = false;
                    match record {
                        LogRecord::EndBlock => {
                            reader.skip_to_end_of_block();
                            end_block = true;
                        }
                        LogRecord::AddExtent(extent) => {
                            reader.handle.add_extent(extent);
                        }
                        LogRecord::Mutation { object_id, mutation } => {
                            if mutations.len() == 0 {
                                log_file_checkpoint = current_checkpoint;
                            }
                            mutations.push((object_id, mutation));
                        }
                        LogRecord::Commit => {
                            if !found_end_of_super_block {
                                *self.log() = Some(InitializedLog {
                                    stores: stores.clone(),
                                    allocator: allocator.clone(),
                                    writer: LogWriter::new(None, 0),
                                    needs_super_block: false,
                                    super_block: super_block.clone(),
                                    log_file_checkpoints: HashMap::new(),
                                });
                            }
                            if let Some(checkpoint) = log_file_checkpoint.take() {
                                mutations.drain(..).for_each(|m| {
                                    self.log().as_mut().unwrap().apply_mutation(
                                        m.0,
                                        &checkpoint,
                                        m.1,
                                        Some(&super_block.log_file_offsets),
                                    )
                                });
                            }
                            if !found_end_of_super_block {
                                let root_parent = stores.root_parent_store();
                                stores.set_root_store(root_parent.open_store(
                                    super_block.root_store_object_id,
                                    StoreOptions {
                                        use_parent_to_allocate_object_ids: true,
                                        ..Default::default()
                                    },
                                )?);
                                reader.buf.clear();
                                reader.read_offset = super_block.log_checkpoint.file_offset;
                                reader.last_check_sum = super_block.log_checkpoint.check_sum;
                                reader.last_read_check_sum = super_block.log_checkpoint.check_sum;
                                reader.handle = Box::new(stores.root_store().open_object(
                                    super_block.log_object_id,
                                    log_handle_options(),
                                )?);
                                found_end_of_super_block = true;
                            }
                        }
                    }
                }
            }
        }
        let mut log = self.log();
        let mut writer = &mut log.as_mut().unwrap().writer;
        writer.handle = Some(reader.handle.as_object_handle());
        writer.offset = reader.read_offset;
        // If the last entry wasn't an end_block, then we need to reset the stream.
        writer.reset = !end_block;
        writer.last_check_sum = reader.last_read_check_sum;
        let root_store = stores.root_store();
        allocator.open(&root_store, super_block.allocator_object_id)?;
        println!("replay done");
        Ok(())
    }

    pub fn init_empty(
        &self,
        stores: &Arc<StoreManager>,
        allocator: &Arc<dyn Allocator>,
    ) -> Result<(), Error> {
        let mut rng = rand::thread_rng();
        let starting_check_sum: u64 = rng.gen();
        *self.log() = Some(InitializedLog {
            allocator: allocator.clone(),
            stores: stores.clone(),
            writer: LogWriter::new(None, starting_check_sum),
            needs_super_block: true,
            super_block: SuperBlock::default(),
            log_file_checkpoints: HashMap::new(),
        });
        stores.set_root_store(stores.root_parent_store().create_child_store(StoreOptions {
            use_parent_to_allocate_object_ids: true,
            ..Default::default()
        })?);
        let root_store = stores.root_store();
        println!("root store object id {:?}", root_store.store_object_id());
        allocator.init(&root_store)?;
        allocator.set_next_block(MIN_SUPER_BLOCK_SIZE / 512); // TODO: stop using blocks.
        let mut transaction = Transaction::new();
        // TODO: Fix this hack; move to object_store code.
        transaction.add(
            root_store.store_object_id(),
            Mutation::Insert {
                item: ObjectItem {
                    key: ObjectKey::attribute(SUPER_BLOCK_OBJECT_ID, 0),
                    value: ObjectValue::attribute(MIN_SUPER_BLOCK_SIZE),
                },
            },
        );
        transaction.add(
            root_store.store_object_id(),
            Mutation::ReplaceExtent {
                item: ObjectItem {
                    key: ObjectKey::extent(
                        SUPER_BLOCK_OBJECT_ID,
                        ExtentKey::new(0, 0..MIN_SUPER_BLOCK_SIZE),
                    ),
                    value: ObjectValue::extent(0),
                },
            },
        );
        transaction.add(
            root_store.store_object_id(),
            Mutation::Insert {
                item: ObjectItem {
                    key: ObjectKey::attribute(1, 0), // TODO: Fix constant
                    value: ObjectValue::attribute(0),
                },
            },
        );
        let log_handle =
            Box::new(stores.root_store().create_object(&mut transaction, log_handle_options())?);
        self.commit(transaction);
        println!("log object id {:?}", log_handle.object_id());
        let mut transaction = Transaction::new();
        log_handle.preallocate_range(0..self.file_chunk_size(), &mut transaction).unwrap(); // TODO
        self.commit(transaction);

        // Fill in the missing details.
        let mut log = self.log();
        let super_block = &mut log.as_mut().unwrap().super_block;
        super_block.root_store_object_id = stores.root_store().store_object_id();
        super_block.allocator_object_id = allocator.object_id();
        super_block.log_object_id = log_handle.object_id();
        super_block.log_checkpoint = LogCheckpoint::new(0, starting_check_sum);
        log.as_mut().unwrap().writer.handle = Some(log_handle);
        Ok(())
    }

    pub fn begin_object_sync(&self, object_id: u64) -> ObjectSync<'_> {
        ObjectSync {
            log: self,
            object_id,
            old_log_file_checkpoint: self
                .log()
                .as_mut()
                .unwrap()
                .log_file_checkpoints
                .remove(&object_id),
        }
    }

    fn log(&self) -> MutexGuard<'_, Option<InitializedLog>> {
        self.0.lock().unwrap()
    }

    fn flush_if_first_super_block(&self) -> Result<(), Error> {
        let lock = self.log();
        let log = lock.as_ref().unwrap();
        if !log.needs_super_block {
            return Ok(());
        }
        let root_store = log.stores.root_store();
        let allocator = log.allocator.clone();
        std::mem::drop(lock);
        // For the very first super-block, we have to flush the root store because the first
        // extents for the log file *must* be on-disk rather than in memory as otherwise we
        // wouldn't be able to replay the log.
        root_store.flush(true)?;
        // For now, the allocator needs to exist. In theory, we could change this so it wasn't
        // necessary, since all information could exist in the log, but then we'd need something
        // to indicate whether or not it had been flushed. Perhaps we should consider storing
        // all the log file offsets separately from the super-block? This most likely is not an
        // optimisation we should care about; there'll be an allocator file pretty quickly.
        allocator.flush(true)
    }

    pub fn sync(&self, options: SyncOptions) -> Result<(), Error> {
        self.flush_if_first_super_block()?;
        self.log().as_mut().unwrap().sync(options)
    }

    pub fn commit(&self, transaction: Transaction) {
        self.log().as_mut().unwrap().commit(transaction, None);
    }

    pub fn register_store(&self, store: &Arc<ObjectStore>) {
        self.log().as_ref().unwrap().stores.new_store(store);
    }
}

pub struct InitializedLog {
    allocator: Arc<dyn Allocator>,
    stores: Arc<StoreManager>,
    writer: LogWriter,
    needs_super_block: bool,
    super_block: SuperBlock,
    // Records dependencies on the log for objects i.e. an entry for object ID 1, would mean it has
    // a dependency on log records from that offset.
    log_file_checkpoints: HashMap<u64, LogCheckpoint>,
}

impl LogProperties for InitializedLog {}

impl InitializedLog {
    fn maybe_extend_log_file(&mut self, writer: Option<&mut LogWriter>) {
        let file_chunk_size = self.file_chunk_size();
        let (writer, second_writer) = match writer {
            None => (&mut self.writer, None),
            Some(writer) => (writer, Some(&mut self.writer)),
        };
        // TODO: what if it needs to grow by more than file_chunk_size()?
        let file_offset = writer.log_file_checkpoint().file_offset;
        let handle = match writer.handle {
            None => return,
            Some(ref mut handle) => handle,
        };
        let size = handle.get_size();
        if file_offset + file_chunk_size <= size {
            return;
        }
        let mut transaction = Transaction::new();
        let allocated =
            handle.preallocate_range(size..size + file_chunk_size, &mut transaction).unwrap(); // TODO
        let object_id = handle.object_id();
        let log_file_checkpoint = second_writer.unwrap_or(writer).write_transaction(&transaction);

        // We need to be sure that any log records that arose from preallocation can fit in
        // within the old preallocated range. TODO: if this situation arose (it shouldn't),
        // then it could be fixed by forcing a sync of the root store.
        assert!(writer.log_file_checkpoint().file_offset <= size);
        let file_offset = writer.log_file_checkpoint().file_offset;
        let handle = writer.handle.as_ref().unwrap();
        assert!(file_offset + file_chunk_size <= handle.get_size());
        if object_id == SUPER_BLOCK_OBJECT_ID {
            for dev_range in allocated.iter() {
                let extent = Extent {
                    range: size..size + range_len(dev_range),
                    device_offset: dev_range.start, // TODO: Consider swapping device extent for file extent.
                };
                serialize_into(&mut *writer, &LogRecord::AddExtent(extent)).unwrap();
                // TODO
            }
        }
        self.apply_mutations(transaction, log_file_checkpoint);
    }

    fn commit(&mut self, mut transaction: Transaction, mut writer: Option<&mut LogWriter>) {
        self.maybe_extend_log_file(writer.as_mut().map(|x| &mut **x));
        let log_file_checkpoint = writer
            .as_mut()
            .map(|x| &mut **x)
            .unwrap_or(&mut self.writer)
            .write_transaction(&mut transaction);
        self.maybe_extend_log_file(writer);
        self.apply_mutations(transaction, log_file_checkpoint);
    }

    fn apply_mutations(
        &mut self,
        mut transaction: Transaction,
        log_file_checkpoint: LogCheckpoint,
    ) {
        transaction
            .mutations
            .drain(..)
            .for_each(|m| self.apply_mutation(m.0, &log_file_checkpoint, m.1, None));
    }

    fn should_apply(
        &mut self,
        object_id: u64,
        log_file_checkpoint: &LogCheckpoint,
        log_file_offsets: Option<&HashMap<u64, u64>>,
    ) -> bool {
        if let Some(&offset) = log_file_offsets.and_then(|hash| hash.get(&object_id)) {
            if offset < log_file_checkpoint.file_offset {
                return false;
            }
        }
        self.log_file_checkpoints.entry(object_id).or_insert_with(|| log_file_checkpoint.clone());
        return true;
    }

    fn get_store(&self, store_object_id: u64) -> Arc<ObjectStore> {
        if let Some(store) = self.stores.store(store_object_id) {
            return store;
        }
        let store =
            self.stores.root_store().lazy_open_store(store_object_id, StoreOptions::default());
        self.stores.new_store(&store);
        store
    }

    fn apply_mutation(
        &mut self,
        object_id: u64,
        log_file_checkpoint: &LogCheckpoint,
        mutation: Mutation,
        log_file_offsets: Option<&HashMap<u64, u64>>,
    ) {
        match mutation {
            Mutation::Insert { item } => {
                if self.should_apply(object_id, log_file_checkpoint, log_file_offsets) {
                    self.get_store(object_id).insert(item);
                }
            }
            Mutation::ReplaceExtent { item } => {
                if self.should_apply(
                    self.super_block.allocator_object_id,
                    log_file_checkpoint,
                    log_file_offsets,
                ) {
                    let (object_id, insert_key, insert_value) =
                        decode_extent(item.as_item_ref()).unwrap();
                    self.allocator.commit_allocation(
                        object_id,
                        insert_key.attribute_id,
                        insert_value.device_offset
                            ..insert_value.device_offset + insert_key.range.end
                                - insert_key.range.start,
                        insert_key.range.start,
                    );
                }
                if self.should_apply(object_id, log_file_checkpoint, log_file_offsets) {
                    self.get_store(object_id).replace_extent(item);
                }
            }
            Mutation::ReplaceOrInsert { item } => {
                if self.should_apply(object_id, log_file_checkpoint, log_file_offsets) {
                    self.get_store(object_id).replace_or_insert(item);
                }
            }
            Mutation::Deallocate(item) => {
                if self.should_apply(object_id, log_file_checkpoint, log_file_offsets) {
                    self.allocator.commit_deallocation(item);
                }
            }
        }
    }

    fn write_super_block(&mut self, log_file_checkpoint: LogCheckpoint) -> Result<(), Error> {
        println!("write_super_block");
        let root_parent_store = self.stores.root_parent_store();
        let root_store = self.stores.root_store();
        let mut super_block_writer = LogWriter::new(
            Some(Box::new(root_store.open_object(SUPER_BLOCK_OBJECT_ID, log_handle_options())?)),
            0,
        );
        let mut super_block = &mut self.super_block;
        super_block.super_block_log_file_offset = log_file_checkpoint.file_offset;
        // Find the minimum log offset required.
        {
            let offsets = &mut self.log_file_checkpoints;
            offsets.remove(&ROOT_PARENT_STORE_OBJECT_ID);
            let min_checkpoint = offsets
                .values()
                .min_by(|x, y| x.file_offset.cmp(&y.file_offset))
                .unwrap_or(&log_file_checkpoint);
            super_block.log_checkpoint = min_checkpoint.clone();
            // TODO: deallocate log file extents here.
        }
        serialize_into(&mut super_block_writer, &*super_block)?;
        super_block_writer.pad_to_block()?;
        let tree = root_parent_store.tree();
        let mut iter = tree.iter();
        // TODO: need to write AddExtent records for existing super block extents or
        // need to delete extents (probably not).  Also need to do A/B copies of the
        // super block.
        iter.advance()?;
        while let Some(item_ref) = iter.get() {
            self.maybe_extend_log_file(Some(&mut super_block_writer));
            serialize_into(
                &mut super_block_writer,
                &LogRecord::Mutation {
                    object_id: ROOT_PARENT_STORE_OBJECT_ID,
                    mutation: Mutation::Insert {
                        item: ObjectItem {
                            key: (*item_ref.key).clone(),
                            value: (*item_ref.value).clone(),
                        },
                    },
                },
            )?;
            iter.advance()?;
        }
        serialize_into(&mut super_block_writer, &LogRecord::Commit)?;
        super_block_writer.pad_to_block()?;
        println!("done writing super block");
        Ok(())
    }

    pub fn sync(&mut self, options: SyncOptions) -> Result<(), Error> {
        if options.new_super_block || self.needs_super_block {
            let log_file_checkpoint = self.writer.log_file_checkpoint();
            self.write_super_block(log_file_checkpoint)?;
            self.needs_super_block = false;
        }
        serialize_into(&mut self.writer, &LogRecord::EndBlock)?;
        self.writer.pad_to_block()?;
        Ok(())
    }
}

#[must_use]
pub struct ObjectSync<'sync> {
    log: &'sync Log,
    object_id: u64,
    old_log_file_checkpoint: Option<LogCheckpoint>,
}

impl ObjectSync<'_> {
    pub fn needs_sync(&self) -> bool {
        self.old_log_file_checkpoint.is_some()
    }

    pub fn done(&mut self) {
        self.old_log_file_checkpoint = None;
    }
}

impl Drop for ObjectSync<'_> {
    fn drop(&mut self) {
        // Assume failure and revert.
        if let Some(checkpoint) = self.old_log_file_checkpoint.take() {
            self.log
                .log()
                .as_mut()
                .unwrap()
                .log_file_checkpoints
                .insert(self.object_id, checkpoint);
        }
    }
}

impl LogReaderHandle for StoreObjectHandle {
    fn as_object_handle(self: Box<Self>) -> Box<dyn ObjectHandle> {
        self
    }

    fn add_extent(&mut self, _extent: Extent) {
        // This should never happen. Just ignore it.
    }
}

impl LogProperties for LogReader {}

struct LogReader {
    handle: Box<dyn LogReaderHandle>,
    buf_offset: usize,
    read_offset: u64,
    buf: Vec<u8>,
    last_check_sum: u64,
    last_read_check_sum: u64,
    bad_check_sum: bool,
    found_reset: bool,
}

impl LogReader {
    fn new(handle: Box<dyn LogReaderHandle>) -> LogReader {
        LogReader {
            handle,
            buf_offset: 0,
            read_offset: 0,
            buf: Vec::new(),
            last_check_sum: 0,
            last_read_check_sum: 0,
            bad_check_sum: false,
            found_reset: false,
        }
    }

    fn log_file_checkpoint(&self) -> LogCheckpoint {
        if self.buf_offset + 8 >= self.buf.len() {
            LogCheckpoint::new(self.read_offset, self.last_check_sum)
        } else {
            assert!(self.read_offset % self.block_size() == 0);
            LogCheckpoint::new(
                self.read_offset - self.block_size() + self.buf_offset as u64,
                self.last_check_sum,
            )
        }
    }

    fn skip_to_end_of_block(&mut self) {
        self.buf_offset = self.buf.len();
    }

    fn reset(&mut self) {
        assert!(self.found_reset && self.bad_check_sum);
        self.found_reset = false;
        self.bad_check_sum = false;
        self.buf_offset = 0;
        self.last_check_sum = self.last_read_check_sum;
    }
}

impl std::io::Read for LogReader {
    fn read(&mut self, buf: &mut [u8]) -> std::io::Result<usize> {
        // println!("log reading {:?} @ {:?}", buf.len(), self.buf_offset);
        assert!(buf.len() < self.block_size() as usize - 8); // TODO
        let mut offset = 0;
        let bs = self.block_size() as usize;
        // TODO: Fix this to read more than one block maybe?
        while offset < buf.len() {
            if self.buf_offset + 8 >= self.buf.len() {
                if self.bad_check_sum {
                    return Ok(offset);
                }
                let align = self.read_offset % self.block_size();
                self.buf.resize(bs, 0);
                self.handle.read(self.read_offset - align, &mut self.buf)?;
                let end = bs - 8;
                let stored_check_sum = LittleEndian::read_u64(&self.buf[end..]);
                let computed_check_sum = fletcher64(&self.buf[..end], self.last_read_check_sum);
                if stored_check_sum != computed_check_sum {
                    // println!("bad check sum");
                    self.bad_check_sum = true;
                    if stored_check_sum ^ RESET_XOR == computed_check_sum && align == 0 {
                        // Record that we've encountered a reset in the stream (a point where the
                        // log wasn't cleanly closed in the past) and it starts afresh in this
                        // block.
                        self.found_reset = true;
                        self.last_read_check_sum = stored_check_sum;
                        self.read_offset += self.block_size() - align;
                    }
                    return Ok(offset);
                } else {
                    self.last_read_check_sum = stored_check_sum;
                    self.read_offset += self.block_size() - align;
                    self.buf_offset = align as usize;
                }
            }
            let to_copy = min(buf.len() - offset, bs - 8 - self.buf_offset);
            buf[offset..offset + to_copy]
                .copy_from_slice(&self.buf[self.buf_offset..self.buf_offset + to_copy]);
            self.buf_offset += to_copy;
            offset += to_copy;
            if self.buf_offset + 8 == bs {
                self.buf_offset += 8;
                self.last_check_sum = self.last_read_check_sum;
            }
        }
        return Ok(offset);
    }
}
