mod allocator;
mod constants;
mod directory;
pub mod filesystem;
mod log;
mod merge;
mod record;

pub use directory::Directory;
pub use filesystem::Filesystem;
pub use log::Transaction;
pub use record::ObjectType;

#[cfg(test)]
mod tests;

use {
    crate::{
        lsm_tree::{ItemRef, LSMTree},
        object_handle::{ObjectHandle, ObjectHandleCursor},
    },
    allocator::Allocator,
    anyhow::Error,
    bincode::{deserialize_from, serialize_into},
    log::{Log, Mutation},
    record::{decode_extent, ExtentKey, ObjectItem, ObjectKey, ObjectKeyData, ObjectValue},
    serde::{Deserialize, Serialize},
    std::{
        cmp::min,
        io::{BufWriter, ErrorKind, Write},
        ops::{Bound, Range},
        sync::{Arc, Mutex},
    },
};

#[derive(Default)]
pub struct HandleOptions {
    // If true, don't COW, write to blocks that are already allocated.
    pub overwrite: bool,
}

#[derive(Default)]
pub struct StoreOptions {
    use_parent_to_allocate_object_ids: bool,
}

pub trait Device: Send + Sync {
    fn block_size(&self) -> u64;
    fn read(&self, offset: u64, buf: &mut [u8]) -> std::io::Result<()>;
    fn write(&self, offset: u64, buf: &[u8]) -> std::io::Result<()>;
}

pub struct StoreObjectHandle {
    store: Arc<ObjectStore>,
    block_size: u64,
    object_id: u64,
    attribute_id: u64,
    size: Mutex<u64>,
    options: HandleOptions,
}

// TODO: Make async, or maybe better to make device take array of operations.
impl StoreObjectHandle {
    fn write_at(&self, offset: u64, buf: &[u8], mut device_offset: u64) -> std::io::Result<()> {
        // Deal with alignment.
        let start_align = (offset % self.block_size) as usize;
        let start_offset = offset - start_align as u64;
        let remainder = if start_align > 0 {
            let (head, remainder) =
                buf.split_at(min(self.block_size as usize - start_align, buf.len()));
            let mut align_buf = vec![0; self.block_size as usize];
            self.read(start_offset, align_buf.as_mut_slice())?;
            &mut align_buf[start_align..(start_align + head.len())].copy_from_slice(head);
            self.store.device.write(device_offset, &align_buf)?;
            device_offset += self.block_size;
            remainder
        } else {
            buf
        };
        if remainder.len() > 0 {
            let end = offset + buf.len() as u64;
            let end_align = (end % self.block_size) as usize;
            let (whole_blocks, tail) = remainder.split_at(remainder.len() - end_align);
            self.store.device.write(device_offset, whole_blocks)?;
            device_offset += whole_blocks.len() as u64;
            if tail.len() > 0 {
                let mut align_buf = vec![0; self.block_size as usize];
                self.read(end - end_align as u64, align_buf.as_mut_slice())?;
                align_buf[..tail.len()].copy_from_slice(tail);
                self.store.device.write(device_offset, &align_buf)?;
            }
        }
        Ok(())
    }

    fn delete_old_extents(
        &self,
        transaction: &mut Transaction,
        key: &ExtentKey,
    ) -> std::io::Result<()> {
        // Delete old extents.  TODO: this should turn into an asynchronous task, that
        // blocks flushing this object store until completed. For that, we need the log
        // checkpoint. To make it work properly, we would need to replace in the mutable
        // layer and free extents there synchronously, and then do the extents in the other
        // layers asynchronously.
        // TODO: need to check allocator checkpoint.

        // We can't trigger work that might depend on state *before* the transaction because we
        // might lose information if we do a flush. So, we have to either do all the lookups up
        // front and queue up the changes, or we must queue up the changes in a different
        // transaction via some async mechanism.
        let tree = &self.store.tree;
        let lower_bound = ObjectKey::extent(self.object_id, key.lower_bound());
        let mut iter = tree.range_from(Bound::Included(&lower_bound)).map_err(map_to_io_error)?;
        while let Some((oid, extent_key, extent_value)) = iter.get().and_then(decode_extent) {
            if oid != self.object_id {
                break;
            }
            if let Some(overlap) = key.overlap(extent_key) {
                self.store.allocator.deallocate(
                    transaction,
                    self.object_id,
                    key.attribute_id,
                    extent_value.device_offset + overlap.start - extent_key.range.start
                        ..extent_value.device_offset + overlap.end - extent_key.range.start,
                    overlap.start,
                );
            } else {
                break;
            }
            iter.advance().unwrap();
        }
        Ok(())
    }
}

