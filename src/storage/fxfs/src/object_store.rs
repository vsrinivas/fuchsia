// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod allocator;
mod constants;
pub mod directory;
pub mod filesystem;
mod journal;
mod merge;
mod record;
#[cfg(test)]
mod testing;
pub mod transaction;

pub use constants::INVALID_OBJECT_ID;
pub use directory::Directory;
pub use filesystem::FxFilesystem;
pub use record::ObjectDescriptor;

use {
    crate::{
        device::{
            buffer::{Buffer, BufferRef, MutableBufferRef},
            Device,
        },
        errors::FxfsError,
        lsm_tree::{types::LayerIterator, LSMTree},
        object_handle::{ObjectHandle, ObjectHandleExt},
        object_store::{
            filesystem::{Filesystem, Mutations},
            record::DEFAULT_DATA_ATTRIBUTE_ID,
            record::{ExtentKey, ExtentValue, ObjectItem, ObjectKey, ObjectKeyData, ObjectValue},
            transaction::{
                AssociatedObject, LockKey, Mutation, ObjectStoreMutation, Operation, Transaction,
            },
        },
    },
    allocator::Allocator,
    anyhow::{anyhow, bail, Error},
    async_trait::async_trait,
    bincode::{deserialize_from, serialize_into},
    futures::{future::BoxFuture, FutureExt},
    serde::{Deserialize, Serialize},
    std::{
        cmp::min,
        ops::{Bound, Range},
        sync::{Arc, Mutex, Weak},
    },
};

// StoreInfo stores information about the object store.  This is stored within the parent object
// store, and is used, for example, to get the persistent layer objects.
#[derive(Clone, Debug, Default, Serialize, Deserialize)]
pub struct StoreInfo {
    // The last used object ID.
    last_object_id: u64,

    // Object ids for layers.  TODO(csuter): need a layer of indirection here so we can
    // support snapshots.
    layers: Vec<u64>,
}

// TODO(csuter): We should test or put checks in place to ensure this limit isn't exceeded.  It
// will likely involve placing limits on the maximum number of layers.
const MAX_STORE_INFO_SERIALIZED_SIZE: usize = 131072;

#[derive(Default)]
pub struct HandleOptions {
    /// If true, don't COW, write to blocks that are already allocated.
    pub overwrite: bool,
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
    store_info: Mutex<StoreInfo>,
    tree: LSMTree<ObjectKey, ObjectValue>,

    // When replaying the journal, the store cannot read StoreInfo until the whole journal
    // has been replayed, so during that time, opened will be false and records
    // just get sent to the tree. Once the journal has been replayed, we can open the store
    // and load all the other layer information.
    opened: Mutex<bool>,
}

impl ObjectStore {
    fn new(
        parent_store: Option<Arc<ObjectStore>>,
        store_object_id: u64,
        filesystem: Arc<dyn Filesystem>,
        store_info: StoreInfo,
        tree: LSMTree<ObjectKey, ObjectValue>,
        opened: bool,
    ) -> Arc<ObjectStore> {
        let device = filesystem.device();
        let block_size = device.block_size();
        let store = Arc::new(ObjectStore {
            parent_store,
            store_object_id,
            device: device,
            block_size,
            filesystem: Arc::downgrade(&filesystem),
            store_info: Mutex::new(store_info),
            tree,
            opened: Mutex::new(opened),
        });
        filesystem.register_store(&store);
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
            StoreInfo::default(),
            LSMTree::new(merge::merge),
            true,
        )
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

