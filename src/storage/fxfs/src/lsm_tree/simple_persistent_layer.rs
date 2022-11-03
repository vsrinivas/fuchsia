// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        errors::FxfsError,
        log::*,
        lsm_tree::types::{
            BoxedLayerIterator, Item, ItemRef, Key, Layer, LayerIterator, LayerWriter, Value,
        },
        object_handle::{ReadObjectHandle, WriteBytes},
        round::{round_down, round_up},
        serialized_types::{Version, Versioned, VersionedLatest, LATEST_VERSION},
    },
    anyhow::{bail, ensure, Context, Error},
    async_trait::async_trait,
    async_utils::event::Event,
    byteorder::{ByteOrder, LittleEndian, ReadBytesExt, WriteBytesExt},
    serde::{Deserialize, Serialize},
    std::{
        cmp::Ordering,
        io::Read,
        marker::PhantomData,
        ops::{Bound, Drop},
        sync::{Arc, Mutex},
        vec::Vec,
    },
    storage_device::buffer::Buffer,
    type_hash::TypeHash,
};

// The first block of each layer contains metadata for the rest of the layer.
#[derive(Debug, Serialize, Deserialize, TypeHash, Versioned)]
pub struct LayerInfo {
    /// The version of the key and value structs serialized in this layer.
    key_value_version: Version,
    /// The block size used within this layer file. This is typically set at compaction time to the
    /// same block size as the underlying object handle.
    ///
    /// (Each block starts with a 2 byte item count so there is a 64k item limit per block,
    /// regardless of block size).
    block_size: u64,
}

/// Implements a very primitive persistent layer where items are packed into blocks and searching
/// for items is done via a simple binary search.
pub struct SimplePersistentLayer {
    object_handle: Arc<dyn ReadObjectHandle>,
    layer_info: LayerInfo,
    size: u64,
    close_event: Mutex<Option<Event>>,
}

struct BufferCursor<'a> {
    buffer: Buffer<'a>,
    pos: usize,
    len: usize,
}

impl std::io::Read for BufferCursor<'_> {
    fn read(&mut self, buf: &mut [u8]) -> std::io::Result<usize> {
        let to_read = std::cmp::min(buf.len(), self.len.saturating_sub(self.pos));
        if to_read > 0 {
            buf[..to_read].copy_from_slice(&self.buffer.as_slice()[self.pos..self.pos + to_read]);
            self.pos += to_read;
        }
        Ok(to_read)
    }
}

struct Iterator<'iter, K: Key, V: Value> {
    // Allocated out of |layer|.
    buffer: BufferCursor<'iter>,

    layer: &'iter SimplePersistentLayer,

    // The position of the _next_ block to be read.
    pos: u64,

    // The item index in the current block.
    item_index: u16,

    // The number of items in the current block.
    item_count: u16,

    // The current item.
    item: Option<Item<K, V>>,
}

impl<K: Key, V: Value> Iterator<'_, K, V> {
    fn new<'iter>(layer: &'iter SimplePersistentLayer, pos: u64) -> Iterator<'iter, K, V> {
        Iterator {
            layer,
            buffer: BufferCursor {
                buffer: layer.object_handle.allocate_buffer(layer.layer_info.block_size as usize),
                pos: 0,
                len: 0,
            },
            pos,
            item_index: 0,
            item_count: 0,
            item: None,
        }
    }
}