pub fn map_to_io_error(error: Error) -> std::io::Error {
    std::io::Error::new(ErrorKind::Other, error)
}

fn round_down(offset: u64, block_size: u64) -> u64 {
    offset - offset % block_size
}

fn round_up(offset: u64, block_size: u64) -> u64 {
    round_down(offset + block_size - 1, block_size)
}

impl ObjectHandle for StoreObjectHandle {
    fn object_id(&self) -> u64 {
        return self.object_id;
    }

    fn read(&self, mut offset: u64, buf: &mut [u8]) -> std::io::Result<usize> {
        // println!("{:?} reading {:?} @ {:?}", self.object_id, buf.len(), offset);
        // TODO: out of range offset
        if buf.len() == 0 {
            return Ok(0);
        }
        let tree = &self.store.tree;
        let mut merger = tree
            .range_from(Bound::Included(&ObjectKey::extent(
                self.object_id,
                ExtentKey::new(self.attribute_id, 0..offset + 1),
            )))
            .map_err(map_to_io_error)?;
        let mut buf_offset = 0;
        let to_do = min(buf.len() as u64, *self.size.lock().unwrap() - offset) as usize;
        // TODO: Should sparse be explicit or just absent records?
        loop {
            match merger.get() {
                Some(ItemRef {
                    key: ObjectKey { object_id, data: ObjectKeyData::Extent(extent_key) },
                    value: ObjectValue::Extent(extent_value),
                }) => {
                    // TODO: check attribute_id
                    if *object_id != self.object_id {
                        break;
                    }
                    if extent_key.range.start > offset {
                        let to_zero =
                            min((extent_key.range.start - offset) as usize, to_do - buf_offset);
                        for i in 0..to_zero {
                            buf[buf_offset + i] = 0;
                        }
                        buf_offset += to_zero;
                        offset += to_zero as u64;
                    }
                    let start_align = (offset % self.block_size) as usize;
                    let mut device_offset = extent_value.device_offset
                        + (offset - start_align as u64 - extent_key.range.start);
                    // Deal with starting alignment.
                    if start_align > 0 {
                        let mut align_buf = vec![0; self.block_size as usize];
                        self.store.device.read(device_offset, &mut align_buf)?;
                        let to_copy =
                            min(self.block_size as usize - start_align, to_do - buf_offset);
                        buf[buf_offset..buf_offset + to_copy]
                            .copy_from_slice(&mut align_buf[start_align..(start_align + to_copy)]);
                        buf_offset += to_copy;
                        if buf_offset >= to_do {
                            break;
                        }
                        offset += to_copy as u64;
                        device_offset += self.block_size;
                    }
                    let end_align = to_do % self.block_size as usize;
                    let to_copy = min(to_do - end_align, (extent_key.range.end - offset) as usize);
                    if to_copy > 0 {
                        self.store
                            .device
                            .read(device_offset, &mut buf[buf_offset..(buf_offset + to_copy)])?;
                        buf_offset += to_copy;
                        offset += to_copy as u64;
                        device_offset += to_copy as u64;
                    }
                    // Deal with end alignment.
                    if offset < extent_key.range.end && end_align > 0 {
                        let mut align_buf = vec![0; self.block_size as usize];
                        self.store.device.read(device_offset, &mut align_buf)?;
                        buf[buf_offset..to_do].copy_from_slice(&align_buf[..end_align]);
                        buf_offset += end_align;
                        break;
                    }
                }
                _ => break,
            }
            if buf_offset == to_do {
                break;
            }
            merger
                .advance_to(&ObjectKey::extent(
                    self.object_id,
                    ExtentKey::new(self.attribute_id, offset..std::u64::MAX),
                ))
                .map_err(map_to_io_error)?;
        }
        // Zero out anything remaining.
        for i in buf_offset..to_do {
            buf[i] = 0;
        }
        Ok(to_do)
    }