    pub async fn create_child_store<'a>(
        self: &'a Arc<ObjectStore>,
        transaction: &mut Transaction<'a>,
    ) -> Result<Arc<ObjectStore>, Error> {
        let object_id = self.get_next_object_id();
        self.create_child_store_with_id(transaction, object_id).await
    }

    async fn create_child_store_with_id<'a>(
        self: &'a Arc<ObjectStore>,
        transaction: &mut Transaction<'a>,
        object_id: u64,
    ) -> Result<Arc<ObjectStore>, Error> {
        self.ensure_open().await?;
        let handle =
            self.create_object_with_id(transaction, object_id, HandleOptions::default()).await?;
        // TODO(csuter): if the transaction rolls back, we need to delete the store.
        Ok(Self::new_empty(
            Some(self.clone()),
            handle.object_id(),
            self.filesystem.upgrade().unwrap(),
        ))
    }

    // When replaying the journal, we need to replay mutation records into the LSM tree, but we
    // cannot properly open the store until all the records have been replayed since some of the
    // records we replay might affect how we open, e.g. they might pertain to new layer files
    // backing this store.  The store will get properly opened whenever an action is taken that
    // needs the store to be opened (via ensure_open).
    pub(super) fn lazy_open_store(
        self: &Arc<ObjectStore>,
        store_object_id: u64,
    ) -> Arc<ObjectStore> {
        Self::new(
            Some(self.clone()),
            store_object_id,
            self.filesystem.upgrade().unwrap(),
            StoreInfo::default(),
            LSMTree::new(merge::merge),
            false,
        )
    }

    pub async fn open_store(
        self: &Arc<ObjectStore>,
        store_object_id: u64,
    ) -> Result<Arc<ObjectStore>, Error> {
        let store = self.lazy_open_store(store_object_id);
        store.ensure_open().await?;
        Ok(store)
    }

    pub async fn open_object(
        self: &Arc<Self>,
        object_id: u64,
        options: HandleOptions,
    ) -> Result<StoreObjectHandle, Error> {
        self.ensure_open().await?;
        let item = self
            .tree
            .find(&ObjectKey::attribute(object_id, DEFAULT_DATA_ATTRIBUTE_ID))
            .await?
            .ok_or(FxfsError::NotFound)?;
        if let ObjectValue::Attribute { size } = item.value {
            Ok(StoreObjectHandle {
                store: self.clone(),
                object_id,
                attribute_id: DEFAULT_DATA_ATTRIBUTE_ID,
                block_size: self.block_size.into(),
                size: Mutex::new(size),
                options,
            })
        } else {
            bail!(FxfsError::Inconsistent);
        }
    }

    async fn create_object_with_id(
        self: &Arc<Self>,
        transaction: &mut Transaction<'_>,
        object_id: u64,
        options: HandleOptions,
    ) -> Result<StoreObjectHandle, Error> {
        self.ensure_open().await?;
        transaction.add(
            self.store_object_id,
            Mutation::insert_object(
                ObjectKey::object(object_id),
                ObjectValue::object(ObjectDescriptor::File, 1),
            ),
        );
        transaction.add(
            self.store_object_id,
            Mutation::insert_object(
                ObjectKey::attribute(object_id, DEFAULT_DATA_ATTRIBUTE_ID),
                ObjectValue::attribute(0),
            ),
        );
        Ok(StoreObjectHandle {
            store: self.clone(),
            block_size: self.block_size.into(),
            object_id,
            attribute_id: DEFAULT_DATA_ATTRIBUTE_ID,
            size: Mutex::new(0),
            options,
        })
    }

    pub async fn create_object(
        self: &Arc<Self>,
        mut transaction: &mut Transaction<'_>,
        options: HandleOptions,
    ) -> Result<StoreObjectHandle, Error> {
        let object_id = self.get_next_object_id();
        self.create_object_with_id(&mut transaction, object_id, options).await
    }

    /// Push all in-memory structures to the device. This is not necessary for sync since the
    /// journal will take care of it.  This will panic if called on the root parent store, which is
    /// in-memory only.  This is supposed to be called when there is either memory or space pressure
    /// (flushing the store will persist in-memory data and allow the journal file to be trimmed).
    pub async fn flush(&self, force: bool) -> Result<(), Error> {
        self.ensure_open().await?;
        // TODO(csuter): This whole process needs to be within a transaction, or otherwise safe in
        // the event of power loss.
        let filesystem = self.filesystem();
        let object_sync = filesystem.begin_object_sync(self.store_object_id);
        if !force && !object_sync.needs_sync() {
            return Ok(());
        }
        let parent_store = self.parent_store.as_ref().unwrap();
        let mut transaction = filesystem.clone().new_transaction(&[]).await?;
        let object_handle =
            parent_store.create_object(&mut transaction, HandleOptions::default()).await?;
        transaction.add(self.store_object_id(), Mutation::TreeSeal);
        transaction.commit().await;

        let object_id = object_handle.object_id();
        let handle = parent_store
            .clone()
            .open_object(self.store_object_id, HandleOptions::default())
            .await?;
        self.tree.compact(object_handle).await?;
        let mut serialized_info = Vec::new();
        {
            let mut store_info = self.store_info.lock().unwrap();
            store_info.layers = vec![object_id];
            serialize_into(&mut serialized_info, &*store_info)?;
        }
        let mut buf = self.device.allocate_buffer(serialized_info.len());
        buf.as_mut_slice()[..serialized_info.len()].copy_from_slice(&serialized_info[..]);
        let mut transaction = filesystem.clone().new_transaction(&[]).await?;
        handle.txn_write(&mut transaction, 0u64, buf.as_ref()).await?;

        transaction.add(self.store_object_id(), Mutation::TreeCompact);
        transaction.commit().await;

        object_sync.commit();
        Ok(())
    }

    /// Adjusts the reference count for a given object.  If the reference count reaches zero, the
    /// object is destroyed and any extents are deallocated.
    pub async fn adjust_refs(
        &self,
        transaction: &mut Transaction<'_>,
        oid: u64,
        delta: i64,
    ) -> Result<(), Error> {
        let item = self.tree.find(&ObjectKey::object(oid)).await?.ok_or(FxfsError::NotFound)?;
        let (object_type, refs) =
            if let ObjectValue::Object { object_descriptor, refs } = item.value {
                (
                    object_descriptor,
                    if delta < 0 {
                        refs.checked_sub((-delta) as u64)
                    } else {
                        refs.checked_add(delta as u64)
                    }
                    .ok_or(anyhow!("refs underflow/overflow"))?,
                )
            } else {
                bail!(FxfsError::Inconsistent);
            };
        if refs == 0 {
            let layer_set = self.tree.layer_set();
            let mut merger = layer_set.merger();
            let allocator = self.allocator();
            let mut iter = merger.seek(Bound::Included(&ObjectKey::attribute(oid, 0))).await?;
            // Loop over all the extents and deallocate them.
            // TODO(csuter): deal with overflowing a transaction.
            while let Some(item) = iter.get() {
                if item.key.object_id != oid {
                    break;
                }
                if let Some((
                    _,
                    attr_id,
                    ExtentKey { range },
                    ExtentValue { device_offset: Some(device_offset) },
                )) = item.into()
                {
                    allocator
                        .deallocate(
                            transaction,
                            *device_offset..*device_offset + (range.end - range.start),
                        )
                        .await;
                    let end = range.end;
                    iter.advance_with_hint(&ObjectKey::extent(oid, attr_id, end..end + 1)).await?;
                } else if let ObjectKey { data: ObjectKeyData::Attribute(attr_id, _), .. } =
                    item.key
                {
                    let attr_id = *attr_id;
                    iter.advance_with_hint(&ObjectKey::extent(oid, attr_id, 0..1)).await?;
                } else {
                    // We expect either attribute records or extent records.
                    bail!(FxfsError::Inconsistent);
                }
            }
            transaction.add(
                self.store_object_id,
                Mutation::merge_object(ObjectKey::tombstone(oid), ObjectValue::None),
            );
        } else {
            transaction.add(
                self.store_object_id,
                Mutation::replace_or_insert_object(
                    item.key,
                    ObjectValue::object(object_type, refs),
                ),
            );
        }
        Ok(())
    }

    async fn ensure_open(&self) -> Result<(), Error> {
        if *self.opened.lock().unwrap() {
            return Ok(());
        }

        self.open_impl().await
    }

    // This returns a BoxFuture because of the cycle: open_object -> ensure_open -> open_impl ->
    // open_object.
    fn open_impl<'a>(&'a self) -> BoxFuture<'a, Result<(), Error>> {
        async move {
            // TODO(csuter): we need to introduce an async lock here.
            let parent_store = self.parent_store.as_ref().unwrap();
            let handle =
                parent_store.open_object(self.store_object_id, HandleOptions::default()).await?;
            if handle.get_size() > 0 {
                let serialized_info = handle.contents(MAX_STORE_INFO_SERIALIZED_SIZE).await?;
                let store_info: StoreInfo = deserialize_from(&serialized_info[..])?;
                let mut handles = Vec::new();
                for object_id in &store_info.layers {
                    handles.push(
                        parent_store.open_object(*object_id, HandleOptions::default()).await?,
                    );
                }
                self.tree.set_layers(handles.into()).await?;
                let mut current_store_info = self.store_info.lock().unwrap();
                if store_info.last_object_id > current_store_info.last_object_id {
                    current_store_info.last_object_id = store_info.last_object_id
                }
            }
            *self.opened.lock().unwrap() = true;
            Ok(())
        }
        .boxed()
    }

    fn get_next_object_id(&self) -> u64 {
        let mut store_info = self.store_info.lock().unwrap();
        store_info.last_object_id += 1;
        store_info.last_object_id
    }

    fn allocator(&self) -> Arc<dyn Allocator> {
        self.filesystem().allocator()
    }
}