#[async_trait]
impl<'iter, K: Key, V: Value> LayerIterator<K, V> for Iterator<'iter, K, V> {
    async fn advance(&mut self) -> Result<(), Error> {
        if self.item_index >= self.item_count {
            if self.pos >= self.layer.size {
                self.item = None;
                return Ok(());
            }
            let len = self.layer.object_handle.read(self.pos, self.buffer.buffer.as_mut()).await?;
            self.buffer.pos = 0;
            self.buffer.len = len;
            debug!(
                pos = self.pos,
                object_size = self.layer.size,
                oid = self.layer.object_handle.object_id()
            );
            self.item_count = self.buffer.read_u16::<LittleEndian>()?;
            if self.item_count == 0 {
                bail!(
                    "Read block with zero item count (object: {}, offset: {})",
                    self.layer.object_handle.object_id(),
                    self.pos
                );
            }
            self.pos += self.layer.layer_info.block_size;
            self.item_index = 0;
        }
        self.item = Some(Item {
            key: K::deserialize_from_version(
                self.buffer.by_ref(),
                self.layer.layer_info.key_value_version,
            )
            .context("Corrupt layer (key)")?,
            value: V::deserialize_from_version(
                self.buffer.by_ref(),
                self.layer.layer_info.key_value_version,
            )
            .context("Corrupt layer (value)")?,
            sequence: self.buffer.read_u64::<LittleEndian>().context("Corrupt layer (seq)")?,
        });
        self.item_index += 1;
        Ok(())
    }

    fn get(&self) -> Option<ItemRef<'_, K, V>> {
        return self.item.as_ref().map(<&Item<K, V>>::into);
    }
}

impl SimplePersistentLayer {
    /// Opens an existing layer that is accessible via |object_handle| (which provides a read
    /// interface to the object).  The layer should have been written prior using
    /// SimplePersistentLayerWriter.
    pub async fn open(object_handle: impl ReadObjectHandle + 'static) -> Result<Arc<Self>, Error> {
        let size = object_handle.get_size();
        let physical_block_size = object_handle.block_size();

        // The first block contains layer file information instead of key/value data.
        let (layer_info, _version) = {
            let mut buffer = object_handle.allocate_buffer(physical_block_size as usize);
            object_handle.read(0, buffer.as_mut()).await?;
            let mut cursor = std::io::Cursor::new(buffer.as_slice());
            LayerInfo::deserialize_with_version(&mut cursor)
                .context("Failed to deserialize LayerInfo")?
        };

        // We expect the layer block size to be a multiple of the physical block size.
        ensure!(layer_info.block_size % physical_block_size == 0, FxfsError::Inconsistent);

        // Catch obviously bad sizes.
        ensure!(size < u64::MAX - layer_info.block_size, FxfsError::Inconsistent);

        Ok(Arc::new(SimplePersistentLayer {
            object_handle: Arc::new(object_handle),
            layer_info,
            size,
            close_event: Mutex::new(Some(Event::new())),
        }))
    }
}

#[async_trait]
impl<K: Key, V: Value> Layer<K, V> for SimplePersistentLayer {
    fn handle(&self) -> Option<&dyn ReadObjectHandle> {
        Some(self.object_handle.as_ref())
    }