    fn write(&self, mut offset: u64, buf: &[u8]) -> std::io::Result<()> {
        if self.options.overwrite {
            let mut buf_offset = 0;
            let tree = &self.store.tree;
            let mut merger = tree
                .range_from(Bound::Included(&ObjectKey::extent(
                    self.object_id,
                    ExtentKey::new(self.attribute_id, 0..(offset + 1)),
                )))
                .map_err(map_to_io_error)?;
            while buf_offset < buf.len() {
                match merger.get() {
                    Some(ItemRef {
                        key: ObjectKey { object_id, data: ObjectKeyData::Extent(extent_key) },
                        value: ObjectValue::Extent(extent_value),
                    }) => {
                        if *object_id != self.object_id {
                            panic!("No extent!"); // TODO
                        }
                        let buf_end =
                            min(buf.len(), buf_offset + (extent_key.range.end - offset) as usize);
                        self.write_at(
                            offset,
                            &buf[buf_offset..buf_end],
                            extent_value.device_offset + (offset - extent_key.range.start),
                        )?;
                        buf_offset = buf_end;
                        offset = extent_key.range.end;
                    }
                    _ => panic!("No extent!"),
                }
                merger
                    .advance_to(&ObjectKey::extent(
                        self.object_id,
                        ExtentKey::new(self.attribute_id, offset..std::u64::MAX),
                    ))
                    .map_err(map_to_io_error)?;
            }
        } else {
            let mut aligned = round_down(offset, self.block_size)
                ..round_up(offset + buf.len() as u64, self.block_size);
            let mut buf_offset = 0;
            let mut transaction = Transaction::new(); // TODO: transaction too big?
            if offset + buf.len() as u64 > *self.size.lock().unwrap() {
                // TODO: need to hold locks properly
                *self.size.lock().unwrap() = offset + buf.len() as u64;
                transaction.add(
                    self.store.store_object_id,
                    Mutation::ReplaceOrInsert {
                        item: ObjectItem {
                            key: ObjectKey::attribute(self.object_id, 0),
                            value: ObjectValue::attribute(*self.size.lock().unwrap()),
                        },
                    },
                );
            }
            self.delete_old_extents(
                &mut transaction,
                &ExtentKey::new(self.attribute_id, aligned.clone()),
            )?;
            while buf_offset < buf.len() {
                let device_range = self
                    .store
                    .allocator
                    .allocate(self.object_id, 0, aligned.clone())
                    .map_err(map_to_io_error)?;
                let extent_len = device_range.end - device_range.start;
                let end = aligned.start + extent_len;
                let len = min(buf.len() - buf_offset, (aligned.end - offset) as usize);
                assert!(len > 0);
                self.write_at(offset, &buf[buf_offset..buf_offset + len], device_range.start)?;
                transaction.add(
                    self.store.store_object_id,
                    Mutation::ReplaceExtent {
                        item: ObjectItem {
                            key: ObjectKey::extent(
                                self.object_id,
                                ExtentKey::new(self.attribute_id, aligned.start..end),
                            ),
                            value: ObjectValue::extent(device_range.start),
                        },
                    },
                );
                aligned.start += extent_len;
                buf_offset += len;
                offset += len as u64;
            }
            self.store.log.commit(transaction);
        }
        Ok(())
    }

    // Must be multiple of block size.
    fn preallocate_range(
        &self,
        mut file_range: Range<u64>,
        transaction: &mut Transaction,
    ) -> std::io::Result<Vec<Range<u64>>> {
        // TODO: add checks on length, etc.
        let mut ranges = Vec::new();
        while file_range.start < file_range.end {
            let device_range = self
                .store
                .allocator
                .allocate(self.object_id, 0, file_range.clone())
                .map_err(map_to_io_error)?;
            let this_file_range =
                file_range.start..file_range.start + device_range.end - device_range.start;
            self.delete_old_extents(
                transaction,
                &ExtentKey::new(self.attribute_id, this_file_range.clone()),
            )?;
            file_range.start = this_file_range.end;
            transaction.add(
                self.store.store_object_id,
                Mutation::ReplaceExtent {
                    item: ObjectItem {
                        key: ObjectKey::extent(
                            self.object_id,
                            ExtentKey::new(self.attribute_id, this_file_range),
                        ),
                        value: ObjectValue::extent(device_range.start),
                    },
                },
            );
            ranges.push(device_range);
        }
        if file_range.end > *self.size.lock().unwrap() {
            *self.size.lock().unwrap() = file_range.end;
            transaction.add(
                self.store.store_object_id,
                Mutation::ReplaceOrInsert {
                    item: ObjectItem {
                        key: ObjectKey::attribute(self.object_id, 0),
                        value: ObjectValue::attribute(*self.size.lock().unwrap()),
                    },
                },
            );
        }
        Ok(ranges)
    }

    fn get_size(&self) -> u64 {
        *self.size.lock().unwrap()
    }
}

#[derive(Clone, Default, Serialize, Deserialize)]
pub struct StoreInfo {
    // The last used object ID.
    last_object_id: u64,

    // Object ids for layers.  TODO: need a layer of indirection here so we can
    // support snapshots.
    layers: Vec<u64>,
}

impl StoreInfo {
    fn new() -> StoreInfo {
        StoreInfo { last_object_id: 0, layers: Vec::new() }
    }
}