#[async_trait]
impl Mutations for ObjectStore {
    async fn apply_mutation(
        &self,
        mutation: Mutation,
        replay: bool,
        object: Option<AssociatedObject<'_>>,
    ) {
        // It's not safe to fully open a store until replay is fully complete (because
        // subsequent mutations could render current records invalid). The exception to
        // this is the root parent object store which contains the extents for the journal
        // file: whilst we are replaying we need to be able to track new extents for the
        // journal file so that we can read from it whilst we are replaying.
        assert!(!replay || !*self.opened.lock().unwrap() || self.parent_store.is_none());

        match mutation {
            Mutation::ObjectStore(ObjectStoreMutation { item, op }) => {
                {
                    let mut store_info = self.store_info.lock().unwrap();
                    if item.key.object_id > store_info.last_object_id {
                        store_info.last_object_id = item.key.object_id;
                    }
                }
                // If this mutation is a size change, update the cached size if there is an
                // associated object.
                match (object, &item) {
                    (Some(object), ObjectItem { value: ObjectValue::Attribute { size }, .. }) => {
                        *object.downcast_ref::<StoreObjectHandle>().unwrap().size.lock().unwrap() =
                            *size;
                    }
                    _ => {}
                }
                match op {
                    Operation::Insert => self.tree.insert(item).await,
                    Operation::ReplaceOrInsert => self.tree.replace_or_insert(item).await,
                    Operation::Merge => {
                        let lower_bound = item.key.key_for_merge_into();
                        self.tree.merge_into(item, &lower_bound).await;
                    }
                }
            }
            Mutation::TreeSeal => self.tree.seal(),
            Mutation::TreeCompact => {
                if replay {
                    self.tree.reset_immutable_layers();
                }
            }
            _ => panic!("unexpected mutation: {:?}", mutation), // TODO(csuter): can't panic
        }
    }

    fn drop_mutation(&self, _mutation: Mutation) {}
}

pub struct StoreObjectHandle {
    store: Arc<ObjectStore>,
    block_size: u64,
    object_id: u64,
    attribute_id: u64,
    size: Mutex<u64>,
    options: HandleOptions,
}

impl StoreObjectHandle {
    pub fn store(&self) -> Arc<ObjectStore> {
        self.store.clone()
    }

    /// Extend the file with the given extent.  The only use case for this right now is for files
    /// that must exist at certain offsets on the device, such as super-blocks.
    pub async fn extend<'a>(&'a self, transaction: &mut Transaction<'a>, device_range: Range<u64>) {
        let old_size = self.txn_get_size(transaction);
        let new_size = old_size + device_range.end - device_range.start;
        self.store.allocator().reserve(transaction, device_range.clone()).await;
        transaction.add_with_object(
            self.store.store_object_id,
            Mutation::replace_or_insert_object(
                ObjectKey::attribute(self.object_id, self.attribute_id),
                ObjectValue::attribute(new_size),
            ),
            self,
        );
        transaction.add(
            self.store.store_object_id(),
            Mutation::merge_object(
                ObjectKey::extent(self.object_id, self.attribute_id, old_size..new_size),
                ObjectValue::extent(device_range.start),
            ),
        );
    }

    async fn write_cow<'a>(
        &'a self,
        transaction: &mut Transaction<'a>,
        mut offset: u64,
        buf: BufferRef<'_>,
    ) -> Result<(), Error> {
        let mut aligned = round_down(offset, self.block_size)
            ..round_up(offset + buf.len() as u64, self.block_size);
        let mut buf_offset = 0;
        if offset + buf.len() as u64 > self.txn_get_size(transaction) {
            transaction.add_with_object(
                self.store.store_object_id,
                Mutation::replace_or_insert_object(
                    ObjectKey::attribute(self.object_id, self.attribute_id),
                    ObjectValue::attribute(offset + buf.len() as u64),
                ),
                self,
            );
        }
        self.deallocate_old_extents(transaction, &ExtentKey::new(aligned.clone())).await?;
        let allocator = self.store.allocator();
        while buf_offset < buf.len() {
            let device_range = allocator.allocate(transaction, aligned.end - aligned.start).await?;
            let extent_len = device_range.end - device_range.start;
            let end = aligned.start + extent_len;
            let len = min(buf.len() - buf_offset, (end - offset) as usize);
            assert!(len > 0);
            self.write_at(
                offset,
                buf.subslice(buf_offset..buf_offset + len),
                device_range.start + offset % self.block_size,
            )
            .await?;
            transaction.add(
                self.store.store_object_id,
                Mutation::merge_object(
                    ObjectKey::extent(self.object_id, self.attribute_id, aligned.start..end),
                    ObjectValue::extent(device_range.start),
                ),
            );
            aligned.start += extent_len;
            buf_offset += len;
            offset += len as u64;
        }
        Ok(())
    }

    // All the extents for the range must have been preallocated using preallocate_range or from
    // existing writes.
    async fn overwrite(&self, mut offset: u64, buf: BufferRef<'_>) -> Result<(), Error> {
        let tree = &self.store.tree;
        let layer_set = tree.layer_set();
        let mut merger = layer_set.merger();
        let end = offset + buf.len() as u64;
        let mut iter = merger
            .seek(Bound::Included(
                &ObjectKey::extent(self.object_id, self.attribute_id, offset..end).search_key(),
            ))
            .await?;
        let mut pos = 0;
        loop {
            let (device_offset, to_do) = match iter.get().and_then(Into::into) {
                Some((
                    object_id,
                    attribute_id,
                    ExtentKey { range },
                    ExtentValue { device_offset: Some(device_offset) },
                )) if object_id == self.object_id
                    && attribute_id == self.attribute_id
                    && range.start <= offset =>
                {
                    (
                        device_offset + (offset - range.start),
                        min(buf.len() - pos, (range.end - offset) as usize),
                    )
                }
                _ => bail!("offset {} not allocated", offset),
            };
            self.write_at(offset, buf.subslice(pos..pos + to_do), device_offset).await?;
            pos += to_do;
            if pos == buf.len() {
                break;
            }
            offset += to_do as u64;
            iter.advance_with_hint(
                &ObjectKey::extent(self.object_id, self.attribute_id, offset..end).search_key(),
            )
            .await?;
        }
        Ok(())
    }

    async fn write_at(
        &self,
        offset: u64,
        buf: BufferRef<'_>,
        mut device_offset: u64,
    ) -> Result<(), Error> {
        // Deal with alignment.
        let start_align = (offset % self.block_size) as usize;
        let start_offset = offset - start_align as u64;
        let remainder = if start_align > 0 {
            let (head, remainder) =
                buf.split_at(min(self.block_size as usize - start_align, buf.len()));
            let mut align_buf = self.store.device.allocate_buffer(self.block_size as usize);
            let read = self.read(start_offset, align_buf.as_mut()).await?;
            align_buf.as_mut_slice()[read..].fill(0);
            align_buf.as_mut_slice()[start_align..(start_align + head.len())]
                .copy_from_slice(head.as_slice());
            device_offset -= start_align as u64;
            self.store.device.write(device_offset, align_buf.as_ref()).await?;
            device_offset += self.block_size;
            remainder
        } else {
            buf
        };
        if remainder.len() > 0 {
            let end = offset + buf.len() as u64;
            let end_align = (end % self.block_size) as usize;
            let (whole_blocks, tail) = remainder.split_at(remainder.len() - end_align);
            self.store.device.write(device_offset, whole_blocks).await?;
            device_offset += whole_blocks.len() as u64;
            if tail.len() > 0 {
                let mut align_buf = self.store.device.allocate_buffer(self.block_size as usize);
                let read = self.read(end - end_align as u64, align_buf.as_mut()).await?;
                align_buf.as_mut_slice()[read..].fill(0);
                &align_buf.as_mut_slice()[..tail.len()].copy_from_slice(tail.as_slice());
                self.store.device.write(device_offset, align_buf.as_ref()).await?;
            }
        }
        Ok(())
    }

    async fn deallocate_old_extents(
        &self,
        transaction: &mut Transaction<'_>,
        key: &ExtentKey,
    ) -> Result<(), Error> {
        let tree = &self.store.tree;
        let layer_set = tree.layer_set();
        let lower_bound =
            ObjectKey::with_extent_key(self.object_id, self.attribute_id, key.search_key());
        let mut merger = layer_set.merger();
        let mut iter = merger.seek(Bound::Included(&lower_bound)).await?;
        let allocator = self.store.allocator();
        loop {
            let (extent_key, extent_value) = match iter.get().and_then(Into::into) {
                Some((oid, attribute_id, extent_key, extent_value))
                    if oid == self.object_id && attribute_id == self.attribute_id =>
                {
                    (extent_key, extent_value)
                }
                _ => break,
            };
            if let ExtentValue { device_offset: Some(device_offset) } = extent_value {
                if let Some(overlap) = key.overlap(extent_key) {
                    allocator
                        .deallocate(
                            transaction,
                            device_offset + overlap.start - extent_key.range.start
                                ..device_offset + overlap.end - extent_key.range.start,
                        )
                        .await;
                } else {
                    break;
                }
            }
            let next = ObjectKey::extent(
                self.object_id,
                self.attribute_id,
                extent_key.range.end..extent_key.range.end + 1,
            );
            iter.advance_with_hint(&next).await.unwrap();
        }
        Ok(())
    }

    // Within a transaction, the size of the object might have changed, so get the size from there
    // if it exists, otherwise, fall back on the cached size.
    fn txn_get_size(&self, transaction: &Transaction<'_>) -> u64 {
        transaction
            .get_object_mutation(
                self.store.store_object_id,
                ObjectKey::attribute(self.object_id, self.attribute_id),
            )
            .and_then(|m| {
                if let ObjectItem { value: ObjectValue::Attribute { size }, .. } = m.item {
                    Some(size)
                } else {
                    None
                }
            })
            .unwrap_or_else(|| self.get_size())
    }
}

