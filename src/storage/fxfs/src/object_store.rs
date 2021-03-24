// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod allocator;
mod constants;
pub mod device;
pub mod filesystem;
mod journal;
mod merge;
mod record;
pub mod transaction;

pub use record::ObjectType;

use {
    crate::{
        errors::FxfsError,
        lsm_tree::{
            types::{Item, ItemRef, LayerIterator},
            LSMTree,
        },
        object_handle::{ObjectHandle, ObjectHandleExt},
        object_store::{
            device::Device,
            filesystem::{ApplyMutations, Filesystem},
            record::DEFAULT_DATA_ATTRIBUTE_ID,
            record::{
                decode_extent, ExtentKey, ExtentValue, ObjectItem, ObjectKey, ObjectKeyData,
                ObjectValue,
            },
            transaction::{Mutation, Transaction},
        },
    },
    allocator::Allocator,
    anyhow::{bail, Error},
    async_trait::async_trait,
    bincode::{deserialize_from, serialize_into},
    futures::{future::BoxFuture, FutureExt},
    serde::{Deserialize, Serialize},
    std::{
        cmp::min,
        ops::Bound,
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

/// An object store supports a file like interface for objects.  Objects are keyed by a 64 bit
/// identifier.  And object store has to be backed by a parent object store (which stores metadata
/// for the object store).  The top-level object store (a.k.a. the root parent object store) is
/// in-memory only.
pub struct ObjectStore {
    parent_store: Option<Arc<ObjectStore>>,
    store_object_id: u64,
    device: Arc<dyn Device>,
    block_size: u64,
    allocator: Weak<dyn Allocator>,
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
        allocator: &Arc<dyn Allocator>,
        filesystem: &Arc<dyn Filesystem>,
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
            allocator: Arc::downgrade(allocator),
            filesystem: Arc::downgrade(filesystem),
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
        allocator: &Arc<dyn Allocator>,
        filesystem: &Arc<dyn Filesystem>,
    ) -> Arc<Self> {
        Self::new(
            parent_store,
            store_object_id,
            allocator,
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

    pub async fn create_child_store(self: &Arc<ObjectStore>) -> Result<Arc<ObjectStore>, Error> {
        let object_id = self.get_next_object_id();
        self.create_child_store_with_id(object_id).await
    }

    async fn create_child_store_with_id(
        self: &Arc<ObjectStore>,
        object_id: u64,
    ) -> Result<Arc<ObjectStore>, Error> {
        self.ensure_open().await?;
        // TODO(csuter): This should probably all be in a transaction. There should probably be a
        // journal record to create a store.
        let mut transaction = Transaction::new();
        let handle = self
            .clone()
            .create_object_with_id(&mut transaction, object_id, HandleOptions::default())
            .await?;
        let filesystem = self.filesystem.upgrade().unwrap();
        filesystem.commit_transaction(transaction).await;

        // Write a default StoreInfo file.  TODO(csuter): this should be part of a bigger
        // transaction i.e.  this function should take transaction as an arg.
        let mut serialized_info = Vec::new();
        serialize_into(&mut serialized_info, &StoreInfo::default())?;
        handle.write(0u64, &serialized_info).await?;

        Ok(Self::new_empty(
            Some(self.clone()),
            handle.object_id(),
            &self.allocator.upgrade().unwrap(),
            &self.filesystem.upgrade().unwrap(),
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
            &self.allocator.upgrade().unwrap(),
            &self.filesystem.upgrade().unwrap(),
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
                object_id: object_id,
                attribute_id: DEFAULT_DATA_ATTRIBUTE_ID,
                block_size: self.block_size,
                size: Mutex::new(size),
                options,
            })
        } else {
            bail!("Expected attribute value");
        }
    }

    async fn create_object_with_id(
        self: &Arc<Self>,
        transaction: &mut Transaction,
        object_id: u64,
        options: HandleOptions,
    ) -> Result<StoreObjectHandle, Error> {
        self.ensure_open().await?;
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
                    key: ObjectKey::attribute(object_id, DEFAULT_DATA_ATTRIBUTE_ID),
                    value: ObjectValue::attribute(DEFAULT_DATA_ATTRIBUTE_ID),
                },
            },
        );
        Ok(StoreObjectHandle {
            store: self.clone(),
            block_size: self.block_size,
            object_id,
            attribute_id: DEFAULT_DATA_ATTRIBUTE_ID,
            size: Mutex::new(0),
            options,
        })
    }

    pub async fn create_object(
        self: &Arc<Self>,
        mut transaction: &mut Transaction,
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
        let mut transaction = Transaction::new();
        let object_handle =
            parent_store.clone().create_object(&mut transaction, HandleOptions::default()).await?;
        transaction.add(self.store_object_id(), Mutation::TreeSeal);
        self.filesystem.upgrade().unwrap().commit_transaction(transaction).await;

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
            // TODO(csuter): replace with a replace_contents method, or maybe better, pass
            // transaction to write.
            serialize_into(&mut serialized_info, &*store_info)?;
        }
        handle.write(0u64, &serialized_info).await?;

        let mut transaction = Transaction::new();
        transaction.add(self.store_object_id(), Mutation::TreeCompact);
        filesystem.commit_transaction(transaction).await;

        object_sync.commit();
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
            let serialized_info = handle.contents(MAX_STORE_INFO_SERIALIZED_SIZE).await?;
            let store_info: StoreInfo = deserialize_from(&serialized_info[..])?;
            let mut handles = Vec::new();
            for object_id in &store_info.layers {
                handles.push(parent_store.open_object(*object_id, HandleOptions::default()).await?);
            }
            self.tree.set_layers(handles.into());
            let mut current_store_info = self.store_info.lock().unwrap();
            if store_info.last_object_id > current_store_info.last_object_id {
                current_store_info.last_object_id = store_info.last_object_id
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
}