    async fn seek<'a>(&'a self, bound: Bound<&K>) -> Result<BoxedLayerIterator<'a, K, V>, Error> {
        let first_block_offset = self.layer_info.block_size;
        let (key, excluded) = match bound {
            Bound::Unbounded => {
                let mut iterator = Iterator::new(self, first_block_offset);
                iterator.advance().await?;
                return Ok(Box::new(iterator));
            }
            Bound::Included(k) => (k, false),
            Bound::Excluded(k) => (k, true),
        };
        // Skip the first block. We Store version info there for now.
        let mut left_offset = self.layer_info.block_size;
        let mut right_offset = round_up(self.size, self.layer_info.block_size).unwrap();
        let mut left = Iterator::new(self, left_offset);
        left.advance().await?;
        match left.get() {
            None => return Ok(Box::new(left)),
            Some(item) => match item.key.cmp_upper_bound(key) {
                Ordering::Greater => return Ok(Box::new(left)),
                Ordering::Equal => {
                    if excluded {
                        left.advance().await?;
                    }
                    return Ok(Box::new(left));
                }
                Ordering::Less => {}
            },
        }
        while right_offset - left_offset > self.layer_info.block_size as u64 {
            // Pick a block midway.
            let mid_offset = round_down(
                left_offset + (right_offset - left_offset) / 2,
                self.layer_info.block_size,
            );
            let mut iterator = Iterator::new(self, mid_offset);
            iterator.advance().await?;
            let item: ItemRef<'_, K, V> = iterator.get().unwrap();
            match item.key.cmp_upper_bound(key) {
                Ordering::Greater => right_offset = mid_offset,
                Ordering::Equal => {
                    if excluded {
                        iterator.advance().await?;
                    }
                    return Ok(Box::new(iterator));
                }
                Ordering::Less => {
                    left_offset = mid_offset;
                    left = iterator;
                }
            }
        }
        // At this point, we know that left_key < key and right_key >= key, so we have to iterate
        // through left_key to find the key we want.
        loop {
            left.advance().await?;
            match left.get() {
                None => return Ok(Box::new(left)),
                Some(item) => match item.key.cmp_upper_bound(key) {
                    Ordering::Greater => return Ok(Box::new(left)),
                    Ordering::Equal => {
                        if excluded {
                            left.advance().await?;
                        }
                        return Ok(Box::new(left));
                    }
                    Ordering::Less => {}
                },
            }
        }
    }

    fn lock(&self) -> Option<Event> {
        self.close_event.lock().unwrap().clone()
    }

    async fn close(&self) {
        let _ = {
            let event = self.close_event.lock().unwrap().take().expect("close already called");
            event.wait_or_dropped()
        }
        .await;
    }

    fn get_version(&self) -> Version {
        return self.layer_info.key_value_version;
    }
}

// -- Writer support --

pub struct SimplePersistentLayerWriter<W: WriteBytes, K: Key, V: Value> {
    writer: W,
    block_size: u64,
    buf: Vec<u8>,
    item_count: u16,
    _key: PhantomData<K>,
    _value: PhantomData<V>,
}

impl<W: WriteBytes, K: Key, V: Value> SimplePersistentLayerWriter<W, K, V> {
    /// Creates a new writer that will serialize items to the object accessible via |object_handle|
    /// (which provdes a write interface to the object).
    pub async fn new(mut writer: W, block_size: u64) -> Result<Self, Error> {
        let layer_info = LayerInfo { block_size, key_value_version: LATEST_VERSION };
        let mut buf: Vec<u8> = Vec::new();
        let len;
        {
            buf.resize(layer_info.block_size as usize, 0);
            let mut cursor = std::io::Cursor::new(&mut buf);
            layer_info.serialize_with_version(&mut cursor)?;
            len = cursor.position();
        }
        writer.write_bytes(&buf[..len as usize]).await?;
        writer.skip(block_size as u64 - len as u64).await?;
        Ok(SimplePersistentLayerWriter {
            writer,
            block_size,
            buf: vec![0; 2],
            item_count: 0,
            _key: PhantomData,
            _value: PhantomData,
        })
    }

    async fn write_some(&mut self, len: usize) -> Result<(), Error> {
        if self.item_count == 0 {
            return Ok(());
        }
        LittleEndian::write_u16(&mut self.buf[0..2], self.item_count);
        self.writer.write_bytes(&self.buf[..len]).await?;
        self.writer.skip(self.block_size as u64 - len as u64).await?;
        debug!(item_count = self.item_count, byte_count = len, "wrote items");
        self.buf.drain(..len - 2); // 2 bytes are used for the next item count.
        self.item_count = 0;
        Ok(())
    }
}

#[async_trait]
impl<W: WriteBytes + Send, K: Key, V: Value> LayerWriter<K, V>
    for SimplePersistentLayerWriter<W, K, V>
{
    async fn write(&mut self, item: ItemRef<'_, K, V>) -> Result<(), Error> {
        // Note the length before we write this item.
        let len = self.buf.len();
        item.key.serialize_into(&mut self.buf)?;
        item.value.serialize_into(&mut self.buf)?;
        self.buf.write_u64::<LittleEndian>(item.sequence)?;

        // If writing the item took us over a block, flush the bytes in the buffer prior to this
        // item.
        if self.buf.len() > self.block_size as usize - 1 || self.item_count == 65535 {
            self.write_some(len).await?;
        }

        self.item_count += 1;
        Ok(())
    }

    async fn flush(&mut self) -> Result<(), Error> {
        self.write_some(self.buf.len()).await?;
        self.writer.complete().await
    }
}