// TODO(jfsulliv): Move into utils module or something else.
fn round_down<T: Into<u64>>(offset: u64, block_size: T) -> u64 {
    offset - offset % block_size.into()
}

fn round_up<T: Into<u64>>(offset: u64, block_size: T) -> u64 {
    let block_size = block_size.into();
    round_down(offset + block_size - 1, block_size)
}

#[async_trait]
impl ObjectHandle for StoreObjectHandle {
    fn object_id(&self) -> u64 {
        return self.object_id;
    }

    fn allocate_buffer(&self, size: usize) -> Buffer<'_> {
        self.store.device.allocate_buffer(size)
    }

    async fn read(&self, mut offset: u64, mut buf: MutableBufferRef<'_>) -> Result<usize, Error> {
        if buf.len() == 0 {
            return Ok(0);
        }
        let fs = self.store.filesystem();
        let _guard = fs
            .read_lock(&[LockKey::object_attribute(
                self.store.store_object_id,
                self.object_id,
                self.attribute_id,
            )])
            .await;
        let size = self.get_size();
        if offset >= size {
            return Ok(0);
        }
        let tree = &self.store.tree;
        let layer_set = tree.layer_set();
        let mut merger = layer_set.merger();
        let mut iter = merger
            .seek(Bound::Included(&ObjectKey::extent(
                self.object_id,
                self.attribute_id,
                offset..offset + 1,
            )))
            .await?;
        let to_do = min(buf.len() as u64, size - offset) as usize;
        buf = buf.subslice_mut(0..to_do);
        let mut start_align = (offset % self.block_size) as usize;
        let end_align = ((offset + to_do as u64) % self.block_size) as usize;
        while let Some((object_id, attribute_id, extent_key, extent_value)) =
            iter.get().and_then(Into::into)
        {
            if object_id != self.object_id || attribute_id != self.attribute_id {
                break;
            }
            if extent_key.range.start > offset {
                // Zero everything up to the start of the extent.
                let to_zero = min(extent_key.range.start - offset, buf.len() as u64) as usize;
                for i in &mut buf.as_mut_slice()[..to_zero] {
                    *i = 0;
                }
                buf = buf.subslice_mut(to_zero..);
                if buf.is_empty() {
                    break;
                }
                offset += to_zero as u64;
                start_align = 0;
            }

            let next_offset = if let ExtentValue { device_offset: Some(device_offset) } =
                extent_value
            {
                let mut device_offset =
                    device_offset + (offset - start_align as u64 - extent_key.range.start);

                // Deal with starting alignment by reading the existing contents into an alignment
                // buffer.
                if start_align > 0 {
                    let mut align_buf = self.store.device.allocate_buffer(self.block_size as usize);
                    self.store.device.read(device_offset, align_buf.as_mut()).await?;
                    let to_copy = min(self.block_size as usize - start_align, buf.len());
                    buf.as_mut_slice()[..to_copy].copy_from_slice(
                        &align_buf.as_slice()[start_align..(start_align + to_copy)],
                    );
                    buf = buf.subslice_mut(to_copy..);
                    if buf.is_empty() {
                        break;
                    }
                    offset += to_copy as u64;
                    device_offset += self.block_size;
                    start_align = 0;
                }

                let to_copy = min(buf.len() - end_align, (extent_key.range.end - offset) as usize);
                if to_copy > 0 {
                    self.store
                        .device
                        .read(device_offset, buf.reborrow().subslice_mut(..to_copy))
                        .await?;
                    buf = buf.subslice_mut(to_copy..);
                    if buf.is_empty() {
                        break;
                    }
                    offset += to_copy as u64;
                    device_offset += to_copy as u64;
                }

                // Deal with end alignment, again by reading the exsting contents into an alignment
                // buffer.
                if offset < extent_key.range.end && end_align > 0 {
                    let mut align_buf = self.store.device.allocate_buffer(self.block_size as usize);
                    self.store.device.read(device_offset, align_buf.as_mut()).await?;
                    buf.as_mut_slice().copy_from_slice(&align_buf.as_slice()[..end_align]);
                    buf = buf.subslice_mut(0..0);
                    break;
                }
                offset
            } else if extent_key.range.end >= offset + buf.len() as u64 {
                // Deleted extent covers remainder, so we're done.
                break;
            } else {
                // Skip past deleted extents.
                extent_key.range.end
            };

            iter.advance_with_hint(&ObjectKey::extent(
                self.object_id,
                self.attribute_id,
                next_offset..next_offset + 1,
            ))
            .await?;
        }
        buf.as_mut_slice().fill(0);
        Ok(to_do)
    }

    async fn txn_write<'a>(
        &'a self,
        transaction: &mut Transaction<'a>,
        offset: u64,
        buf: BufferRef<'_>,
    ) -> Result<(), Error> {
        if buf.is_empty() {
            Ok(())
        } else if self.options.overwrite {
            self.overwrite(offset, buf).await
        } else {
            self.write_cow(transaction, offset, buf).await
        }
    }

    fn get_size(&self) -> u64 {
        *self.size.lock().unwrap()
    }

    async fn truncate<'a>(
        &'a self,
        transaction: &mut Transaction<'a>,
        size: u64,
    ) -> Result<(), Error> {
        let old_size = self.txn_get_size(transaction);
        if size < old_size {
            let deleted_range =
                round_up(size, self.block_size)..round_up(old_size, self.block_size);
            transaction.add(
                self.store.store_object_id,
                Mutation::merge_object(
                    ObjectKey::extent(self.object_id, self.attribute_id, deleted_range.clone()),
                    ObjectValue::deleted_extent(),
                ),
            );
            self.deallocate_old_extents(transaction, &ExtentKey::new(deleted_range)).await?;
            let to_zero = round_up(size, self.block_size) - size;
            if to_zero > 0 {
                assert!(to_zero < self.block_size);
                // We intentionally use the COW write path even if we're in overwrite mode. There's
                // no need to support overwrite mode here, and it would be difficult since we'd need
                // to transactionalize zeroing the tail of the last block with the other metadata
                // changes, which we don't currently have a way to do.
                // TODO(csuter): This is allocating a small buffer that we'll just end up copying.
                // Is there a better way?
                let mut buf = self.allocate_buffer(to_zero as usize);
                buf.as_mut_slice().fill(0);
                self.write_cow(transaction, size, buf.as_ref()).await?;
            }
        }
        transaction.add_with_object(
            self.store.store_object_id,
            Mutation::replace_or_insert_object(
                ObjectKey::attribute(self.object_id, self.attribute_id),
                ObjectValue::attribute(size),
            ),
            self,
        );
        Ok(())
    }

    // Must be multiple of block size.
    async fn preallocate_range<'a>(
        &'a self,
        transaction: &mut Transaction<'a>,
        mut file_range: Range<u64>,
    ) -> Result<Vec<Range<u64>>, Error> {
        let mut ranges = Vec::new();
        let tree = &self.store.tree;
        let layer_set = tree.layer_set();
        let mut merger = layer_set.merger();
        let mut iter = merger
            .seek(Bound::Included(&ObjectKey::with_extent_key(
                self.object_id,
                self.attribute_id,
                ExtentKey::new(file_range.clone()).search_key(),
            )))
            .await?;
        'outer: while file_range.start < file_range.end {
            let allocate_end = loop {
                match iter.get().and_then(Into::into) {
                    Some((
                        oid,
                        attribute_id,
                        extent_key,
                        ExtentValue { device_offset: Some(device_offset) },
                    )) if oid == self.object_id
                        && attribute_id == self.attribute_id
                        && extent_key.range.start < file_range.end =>
                    {
                        if extent_key.range.start <= file_range.start {
                            // Record the existing extent and move on.
                            let device_range = device_offset + file_range.start
                                - extent_key.range.start
                                ..device_offset + min(extent_key.range.end, file_range.end)
                                    - extent_key.range.start;
                            file_range.start += device_range.end - device_range.start;
                            ranges.push(device_range);
                            if file_range.start >= file_range.end {
                                break 'outer;
                            }
                            iter.advance_with_hint(&ObjectKey::with_extent_key(
                                self.object_id,
                                self.attribute_id,
                                ExtentKey::new(file_range.clone()).search_key(),
                            ))
                            .await?;
                            continue;
                        } else {
                            // There's nothing allocated between file_range.start and the beginning
                            // of this extent.
                            break extent_key.range.start;
                        }
                    }
                    Some((oid, attribute_id, extent_key, ExtentValue { device_offset: None }))
                        if oid == self.object_id
                            && attribute_id == self.attribute_id
                            && extent_key.range.end < file_range.end =>
                    {
                        // The current extent is sparse, so skip to the next extent.
                        let next_extent_hint = extent_key.range.end..file_range.end;
                        iter.advance_with_hint(&ObjectKey::with_extent_key(
                            self.object_id,
                            self.attribute_id,
                            ExtentKey::new(next_extent_hint).search_key(),
                        ))
                        .await?;
                    }
                    _ => {
                        // We can just preallocate the rest.
                        break file_range.end;
                    }
                }
            };
            let device_range = self
                .store
                .allocator()
                .allocate(transaction, allocate_end - file_range.start)
                .await?;
            let this_file_range =
                file_range.start..file_range.start + device_range.end - device_range.start;
            file_range.start = this_file_range.end;
            transaction.add(
                self.store.store_object_id,
                Mutation::merge_object(
                    ObjectKey::extent(self.object_id, self.attribute_id, this_file_range),
                    ObjectValue::extent(device_range.start),
                ),
            );
            ranges.push(device_range);
            // If we didn't allocate all that we requested, we'll loop around and try again.
        }
        // Update the file size if it changed.
        if file_range.end > self.txn_get_size(transaction) {
            transaction.add_with_object(
                self.store.store_object_id,
                Mutation::replace_or_insert_object(
                    ObjectKey::attribute(self.object_id, self.attribute_id),
                    ObjectValue::attribute(file_range.end),
                ),
                self,
            );
        }
        Ok(ranges)
    }

    async fn new_transaction<'a>(&self) -> Result<Transaction<'a>, Error> {
        Ok(self
            .store
            .filesystem()
            .new_transaction(&[LockKey::object_attribute(
                self.store.store_object_id,
                self.object_id,
                self.attribute_id,
            )])
            .await?)
    }
}