#[async_trait]
impl ApplyMutations for ObjectStore {
    async fn apply_mutation(&self, mutation: Mutation, replay: bool) {
        // It's not safe to fully open a store until replay is fully complete (because
        // subsequent mutations could render current records invalid). The exception to
        // this is the root parent object store which contains the extents for the journal
        // file: whilst we are replaying we need to be able to track new extents for the
        // journal file so that we can read from it whilst we are replaying.
        assert!(!replay || !*self.opened.lock().unwrap() || self.parent_store.is_none());

        match mutation {
            Mutation::Insert { item } => {
                {
                    let mut store_info = self.store_info.lock().unwrap();
                    if item.key.object_id > store_info.last_object_id {
                        store_info.last_object_id = item.key.object_id;
                    }
                }
                self.tree.insert(item).await;
            }
            Mutation::ReplaceExtent { item } => {
                let lower_bound = item.key.key_for_merge_into();
                self.tree.merge_into(item, &lower_bound).await;
            }
            Mutation::ReplaceOrInsert { item } => self.tree.replace_or_insert(item).await,
            Mutation::TreeSeal => self.tree.seal(),
            Mutation::TreeCompact => {
                if replay {
                    self.tree.reset_immutable_layers();
                }
            }
            _ => panic!("unexpected mutation!"),
        }
    }
}

#[derive(Default)]
pub struct HandleOptions {
    // If true, don't COW, write to blocks that are already allocated.
    pub overwrite: bool,
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
    fn store(&self) -> Arc<ObjectStore> {
        self.store.clone()
    }

    async fn write_cow(
        &self,
        transaction: &mut Transaction,
        mut offset: u64,
        buf: &[u8],
    ) -> Result<(), Error> {
        let mut aligned = round_down(offset, self.block_size)
            ..round_up(offset + buf.len() as u64, self.block_size);
        let mut buf_offset = 0;
        if offset + buf.len() as u64 > *self.size.lock().unwrap() {
            // TODO(csuter): need to hold locks properly
            *self.size.lock().unwrap() = offset + buf.len() as u64;
            transaction.add(
                self.store.store_object_id,
                Mutation::ReplaceOrInsert {
                    item: ObjectItem {
                        key: ObjectKey::attribute(self.object_id, self.attribute_id),
                        value: ObjectValue::attribute(*self.size.lock().unwrap()),
                    },
                },
            );
        }
        self.delete_old_extents(transaction, &ExtentKey::new(self.attribute_id, aligned.clone()))
            .await?;
        while buf_offset < buf.len() {
            let device_range = self
                .store
                .allocator
                .upgrade()
                .unwrap()
                .allocate(
                    transaction,
                    self.store.store_object_id(),
                    self.object_id,
                    self.attribute_id,
                    aligned.clone(),
                )
                .await?;
            let extent_len = device_range.end - device_range.start;
            let end = aligned.start + extent_len;
            let len = min(buf.len() - buf_offset, (end - offset) as usize);
            assert!(len > 0);
            self.write_at(
                offset,
                &buf[buf_offset..buf_offset + len],
                device_range.start + offset % self.block_size,
            )
            .await?;
            transaction.add(
                self.store.store_object_id,
                Mutation::ReplaceExtent {
                    item: Item::new(
                        ObjectKey::extent(self.object_id, self.attribute_id, aligned.start..end),
                        ObjectValue::extent(device_range.start),
                    ),
                },
            );
            aligned.start += extent_len;
            buf_offset += len;
            offset += len as u64;
        }
        Ok(())
    }