impl<W: WriteBytes, K: Key, V: Value> Drop for SimplePersistentLayerWriter<W, K, V> {
    fn drop(&mut self) {
        if self.item_count > 0 {
            warn!("Dropping unwritten items; did you forget to flush?");
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::{SimplePersistentLayer, SimplePersistentLayerWriter},
        crate::{
            lsm_tree::types::{DefaultOrdUpperBound, Item, ItemRef, Layer, LayerWriter},
            object_handle::Writer,
            testing::fake_object::{FakeObject, FakeObjectHandle},
        },
        fuchsia_async as fasync,
        std::{ops::Bound, sync::Arc},
    };

    impl DefaultOrdUpperBound for i32 {}

    #[fasync::run_singlethreaded(test)]
    async fn test_iterate_after_write() {
        const BLOCK_SIZE: u64 = 512;
        const ITEM_COUNT: i32 = 10000;

        let handle = FakeObjectHandle::new(Arc::new(FakeObject::new()));
        {
            let mut writer = SimplePersistentLayerWriter::<Writer<'_>, i32, i32>::new(
                Writer::new(&handle),
                BLOCK_SIZE,
            )
            .await
            .expect("writer new");
            for i in 0..ITEM_COUNT {
                writer.write(Item::new(i, i).as_item_ref()).await.expect("write failed");
            }
            writer.flush().await.expect("flush failed");
        }
        let layer = SimplePersistentLayer::open(handle).await.expect("new failed");
        let mut iterator = layer.seek(Bound::Unbounded).await.expect("seek failed");
        for i in 0..ITEM_COUNT {
            let ItemRef { key, value, .. } = iterator.get().expect("missing item");
            assert_eq!((key, value), (&i, &i));
            iterator.advance().await.expect("failed to advance");
        }
        assert!(iterator.get().is_none());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_seek_after_write() {
        const BLOCK_SIZE: u64 = 512;
        const ITEM_COUNT: i32 = 10000;

        let handle = FakeObjectHandle::new(Arc::new(FakeObject::new()));
        {
            let mut writer = SimplePersistentLayerWriter::<Writer<'_>, i32, i32>::new(
                Writer::new(&handle),
                BLOCK_SIZE,
            )
            .await
            .expect("writer new");
            for i in 0..ITEM_COUNT {
                writer.write(Item::new(i, i).as_item_ref()).await.expect("write failed");
            }
            writer.flush().await.expect("flush failed");
        }
        let layer = SimplePersistentLayer::open(handle).await.expect("new failed");
        for i in 0..ITEM_COUNT {
            let mut iterator = layer.seek(Bound::Included(&i)).await.expect("failed to seek");
            let ItemRef { key, value, .. } = iterator.get().expect("missing item");
            assert_eq!((key, value), (&i, &i));

            // Check that we can advance to the next item.
            iterator.advance().await.expect("failed to advance");
            if i == ITEM_COUNT - 1 {
                assert!(iterator.get().is_none());
            } else {
                let ItemRef { key, value, .. } = iterator.get().expect("missing item");
                let j = i + 1;
                assert_eq!((key, value), (&j, &j));
            }
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_seek_unbounded() {
        const BLOCK_SIZE: u64 = 512;
        const ITEM_COUNT: i32 = 10000;

        let handle = FakeObjectHandle::new(Arc::new(FakeObject::new()));
        {
            let mut writer = SimplePersistentLayerWriter::<Writer<'_>, i32, i32>::new(
                Writer::new(&handle),
                BLOCK_SIZE,
            )
            .await
            .expect("writer new");
            for i in 0..ITEM_COUNT {
                writer.write(Item::new(i, i).as_item_ref()).await.expect("write failed");
            }
            writer.flush().await.expect("flush failed");
        }
        let layer = SimplePersistentLayer::open(handle).await.expect("new failed");
        let mut iterator = layer.seek(Bound::Unbounded).await.expect("failed to seek");
        let ItemRef { key, value, .. } = iterator.get().expect("missing item");
        assert_eq!((key, value), (&0, &0));

        // Check that we can advance to the next item.
        iterator.advance().await.expect("failed to advance");
        let ItemRef { key, value, .. } = iterator.get().expect("missing item");
        assert_eq!((key, value), (&1, &1));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_zero_items() {
        const BLOCK_SIZE: u64 = 512;

        let handle = FakeObjectHandle::new(Arc::new(FakeObject::new()));
        {
            let mut writer = SimplePersistentLayerWriter::<Writer<'_>, i32, i32>::new(
                Writer::new(&handle),
                BLOCK_SIZE,
            )
            .await
            .expect("writer new");
            writer.flush().await.expect("flush failed");
        }

        let layer = SimplePersistentLayer::open(handle).await.expect("new failed");
        let iterator = (layer.as_ref() as &dyn Layer<i32, i32>)
            .seek(Bound::Unbounded)
            .await
            .expect("seek failed");
        assert!(iterator.get().is_none())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_large_block_size() {
        // Large enough such that we hit the 64k item limit.
        const BLOCK_SIZE: u64 = 2097152;
        const ITEM_COUNT: i32 = 70000;

        let handle =
            FakeObjectHandle::new_with_block_size(Arc::new(FakeObject::new()), BLOCK_SIZE as usize);
        {
            let mut writer = SimplePersistentLayerWriter::<Writer<'_>, i32, i32>::new(
                Writer::new(&handle),
                BLOCK_SIZE,
            )
            .await
            .expect("writer new");
            for i in 0..ITEM_COUNT {
                writer.write(Item::new(i, i).as_item_ref()).await.expect("write failed");
            }
            writer.flush().await.expect("flush failed");
        }

        let layer = SimplePersistentLayer::open(handle).await.expect("new failed");
        let mut iterator = layer.seek(Bound::Unbounded).await.expect("seek failed");
        for i in 0..ITEM_COUNT {
            let ItemRef { key, value, .. } = iterator.get().expect("missing item");
            assert_eq!((key, value), (&i, &i));
            iterator.advance().await.expect("failed to advance");
        }
        assert!(iterator.get().is_none());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_seek_bound_excluded() {
        const BLOCK_SIZE: u64 = 512;
        const ITEM_COUNT: i32 = 10000;

        let handle = FakeObjectHandle::new(Arc::new(FakeObject::new()));
        {
            let mut writer = SimplePersistentLayerWriter::<Writer<'_>, i32, i32>::new(
                Writer::new(&handle),
                BLOCK_SIZE,
            )
            .await
            .expect("writer new");
            for i in 0..ITEM_COUNT {
                writer.write(Item::new(i, i).as_item_ref()).await.expect("write failed");
            }
            writer.flush().await.expect("flush failed");
        }
        let layer = SimplePersistentLayer::open(handle).await.expect("new failed");

        for i in 0..ITEM_COUNT {
            let mut iterator = layer.seek(Bound::Excluded(&i)).await.expect("failed to seek");
            let i_plus_one = i + 1;
            if i_plus_one < ITEM_COUNT {
                let ItemRef { key, value, .. } = iterator.get().expect("missing item");

                assert_eq!((key, value), (&i_plus_one, &i_plus_one));

                // Check that we can advance to the next item.
                iterator.advance().await.expect("failed to advance");
                let i_plus_two = i + 2;
                if i_plus_two < ITEM_COUNT {
                    let ItemRef { key, value, .. } = iterator.get().expect("missing item");
                    assert_eq!((key, value), (&i_plus_two, &i_plus_two));
                } else {
                    assert!(iterator.get().is_none());
                }
            } else {
                assert!(iterator.get().is_none());
            }
        }
    }
}