#[cfg(test)]
mod tests {
    use {
        crate::{
            lsm_tree::types::{ItemRef, LayerIterator},
            object_handle::{ObjectHandle, ObjectHandleExt},
            object_store::{
                record::{ObjectKey, ObjectKeyData},
                round_up,
                testing::{fake_allocator::FakeAllocator, fake_filesystem::FakeFilesystem},
                transaction::TransactionHandler,
                HandleOptions, ObjectStore, StoreObjectHandle,
            },
            testing::fake_device::FakeDevice,
        },
        fuchsia_async as fasync,
        futures::{channel::oneshot::channel, join},
        rand::Rng,
        std::{
            ops::Bound,
            sync::{Arc, Mutex},
            time::Duration,
        },
    };

    const TEST_DEVICE_BLOCK_SIZE: u32 = 512;

    // Some tests (the preallocate_range ones) currently assume that the data only occupies a single
    // device block.
    const TEST_DATA_OFFSET: u64 = 600;
    const TEST_DATA: &[u8] = b"hello";
    const TEST_OBJECT_SIZE: u64 = 913;

    async fn test_filesystem_and_store(
    ) -> (Arc<FakeFilesystem>, Arc<FakeAllocator>, Arc<ObjectStore>) {
        let device = Arc::new(FakeDevice::new(1024, TEST_DEVICE_BLOCK_SIZE));
        let fs = FakeFilesystem::new(device);
        let allocator = Arc::new(FakeAllocator::new());
        fs.object_manager().set_allocator(allocator.clone());
        let parent_store = ObjectStore::new_empty(None, 2, fs.clone());
        let mut transaction =
            fs.clone().new_transaction(&[]).await.expect("new_transaction failed");
        let store = parent_store
            .create_child_store_with_id(&mut transaction, 3)
            .await
            .expect("create_child_store failed");
        transaction.commit().await;
        (fs.clone(), allocator, store)
    }