pub struct ObjectStore {
    parent_store: Option<Arc<ObjectStore>>,
    store_object_id: u64,
    device: Arc<dyn Device>,
    block_size: u64,
    allocator: Arc<dyn Allocator>,
    log: Arc<Log>,
    options: StoreOptions,
    store_info: Mutex<StoreInfo>,
    tree: LSMTree<ObjectKey, ObjectValue>,
}

impl ObjectStore {
    fn new(
        parent_store: Option<Arc<ObjectStore>>,
        store_object_id: u64,
        device: Arc<dyn Device>,
        allocator: Arc<dyn Allocator>,
        log: Arc<Log>,
        store_info: StoreInfo,
        tree: LSMTree<ObjectKey, ObjectValue>,
        options: StoreOptions,
    ) -> Arc<ObjectStore> {
        Arc::new(ObjectStore {
            parent_store,
            store_object_id,
            device: device.clone(),
            block_size: device.block_size(),
            allocator,
            log,
            options,
            store_info: Mutex::new(store_info),
            tree,
        })
    }

    pub fn new_empty(
        parent_store: Option<Arc<ObjectStore>>,
        store_object_id: u64,
        device: Arc<dyn Device>,
        allocator: Arc<dyn Allocator>,
        log: Arc<Log>,
        options: StoreOptions,
    ) -> Arc<Self> {
        Self::new(
            parent_store,
            store_object_id,
            device,
            allocator,
            log,
            StoreInfo::new(),
            LSMTree::new(merge::merge),
            options,
        )
    }

    pub fn log(&self) -> &Log {
        &self.log
    }

    pub fn create_child_store(
        parent_store: Arc<ObjectStore>,
        options: StoreOptions,
    ) -> Result<Arc<ObjectStore>, Error> {
        // TODO: This should probably all be in a transaction. There should probably be a log
        // record to create a store.
        let mut transaction = Transaction::new();
        let handle =
            parent_store.clone().create_object(&mut transaction, HandleOptions::default())?;
        parent_store.log.commit(transaction);
        Ok(Self::new_empty(
            Some(parent_store.clone()),
            handle.object_id(),
            parent_store.device.clone(),
            parent_store.allocator.clone(),
            parent_store.log.clone(),
            options,
        ))
    }

    pub fn open_store(
        self: &Arc<ObjectStore>,
        store_object_id: u64,
        options: StoreOptions,
    ) -> Result<Arc<ObjectStore>, Error> {
        println!("opening handle");
        let handle = self.clone().open_object(store_object_id, HandleOptions::default())?;
        println!("deserializing");
        let store_info: StoreInfo =
            deserialize_from(ObjectHandleCursor::new(&handle as &dyn ObjectHandle, 0))?;
        println!("opening handles");
        let mut handles = Vec::new();
        for object_id in &store_info.layers {
            handles.push(self.clone().open_object(*object_id, HandleOptions::default())?);
        }
        if options.use_parent_to_allocate_object_ids {
            if store_info.last_object_id > self.store_info.lock().unwrap().last_object_id {
                self.store_info.lock().unwrap().last_object_id = store_info.last_object_id;
            }
        }
        Ok(Self::new(
            Some(self.clone()),
            store_object_id,
            self.device.clone(),
            self.allocator.clone(),
            self.log.clone(),
            store_info,
            LSMTree::open(merge::merge, handles.into_boxed_slice()),
            StoreOptions::default(),
        ))
    }

    pub fn store_object_id(&self) -> u64 {
        self.store_object_id
    }

    pub fn open_object(
        self: &Arc<Self>,
        object_id: u64,
        options: HandleOptions,
    ) -> std::io::Result<StoreObjectHandle> {
        let item = self
            .tree
            .find(&ObjectKey::attribute(object_id, 0))
            .map_err(map_to_io_error)?
            .ok_or(std::io::Error::new(ErrorKind::NotFound, "Not found"))?;
        if let ObjectValue::Attribute { size } = item.value {
            Ok(StoreObjectHandle {
                store: self.clone(),
                object_id: object_id,
                attribute_id: 0,
                block_size: self.block_size,
                size: Mutex::new(size),
                options,
            })
        } else {
            Err(std::io::Error::new(ErrorKind::InvalidData, "Expected attribute value"))
        }
    }