    async fn write_at(&self, offset: u64, buf: &[u8], mut device_offset: u64) -> Result<(), Error> {
        // Deal with alignment.
        let start_align = (offset % self.block_size) as usize;
        let start_offset = offset - start_align as u64;
        let remainder = if start_align > 0 {
            let (head, remainder) =
                buf.split_at(min(self.block_size as usize - start_align, buf.len()));
            let mut align_buf = vec![0; self.block_size as usize];
            self.read(start_offset, align_buf.as_mut_slice()).await?;
            &mut align_buf[start_align..(start_align + head.len())].copy_from_slice(head);
            device_offset -= start_align as u64;
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
                self.read(end - end_align as u64, align_buf.as_mut_slice()).await?;
                align_buf[..tail.len()].copy_from_slice(tail);
                self.store.device.write(device_offset, &align_buf)?;
            }
        }
        Ok(())
    }

    async fn delete_old_extents(
        &self,
        transaction: &mut Transaction,
        key: &ExtentKey,
    ) -> Result<(), Error> {
        let tree = &self.store.tree;
        let layer_set = tree.layer_set();
        let lower_bound = ObjectKey::with_extent_key(self.object_id, key.search_key());
        let mut merger = layer_set.merger();
        let mut iter = merger.seek(Bound::Included(&lower_bound)).await?;
        loop {
            let (oid, extent_key, extent_value) = match iter.get().and_then(decode_extent) {
                None => break,
                Some((oid, extent_key, extent_value)) => (oid, extent_key, extent_value),
            };
            if oid != self.object_id || extent_key.attribute_id != self.attribute_id {
                break;
            }
            if let ExtentValue { device_offset: Some(device_offset) } = extent_value {
                if let Some(overlap) = key.overlap(extent_key) {
                    self.store
                        .allocator
                        .upgrade()
                        .unwrap()
                        .deallocate(
                            transaction,
                            self.store.store_object_id(),
                            self.object_id,
                            key.attribute_id,
                            device_offset + overlap.start - extent_key.range.start
                                ..device_offset + overlap.end - extent_key.range.start,
                            overlap.start,
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
}

fn round_down(offset: u64, block_size: u64) -> u64 {
    offset - offset % block_size
}

fn round_up(offset: u64, block_size: u64) -> u64 {
    round_down(offset + block_size - 1, block_size)
}

#[async_trait]
impl ObjectHandle for StoreObjectHandle {
    fn object_id(&self) -> u64 {
        return self.object_id;
    }

    async fn read(&self, mut offset: u64, mut buf: &mut [u8]) -> Result<usize, Error> {
        if buf.len() == 0 || offset >= *self.size.lock().unwrap() {
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
        let to_do = min(buf.len() as u64, *self.size.lock().unwrap() - offset) as usize;
        buf = &mut buf[..to_do];
        let mut start_align = (offset % self.block_size) as usize;
        let end_align = ((offset + to_do as u64) % self.block_size) as usize;
        while let Some(ItemRef {
            key: ObjectKey { object_id, data: ObjectKeyData::Extent(extent_key) },
            value: ObjectValue::Extent(extent_value),
        }) = iter.get()
        {
            if *object_id != self.object_id || extent_key.attribute_id != self.attribute_id {
                break;
            }
            if extent_key.range.start > offset {
                // Zero everything up to the start of the extent.
                let to_zero = min(extent_key.range.start - offset, buf.len() as u64) as usize;
                for i in &mut buf[..to_zero] {
                    *i = 0;
                }
                buf = &mut buf[to_zero..];
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
                    let mut align_buf = vec![0; self.block_size as usize];
                    self.store.device.read(device_offset, &mut align_buf)?;
                    let to_copy = min(self.block_size as usize - start_align, buf.len());
                    buf[..to_copy]
                        .copy_from_slice(&mut align_buf[start_align..(start_align + to_copy)]);
                    buf = &mut buf[to_copy..];
                    if buf.is_empty() {
                        break;
                    }
                    offset += to_copy as u64;
                    device_offset += self.block_size;
                    start_align = 0;
                }

                let to_copy = min(buf.len() - end_align, (extent_key.range.end - offset) as usize);
                if to_copy > 0 {
                    self.store.device.read(device_offset, &mut buf[..to_copy])?;
                    buf = &mut buf[to_copy..];
                    if buf.is_empty() {
                        break;
                    }
                    offset += to_copy as u64;
                    device_offset += to_copy as u64;
                }

                // Deal with end alignment, again by reading the exsting contents into an alignment
                // buffer.
                if offset < extent_key.range.end && end_align > 0 {
                    let mut align_buf = vec![0; self.block_size as usize];
                    self.store.device.read(device_offset, &mut align_buf)?;
                    buf.copy_from_slice(&align_buf[..end_align]);
                    buf = &mut [];
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
        buf.fill(0);
        Ok(to_do)
    }

    async fn write(&self, offset: u64, buf: &[u8]) -> Result<(), Error> {
        if self.options.overwrite {
            panic!("Not supported");
        } else {
            let mut transaction = Transaction::new(); // TODO(csuter): transaction too big?
            self.write_cow(&mut transaction, offset, buf).await?;
            self.store.filesystem.upgrade().unwrap().commit_transaction(transaction).await;
            Ok(())
        }
    }

    async fn truncate(&self, length: u64) -> Result<(), Error> {
        let mut transaction = Transaction::new(); // TODO(csuter): transaction too big?
        let old_size = *self.size.lock().unwrap();
        if length < old_size {
            let deleted_range =
                round_up(length, self.block_size)..round_up(old_size, self.block_size);
            transaction.add(
                self.store.store_object_id,
                Mutation::ReplaceExtent {
                    item: ObjectItem {
                        key: ObjectKey::extent(self.object_id, self.attribute_id, deleted_range),
                        value: ObjectValue::deleted_extent(),
                    },
                },
            );
            let to_zero = round_up(length, self.block_size) - length;
            if to_zero > 0 {
                assert!(to_zero < self.block_size);
                // We intentionally use the COW write path even if we're in overwrite mode. There's
                // no need to support overwrite mode here, and it would be difficult since we'd need
                // to transactionalize zeroing the tail of the last block with the other metadata
                // changes, which we don't currently have a way to do.
                self.write_cow(&mut transaction, length, &vec![0 as u8; to_zero as usize]).await?;
            }
        }
        transaction.add(
            self.store.store_object_id,
            Mutation::ReplaceOrInsert {
                item: ObjectItem {
                    key: ObjectKey::attribute(self.object_id, self.attribute_id),
                    value: ObjectValue::attribute(length),
                },
            },
        );
        self.store.filesystem.upgrade().unwrap().commit_transaction(transaction).await;
        *self.size.lock().unwrap() = length;
        Ok(())
    }

    fn get_size(&self) -> u64 {
        *self.size.lock().unwrap()
    }
}

#[cfg(test)]
mod tests {
    use {
        crate::{
            object_handle::ObjectHandle,
            object_store::{
                allocator::Allocator,
                device::Device,
                filesystem::{ApplyMutations, Filesystem, ObjectManager, ObjectSync},
                journal::JournalCheckpoint,
                transaction::{Mutation, Transaction},
                HandleOptions, ObjectStore, StoreObjectHandle,
            },
            testing::fake_device::FakeDevice,
        },
        anyhow::Error,
        async_trait::async_trait,
        fuchsia_async as fasync,
        std::sync::{Arc, Mutex},
    };

    const ALLOCATOR_OBJECT_ID: u64 = 1;
    const TEST_DATA_OFFSET: u64 = 600;
    const TEST_DATA: &[u8] = b"hello";
    const TEST_OBJECT_SIZE: u64 = 913;

    struct FakeAllocator(Mutex<u64>);

    #[async_trait]
    impl Allocator for FakeAllocator {
        fn object_id(&self) -> u64 {
            ALLOCATOR_OBJECT_ID
        }

        async fn allocate(
            &self,
            _transaction: &mut Transaction,
            _store_object_id: u64,
            _object_id: u64,
            _attribute_id: u64,
            object_range: std::ops::Range<u64>,
        ) -> Result<std::ops::Range<u64>, Error> {
            let mut last_end = self.0.lock().unwrap();
            let result = *last_end..*last_end + object_range.end - object_range.start;
            *last_end = result.end;
            Ok(result)
        }

        async fn deallocate(
            &self,
            _transaction: &mut Transaction,
            _store_object_id: u64,
            _object_id: u64,
            _attribute_id: u64,
            _device_range: std::ops::Range<u64>,
            _file_offset: u64,
        ) {
        }

        fn as_apply_mutations(self: Arc<Self>) -> Arc<dyn ApplyMutations> {
            self
        }
    }

    #[async_trait]
    impl ApplyMutations for FakeAllocator {
        async fn apply_mutation(&self, _mutation: Mutation, _replay: bool) {}
    }

    struct FakeFilesystem {
        device: Arc<dyn Device>,
        _allocator: Arc<FakeAllocator>,
        object_manager: Arc<ObjectManager>,
    }

    impl FakeFilesystem {
        fn new(device: Arc<dyn Device>, allocator: Arc<FakeAllocator>) -> Arc<Self> {
            Arc::new(FakeFilesystem {
                device,
                _allocator: allocator,
                object_manager: Arc::new(ObjectManager::new()),
            })
        }
    }

    #[async_trait]
    impl Filesystem for FakeFilesystem {
        async fn commit_transaction(&self, transaction: Transaction) {
            for (object_id, mutation) in transaction.mutations {
                self.object_manager
                    .apply_mutation(object_id, mutation, false, &JournalCheckpoint::default())
                    .await;
            }
        }

        fn register_store(&self, store: &Arc<ObjectStore>) {
            self.object_manager.register_store(store);
        }

        fn begin_object_sync(&self, object_id: u64) -> ObjectSync {
            self.object_manager.begin_object_sync(object_id)
        }

        fn device(&self) -> Arc<dyn Device> {
            self.device.clone()
        }
    }

    async fn test_filesystem_and_store() -> (Arc<FakeFilesystem>, Arc<ObjectStore>) {
        let device = Arc::new(FakeDevice::new(512));
        let allocator = Arc::new(FakeAllocator(Mutex::new(0)));
        let filesystem = FakeFilesystem::new(device, allocator.clone());
        let parent_store = ObjectStore::new_empty(
            None,
            2,
            &(allocator.clone() as Arc<dyn Allocator>),
            &(filesystem.clone() as Arc<dyn Filesystem>),
        );
        (
            filesystem.clone(),
            parent_store.create_child_store().await.expect("create_child_store failed"),
        )
    }

    async fn test_filesystem_and_object() -> (Arc<FakeFilesystem>, StoreObjectHandle) {
        let (fs, store) = test_filesystem_and_store().await;
        let mut transaction = Transaction::new();
        let object = store
            .create_object(&mut transaction, HandleOptions::default())
            .await
            .expect("create_object failed");
        store.filesystem().commit_transaction(transaction).await;
        object.write(TEST_DATA_OFFSET, TEST_DATA).await.expect("write failed");
        object.truncate(TEST_OBJECT_SIZE).await.expect("truncate failed");
        (fs, object)
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_zero_buf_len_read() {
        let (_fs, object) = test_filesystem_and_object().await;
        let mut buf = [0u8; 0];
        assert_eq!(object.read(0, &mut buf).await.expect("read failed"), 0);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_beyond_eof_read() {
        let (_fs, object) = test_filesystem_and_object().await;
        let mut buf = [123u8; TEST_DATA.len() * 2];
        assert_eq!(object.read(TEST_OBJECT_SIZE, &mut buf).await.expect("read failed"), 0);
        assert_eq!(object.read(TEST_OBJECT_SIZE - 2, &mut buf).await.expect("read failed"), 2);
        assert_eq!(&buf[0..2], &[0, 0]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_read_sparse() {
        let (_fs, object) = test_filesystem_and_object().await;
        // Deliberately read 1 byte into the object and not right to eof.
        let mut buf = [123u8; TEST_OBJECT_SIZE as usize - 2];
        assert_eq!(
            object.read(1, &mut buf).await.expect("read failed"),
            TEST_OBJECT_SIZE as usize - 2
        );
        let mut expected = [0; TEST_OBJECT_SIZE as usize - 2];
        let offset = TEST_DATA_OFFSET as usize - 1;
        &mut expected[offset..offset + TEST_DATA.len()].copy_from_slice(TEST_DATA);
        assert_eq!(buf, expected);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_read_after_writes_interspersed_with_flush() {
        let (_fs, object) = test_filesystem_and_object().await;

        object.store().flush(false).await.expect("flush failed");

        // Write more test data to the first block fo the file.
        object.write(0, TEST_DATA).await.expect("write failed");

        let mut buf = [123u8; TEST_OBJECT_SIZE as usize - 2];
        assert_eq!(
            object.read(1, &mut buf).await.expect("read failed"),
            TEST_OBJECT_SIZE as usize - 2
        );

        let mut expected = [0; TEST_OBJECT_SIZE as usize - 2];
        let offset = TEST_DATA_OFFSET as usize - 1;
        &mut expected[offset..offset + TEST_DATA.len()].copy_from_slice(TEST_DATA);
        &mut expected[..TEST_DATA.len() - 1].copy_from_slice(&TEST_DATA[1..]);
        assert_eq!(buf, expected);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_read_after_truncate_and_extend() {
        let (_fs, object) = test_filesystem_and_object().await;

        // Arrange for there to be <extent><deleted-extent><extent>.
        object.write(0, TEST_DATA).await.expect("write failed"); // This adds an extent at 0..512.
        object.truncate(3).await.expect("truncate failed"); // This deletes 512..1024.
        object.write(1500, b"foo").await.expect("write failed"); // This adds 1024..1536.

        const LEN1: usize = 1501;
        let mut buf = [123u8; LEN1];
        assert_eq!(object.read(1, &mut buf).await.expect("read failed"), LEN1);
        let mut expected = [0; LEN1];
        &mut expected[0..2].copy_from_slice(&TEST_DATA[1..3]);
        &mut expected[1499..].copy_from_slice(b"fo");
        assert_eq!(buf, expected);

        // Also test a read that ends midway through the deleted extent.
        const LEN2: usize = 600;
        let mut buf = [123u8; LEN2];
        assert_eq!(object.read(1, &mut buf[0..600]).await.expect("read failed"), LEN2);
        assert_eq!(buf, &expected[..LEN2]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_read_whole_blocks_with_multiple_objects() {
        let (_fs, object) = test_filesystem_and_object().await;
        object.write(0, &[0xaf; 512]).await.expect("write failed");

        let store = object.store();
        let mut transaction = Transaction::new();
        let object2 = store
            .create_object(&mut transaction, HandleOptions::default())
            .await
            .expect("create_object failed");
        store.filesystem().commit_transaction(transaction).await;
        object2.write(0, &[0xef; 512]).await.expect("write failed");

        object.write(512, &[0xaf; 512]).await.expect("write failed");
        object.truncate(1536).await.expect("truncate failed");
        object2.write(512, &[0xef; 512]).await.expect("write failed");

        let mut buf = [123u8; 2048];
        assert_eq!(object.read(0, &mut buf).await.expect("read failed"), 1536);
        assert_eq!(&buf[..1024], &[0xaf; 1024]);
        assert_eq!(&buf[1024..1536], &[0; 512]);
        assert_eq!(object2.read(0, &mut buf).await.expect("read failed"), 1024);
        assert_eq!(&buf[..1024], &[0xef; 1024]);
    }
}

// TODO(csuter): validation of all deserialized structs.
// TODO(csuter): test ObjectStore::flush.
// TODO(csuter): check all panic! calls.
// TODO(csuter): test allocation and deallocation.