    async fn test_filesystem_and_object(
    ) -> (Arc<FakeFilesystem>, Arc<FakeAllocator>, StoreObjectHandle) {
        let (fs, allocator, store) = test_filesystem_and_store().await;
        let object;
        let mut transaction =
            fs.clone().new_transaction(&[]).await.expect("new_transaction failed");
        object = store
            .create_object(&mut transaction, HandleOptions::default())
            .await
            .expect("create_object failed");
        {
            let mut buf = object.allocate_buffer(TEST_DATA.len());
            buf.as_mut_slice().copy_from_slice(TEST_DATA);
            object
                .txn_write(&mut transaction, TEST_DATA_OFFSET, buf.as_ref())
                .await
                .expect("write failed");
        }
        object.truncate(&mut transaction, TEST_OBJECT_SIZE).await.expect("truncate failed");
        transaction.commit().await;
        (fs, allocator, object)
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_zero_buf_len_read() {
        let (_fs, _, object) = test_filesystem_and_object().await;
        let mut buf = object.allocate_buffer(0);
        assert_eq!(object.read(0u64, buf.as_mut()).await.expect("read failed"), 0);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_beyond_eof_read() {
        let (_fs, _, object) = test_filesystem_and_object().await;
        let mut buf = object.allocate_buffer(TEST_DATA.len() * 2);
        buf.as_mut_slice().fill(123u8);
        assert_eq!(object.read(TEST_OBJECT_SIZE, buf.as_mut()).await.expect("read failed"), 0);
        assert_eq!(object.read(TEST_OBJECT_SIZE - 2, buf.as_mut()).await.expect("read failed"), 2);
        assert_eq!(&buf.as_slice()[0..2], &[0, 0]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_read_sparse() {
        let (_fs, _, object) = test_filesystem_and_object().await;
        // Deliberately read 1 byte into the object and not right to eof.
        let len = TEST_OBJECT_SIZE as usize - 2;
        let mut buf = object.allocate_buffer(len);
        buf.as_mut_slice().fill(123u8);
        assert_eq!(object.read(1u64, buf.as_mut()).await.expect("read failed"), len);
        let mut expected = vec![0; len];
        let offset = TEST_DATA_OFFSET as usize - 1;
        &mut expected[offset..offset + TEST_DATA.len()].copy_from_slice(TEST_DATA);
        assert_eq!(buf.as_slice()[..len], expected[..]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_read_after_writes_interspersed_with_flush() {
        let (_fs, _, object) = test_filesystem_and_object().await;

        object.store().flush(false).await.expect("flush failed");

        // Write more test data to the first block fo the file.
        let mut buf = object.allocate_buffer(TEST_DATA.len());
        buf.as_mut_slice().copy_from_slice(TEST_DATA);
        object.write(0u64, buf.as_ref()).await.expect("write failed");

        let len = TEST_OBJECT_SIZE as usize - 2;
        let mut buf = object.allocate_buffer(len);
        buf.as_mut_slice().fill(123u8);
        assert_eq!(object.read(1u64, buf.as_mut()).await.expect("read failed"), len);

        let mut expected = vec![0u8; len];
        let offset = TEST_DATA_OFFSET as usize - 1;
        &mut expected[offset..offset + TEST_DATA.len()].copy_from_slice(TEST_DATA);
        &mut expected[..TEST_DATA.len() - 1].copy_from_slice(&TEST_DATA[1..]);
        assert_eq!(buf.as_slice()[..len], expected[..]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_read_after_truncate_and_extend() {
        let (fs, _, object) = test_filesystem_and_object().await;

        // Arrange for there to be <extent><deleted-extent><extent>.
        let mut buf = object.allocate_buffer(TEST_DATA.len());
        buf.as_mut_slice().copy_from_slice(TEST_DATA);
        object.write(0, buf.as_ref()).await.expect("write failed"); // This adds an extent at 0..512.
        let mut transaction =
            fs.clone().new_transaction(&[]).await.expect("new_transaction failed");
        object.truncate(&mut transaction, 3).await.expect("truncate failed"); // This deletes 512..1024.
        transaction.commit().await;
        let data = b"foo";
        let mut buf = object.allocate_buffer(data.len());
        buf.as_mut_slice().copy_from_slice(data);
        object.write(1500, buf.as_ref()).await.expect("write failed"); // This adds 1024..1536.

        const LEN1: usize = 1501;
        let mut buf = object.allocate_buffer(LEN1);
        buf.as_mut_slice().fill(123u8);
        assert_eq!(object.read(1u64, buf.as_mut()).await.expect("read failed"), LEN1);
        let mut expected = [0; LEN1];
        &mut expected[0..2].copy_from_slice(&TEST_DATA[1..3]);
        &mut expected[1499..].copy_from_slice(b"fo");
        assert_eq!(buf.as_slice(), expected);

        // Also test a read that ends midway through the deleted extent.
        const LEN2: usize = 600;
        let mut buf = object.allocate_buffer(LEN2);
        buf.as_mut_slice().fill(123u8);
        assert_eq!(object.read(1u64, buf.as_mut()).await.expect("read failed"), LEN2);
        assert_eq!(buf.as_slice(), &expected[..LEN2]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_read_whole_blocks_with_multiple_objects() {
        let (fs, _, object) = test_filesystem_and_object().await;
        let mut buffer = object.allocate_buffer(512);
        buffer.as_mut_slice().fill(0xaf);
        object.write(0, buffer.as_ref()).await.expect("write failed");

        let store = object.store();
        let mut transaction =
            fs.clone().new_transaction(&[]).await.expect("new_transaction failed");
        let object2 = store
            .create_object(&mut transaction, HandleOptions::default())
            .await
            .expect("create_object failed");
        transaction.commit().await;
        let mut ef_buffer = object.allocate_buffer(512);
        ef_buffer.as_mut_slice().fill(0xef);
        object2.write(0, ef_buffer.as_ref()).await.expect("write failed");

        let mut buffer = object.allocate_buffer(512);
        buffer.as_mut_slice().fill(0xaf);
        object.write(512, buffer.as_ref()).await.expect("write failed");
        let mut transaction = object.new_transaction().await.expect("new_transaction failed");
        object.truncate(&mut transaction, 1536).await.expect("truncate failed");
        transaction.commit().await;
        object2.write(512, ef_buffer.as_ref()).await.expect("write failed");

        let mut buffer = object.allocate_buffer(2048);
        buffer.as_mut_slice().fill(123);
        assert_eq!(object.read(0, buffer.as_mut()).await.expect("read failed"), 1536);
        assert_eq!(&buffer.as_slice()[..1024], &[0xaf; 1024]);
        assert_eq!(&buffer.as_slice()[1024..1536], &[0; 512]);
        assert_eq!(object2.read(0, buffer.as_mut()).await.expect("read failed"), 1024);
        assert_eq!(&buffer.as_slice()[..1024], &[0xef; 1024]);
    }

    async fn test_preallocate_common(allocator: &FakeAllocator, object: StoreObjectHandle) {
        let allocated_before = allocator.allocated();
        let mut transaction = object.new_transaction().await.expect("new_transaction failed");
        object.preallocate_range(&mut transaction, 0..512).await.expect("preallocate_range failed");
        transaction.commit().await;
        assert!(object.get_size() < 1048576);
        let mut transaction = object.new_transaction().await.expect("new_transaction failed");
        object
            .preallocate_range(&mut transaction, 0..1048576)
            .await
            .expect("preallocate_range failed");
        transaction.commit().await;
        assert_eq!(object.get_size(), 1048576);
        // Check that it didn't reallocate the space for the existing extent
        let allocated_after = allocator.allocated();
        assert_eq!(allocated_after - allocated_before, 1048576 - TEST_DEVICE_BLOCK_SIZE as usize);

        // Reopen the object in overwrite mode.
        let object = object
            .store
            .open_object(
                object.object_id(),
                HandleOptions { overwrite: true, ..Default::default() },
            )
            .await
            .expect("open_object_with_id failed");
        let mut buf = object.allocate_buffer(2048);
        buf.as_mut_slice().fill(47);
        object.write(0, buf.subslice(..TEST_DATA_OFFSET as usize)).await.expect("write failed");
        buf.as_mut_slice().fill(95);
        let offset = round_up(TEST_OBJECT_SIZE, TEST_DEVICE_BLOCK_SIZE);
        object.write(offset, buf.as_ref()).await.expect("write failed");

        // Make sure there were no more allocations.
        assert_eq!(allocator.allocated(), allocated_after);

        // Read back the data and make sure it is what we expect.
        let mut buf = object.allocate_buffer(104876);
        assert_eq!(object.read(0, buf.as_mut()).await.expect("read failed"), buf.len());
        assert_eq!(&buf.as_slice()[..TEST_DATA_OFFSET as usize], &[47; TEST_DATA_OFFSET as usize]);
        assert_eq!(
            &buf.as_slice()[TEST_DATA_OFFSET as usize..TEST_DATA_OFFSET as usize + TEST_DATA.len()],
            TEST_DATA
        );
        assert_eq!(&buf.as_slice()[offset as usize..offset as usize + 2048], &[95; 2048]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_preallocate_range() {
        let (_fs, allocator, object) = test_filesystem_and_object().await;
        test_preallocate_common(&allocator, object).await;
    }

    // This is identical to the previous test except that we flush so that extents end up in
    // different layers.
    #[fasync::run_singlethreaded(test)]
    async fn test_preallocate_suceeds_when_extents_are_in_different_layers() {
        let (_fs, allocator, object) = test_filesystem_and_object().await;
        object.store().flush(false).await.expect("flush failed");
        test_preallocate_common(&allocator, object).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_already_preallocated() {
        let (_fs, allocator, object) = test_filesystem_and_object().await;
        let allocated_before = allocator.allocated();
        let mut transaction = object.new_transaction().await.expect("new_transaction failed");
        let offset = TEST_DATA_OFFSET - TEST_DATA_OFFSET % TEST_DEVICE_BLOCK_SIZE as u64;
        object
            .preallocate_range(&mut transaction, offset..offset + 512)
            .await
            .expect("preallocate_range failed");
        transaction.commit().await;
        // Check that it didn't reallocate any new space.
        assert_eq!(allocator.allocated(), allocated_before);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_overwrite_fails_if_not_preallocated() {
        let (_fs, _, object) = test_filesystem_and_object().await;

        let object = object
            .store
            .open_object(
                object.object_id(),
                HandleOptions { overwrite: true, ..Default::default() },
            )
            .await
            .expect("open_object_with_id failed");
        let mut buf = object.allocate_buffer(2048);
        buf.as_mut_slice().fill(95);
        let offset = round_up(TEST_OBJECT_SIZE, TEST_DEVICE_BLOCK_SIZE);
        object.write(offset, buf.as_ref()).await.expect_err("write suceceded");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_extend() {
        let (fs, _allocator, store) = test_filesystem_and_store().await;
        let handle;
        let mut transaction =
            fs.clone().new_transaction(&[]).await.expect("new_transaction failed");
        handle = store
            .create_object(
                &mut transaction,
                HandleOptions { overwrite: true, ..Default::default() },
            )
            .await
            .expect("create_object failed");
        handle.extend(&mut transaction, 0..5 * TEST_DEVICE_BLOCK_SIZE as u64).await;
        transaction.commit().await;
        let mut buf = handle.allocate_buffer(5 * TEST_DEVICE_BLOCK_SIZE as usize);
        buf.as_mut_slice().fill(123);
        handle.write(0, buf.as_ref()).await.expect("write failed");
        buf.as_mut_slice().fill(67);
        handle.read(0, buf.as_mut()).await.expect("read failed");
        assert_eq!(buf.as_slice(), [123; 5 * TEST_DEVICE_BLOCK_SIZE as usize]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_truncate_deallocates_old_extents() {
        let (fs, allocator, object) = test_filesystem_and_object().await;
        let mut buf = object.allocate_buffer(5 * TEST_DEVICE_BLOCK_SIZE as usize);
        buf.as_mut_slice().fill(0xaa);
        object.write(0, buf.as_ref()).await.expect("write failed");

        let deallocated_before = allocator.deallocated();
        let mut transaction =
            fs.clone().new_transaction(&[]).await.expect("new_transaction failed");
        object
            .truncate(&mut transaction, TEST_DEVICE_BLOCK_SIZE as u64)
            .await
            .expect("truncate failed");
        transaction.commit().await;
        let deallocated_after = allocator.deallocated();
        assert!(
            deallocated_before < deallocated_after,
            "before = {} after = {}",
            deallocated_before,
            deallocated_after
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_adjust_refs() {
        let (fs, allocator, object) = test_filesystem_and_object().await;
        let mut transaction =
            fs.clone().new_transaction(&[]).await.expect("new_transaction failed");
        let store = object.store();
        store
            .adjust_refs(&mut transaction, object.object_id(), 1)
            .await
            .expect("adjust_refs failed");
        transaction.commit().await;

        let deallocated_before = allocator.deallocated();
        let mut transaction =
            fs.clone().new_transaction(&[]).await.expect("new_transaction failed");
        store
            .adjust_refs(&mut transaction, object.object_id(), -2)
            .await
            .expect("adjust_refs failed");
        transaction.commit().await;
        assert_eq!(allocator.deallocated() - deallocated_before, TEST_DEVICE_BLOCK_SIZE as usize);

        let layer_set = store.tree.layer_set();
        let mut merger = layer_set.merger();
        let mut iter = merger.seek(Bound::Unbounded).await.expect("seek failed");
        let mut found = false;
        while let Some(ItemRef { key: ObjectKey { object_id, data }, .. }) = iter.get() {
            if let ObjectKeyData::Tombstone = data {
                assert!(!found);
                found = true;
            } else {
                assert!(*object_id != object.object_id(), "{:?}", iter.get());
            }
            iter.advance().await.expect("advance failed");
        }
        assert!(found);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_locks() {
        let (_fs, _allocator, object) = test_filesystem_and_object().await;
        let (send1, recv1) = channel();
        let (send2, recv2) = channel();
        let (send3, recv3) = channel();
        let done = Mutex::new(false);
        join!(
            async {
                let mut t = object.new_transaction().await.expect("new_transaction failed");
                send1.send(()).unwrap(); // Tell the next future to continue.
                send3.send(()).unwrap(); // Tell the last future to continue.
                recv2.await.unwrap();
                let mut buf = object.allocate_buffer(5);
                buf.as_mut_slice().copy_from_slice(b"hello");
                object.txn_write(&mut t, 0, buf.as_ref()).await.expect("write failed");
                // This is a halting problem so all we can do is sleep.
                fasync::Timer::new(Duration::from_millis(100)).await;
                assert!(!*done.lock().unwrap());
                t.commit().await;
            },
            async {
                recv1.await.unwrap();
                // Reads should not block.
                let mut buf = object.allocate_buffer(TEST_DATA.len());
                assert_eq!(
                    object.read(TEST_DATA_OFFSET, buf.as_mut()).await.expect("read failed"),
                    TEST_DATA.len()
                );
                assert_eq!(buf.as_slice(), TEST_DATA);
                // Tell the first future to continue.
                send2.send(()).unwrap();
            },
            async {
                // This should block until the first future has completed.
                recv3.await.unwrap();
                let _t = object.new_transaction().await.expect("new_transaction failed");
                let mut buf = object.allocate_buffer(5);
                assert_eq!(object.read(0, buf.as_mut()).await.expect("read failed"), 5);
                assert_eq!(buf.as_slice(), b"hello");
            }
        );
    }

    #[fasync::run(10, test)]
    async fn test_racy_reads() {
        let (fs, _allocator, store) = test_filesystem_and_store().await;
        let object;
        let mut transaction =
            fs.clone().new_transaction(&[]).await.expect("new_transaction failed");
        object = Arc::new(
            store
                .create_object(&mut transaction, HandleOptions::default())
                .await
                .expect("create_object failed"),
        );
        transaction.commit().await;
        for _ in 0..100 {
            let cloned_object = object.clone();
            let writer = fasync::Task::spawn(async move {
                let mut buf = cloned_object.allocate_buffer(10);
                buf.as_mut_slice().fill(123);
                cloned_object.write(0, buf.as_ref()).await.expect("write failed");
            });
            let cloned_object = object.clone();
            let reader = fasync::Task::spawn(async move {
                let wait_time = rand::thread_rng().gen_range(0, 5);
                fasync::Timer::new(Duration::from_millis(wait_time)).await;
                let mut buf = cloned_object.allocate_buffer(10);
                buf.as_mut_slice().fill(23);
                let amount = cloned_object.read(0, buf.as_mut()).await.expect("write failed");
                // If we succeed in reading data, it must include the write; i.e. if we see the size
                // change, we should see the data too.  For this to succeed it requires locking on
                // the read size to ensure that when we read the size, we get the extents changed in
                // that same transaction.
                if amount != 0 {
                    assert_eq!(amount, 10);
                    assert_eq!(buf.as_slice(), &[123; 10]);
                }
            });
            writer.await;
            reader.await;
            let mut transaction = object.new_transaction().await.expect("new_transaction failed");
            object.truncate(&mut transaction, 0).await.expect("truncate failed");
            transaction.commit().await;
        }
    }
}

// TODO(csuter): validation of all deserialized structs.
// TODO(csuter): test ObjectStore::flush.
// TODO(csuter): check all panic! calls.
// TODO(csuter): test allocation and deallocation.