    pub fn create_object_with_id(
        self: &Arc<Self>,
        transaction: &mut Transaction,
        object_id: u64,
        options: HandleOptions,
    ) -> std::io::Result<StoreObjectHandle> {
        transaction.add(
            self.store_object_id,
            Mutation::Insert {
                item: ObjectItem {
                    key: ObjectKey::object(object_id),
                    value: ObjectValue::object(ObjectType::File),
                },
            },
        );
        transaction.add(
            self.store_object_id,
            Mutation::Insert {
                item: ObjectItem {
                    key: ObjectKey::attribute(object_id, 0),
                    value: ObjectValue::attribute(0),
                },
            },
        );
        Ok(StoreObjectHandle {
            store: self.clone(),
            block_size: self.block_size,
            object_id,
            attribute_id: 0,
            size: Mutex::new(0),
            options,
        })
    }

    pub fn create_directory(self: &Arc<Self>) -> std::io::Result<directory::Directory> {
        let object_id = self.get_next_object_id();
        let mut transaction = Transaction::new();
        transaction.add(
            self.store_object_id,
            Mutation::Insert {
                item: ObjectItem {
                    key: ObjectKey::object(object_id),
                    value: ObjectValue::object(ObjectType::Directory),
                },
            },
        );
        self.log.commit(transaction);
        Ok(directory::Directory::new(self.clone(), object_id))
    }

    pub fn open_directory(
        self: &Arc<Self>,
        object_id: u64,
    ) -> std::io::Result<directory::Directory> {
        let item = self
            .tree
            .find(&ObjectKey::object(object_id))
            .map_err(map_to_io_error)?
            .ok_or(std::io::Error::new(ErrorKind::NotFound, "Not found"))?;
        if let ObjectValue::Object { object_type: ObjectType::Directory } = item.value {
            Ok(directory::Directory::new(self.clone(), object_id))
        } else {
            Err(std::io::Error::new(ErrorKind::InvalidData, "Expected directory"))
        }
    }

    fn store_info_for_last_object_id(&self) -> &Mutex<StoreInfo> {
        if self.options.use_parent_to_allocate_object_ids {
            &self.parent_store.as_ref().unwrap().store_info
        } else {
            &self.store_info
        }
    }

    fn get_next_object_id(&self) -> u64 {
        let mut store_info = self.store_info_for_last_object_id().lock().unwrap();
        store_info.last_object_id += 1;
        store_info.last_object_id
    }

    pub fn create_object(
        self: &Arc<Self>,
        mut transaction: &mut Transaction,
        options: HandleOptions,
    ) -> std::io::Result<StoreObjectHandle> {
        let object_id = self.get_next_object_id();
        self.create_object_with_id(&mut transaction, object_id, options)
    }

    pub fn tree(&self) -> &LSMTree<ObjectKey, ObjectValue> {
        &self.tree
    }

    // Push all in-memory structures to the device. This is not necessary for sync since the log
    // will take care of it. This will panic if called on the root parent store.
    pub fn flush(&self, force: bool) -> Result<(), Error> {
        let mut object_sync = self.log.begin_object_sync(self.store_object_id);
        if !force && !object_sync.needs_sync() {
            return Ok(());
        }
        let parent_store = self.parent_store.as_ref().unwrap();
        let mut transaction = Transaction::new();
        let object_handle =
            parent_store.clone().create_object(&mut transaction, HandleOptions::default())?;
        self.log.commit(transaction); // This needs to encompass all the following.
        let object_id = object_handle.object_id();
        let handle =
            parent_store.clone().open_object(self.store_object_id, HandleOptions::default())?;
        self.tree.commit(object_handle)?;
        let mut store_info = self.store_info.lock().unwrap();
        // TODO: get layers from tree.
        store_info.layers = vec![object_id];
        if self.options.use_parent_to_allocate_object_ids {
            store_info.last_object_id =
                self.parent_store.as_ref().unwrap().store_info.lock().unwrap().last_object_id;
        }
        // TODO: truncate file in same transaction.
        let mut writer = BufWriter::new(ObjectHandleCursor::new(&handle as &dyn ObjectHandle, 0));
        serialize_into(&mut writer, &*store_info)?;
        writer.flush()?;
        object_sync.done();
        Ok(())
    }

    // -- Methods only to be called by Log --
    pub fn insert(&self, item: ObjectItem) {
        let store_info = self.store_info_for_last_object_id();
        if item.key.object_id > store_info.lock().unwrap().last_object_id {
            store_info.lock().unwrap().last_object_id = item.key.object_id;
        }
        self.tree.insert(item);
    }

    pub fn replace_extent(&self, item: ObjectItem) {
        let lower_bound = item.key.lower_bound();
        self.tree.replace_range(item, &lower_bound);
    }

    pub fn replace_or_insert(&self, item: ObjectItem) {
        self.tree.replace_or_insert(item);
    }
}

// TODO: validation of all deserialized structs.
