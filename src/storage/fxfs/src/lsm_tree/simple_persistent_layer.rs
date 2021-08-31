// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        lsm_tree::types::{
            BoxedLayerIterator, Item, ItemRef, Key, Layer, LayerIterator, LayerWriter, Value,
        },
        object_handle::{ReadObjectHandle, WriteBytes},
    },
    anyhow::{bail, Context, Error},
    async_trait::async_trait,
    async_utils::event::Event,
    byteorder::{ByteOrder, LittleEndian, ReadBytesExt},
    serde::Serialize,
    std::{
        cmp::Ordering,
        ops::{Bound, Drop},
        sync::{Arc, Mutex},
        vec::Vec,
    },
};

/// Implements a very primitive persistent layer where items are packed into blocks and searching
/// for items is done via a simple binary search. Each block starts with a 2 byte item count so
/// there is a 64k item limit per block.
pub struct SimplePersistentLayer {
    object_handle: Arc<dyn ReadObjectHandle>,
    block_size: u32,
    size: u64,
    close_event: Mutex<Option<Event>>,
}

pub struct Iterator<'iter, K, V> {
    layer: &'iter SimplePersistentLayer,

    // The position of the _next_ block to be read.
    pos: u64,

    // A cursor contiaining the most recently read block.
    reader: Option<std::io::Cursor<Box<[u8]>>>,

    // The item index in the current block.
    item_index: u16,

    // The number of items in the current block.
    item_count: u16,

    // The current item.
    item: Option<Item<K, V>>,
}

impl<K, V> Iterator<'_, K, V> {
    fn new<'iter>(layer: &'iter SimplePersistentLayer, pos: u64) -> Iterator<'iter, K, V> {
        Iterator { layer, pos, reader: None, item_index: 0, item_count: 0, item: None }
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
            // TODO(jfsulliv): Reuse this transfer buffer.
            let bs = self.layer.block_size as usize;
            let mut buf = self.layer.object_handle.allocate_buffer(bs);
            let len = self.layer.object_handle.read(self.pos, buf.as_mut()).await?;
            log::debug!(
                "pos={}, object size={}, object id={}",
                self.pos,
                self.layer.size,
                self.layer.object_handle.object_id()
            );
            let mut reader = std::io::Cursor::new(buf.as_slice()[..len].to_vec().into());
            self.item_count = reader.read_u16::<LittleEndian>()?;
            if self.item_count == 0 {
                bail!(
                    "Read block with zero item count (object: {}, offset: {})",
                    self.layer.object_handle.object_id(),
                    self.pos
                );
            }
            self.reader = Some(reader);
            self.pos += self.layer.block_size as u64;
            self.item_index = 0;
        }
        self.item = Some(
            bincode::deserialize_from(self.reader.as_mut().unwrap()).context("Corrupt layer")?,
        );
        self.item_index += 1;
        Ok(())
    }

    fn get(&self) -> Option<ItemRef<'_, K, V>> {
        return self.item.as_ref().map(<&Item<K, V>>::into);
    }
}

fn round_down(value: u64, block_size: u32) -> u64 {
    value - value % block_size as u64
}

fn round_up(value: u64, block_size: u32) -> u64 {
    round_down(value + block_size as u64 - 1, block_size)
}

impl SimplePersistentLayer {
    /// Opens an existing layer that is accessible via |object_handle| (which provides a read
    /// interface to the object).  The layer should have been written prior using
    /// SimplePersistentLayerWriter.
    pub async fn open(
        object_handle: impl ReadObjectHandle + 'static,
        block_size: u32,
    ) -> Result<Arc<Self>, Error> {
        let size = object_handle.get_size();
        Ok(Arc::new(SimplePersistentLayer {
            object_handle: Arc::new(object_handle),
            block_size,
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
        let (key, excluded) = match bound {
            Bound::Unbounded => {
                let mut iterator = Iterator::new(self, 0);
                iterator.advance().await?;
                return Ok(Box::new(iterator));
            }
            Bound::Included(k) => (k, false),
            Bound::Excluded(k) => (k, true),
        };
        let mut left_offset = 0;
        let mut right_offset = round_up(self.size, self.block_size);
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
        while right_offset - left_offset > self.block_size as u64 {
            // Pick a block midway.
            let mid_offset =
                round_down(left_offset + (right_offset - left_offset) / 2, self.block_size);
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
}

// -- Writer support --

pub struct SimplePersistentLayerWriter<W> {
    block_size: u32,
    buf: Vec<u8>,
    writer: W,
    item_count: u16,
}

impl<W: WriteBytes> SimplePersistentLayerWriter<W> {
    /// Creates a new writer that will serialize items to the object accessible via |object_handle|
    /// (which provdes a write interface to the object).
    pub fn new(writer: W, block_size: u32) -> Self {
        SimplePersistentLayerWriter { block_size, buf: vec![0; 2], writer, item_count: 0 }
    }

    async fn write_some(&mut self, len: usize) -> Result<(), Error> {
        if self.item_count == 0 {
            return Ok(());
        }
        LittleEndian::write_u16(&mut self.buf[0..2], self.item_count);
        self.writer.write_bytes(&self.buf[..len]).await?;
        self.writer.skip(self.block_size as u64 - len as u64);
        log::debug!("wrote {} items, {} bytes", self.item_count, len);
        self.buf.drain(..len - 2); // 2 bytes are used for the next item count.
        self.item_count = 0;
        Ok(())
    }
}

#[async_trait]
impl<W: WriteBytes + Send> LayerWriter for SimplePersistentLayerWriter<W> {
    async fn write<K: Send + Serialize + Sync, V: Send + Serialize + Sync>(
        &mut self,
        item: ItemRef<'_, K, V>,
    ) -> Result<(), Error> {
        // TODO(jfsulliv): Simplify all of this by implementing std::io::Writer for WriteBytes.
        // Note the length before we write this item.
        let len = self.buf.len();
        bincode::serialize_into(&mut self.buf, &item)?;

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

impl<W> Drop for SimplePersistentLayerWriter<W> {
    fn drop(&mut self) {
        if self.item_count > 0 {
            log::warn!("Dropping unwritten items; did you forget to flush?");
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
        const BLOCK_SIZE: u32 = 512;
        const ITEM_COUNT: i32 = 10000;

        let handle = FakeObjectHandle::new(Arc::new(FakeObject::new()));
        {
            let mut writer = SimplePersistentLayerWriter::new(Writer::new(&handle), BLOCK_SIZE);
            for i in 0..ITEM_COUNT {
                writer.write(Item::new(i, i).as_item_ref()).await.expect("write failed");
            }
            writer.flush().await.expect("flush failed");
        }
        let layer = SimplePersistentLayer::open(handle, BLOCK_SIZE).await.expect("new failed");
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
        const BLOCK_SIZE: u32 = 512;
        const ITEM_COUNT: i32 = 10000;

        let handle = FakeObjectHandle::new(Arc::new(FakeObject::new()));
        {
            let mut writer = SimplePersistentLayerWriter::new(Writer::new(&handle), BLOCK_SIZE);
            for i in 0..ITEM_COUNT {
                writer.write(Item::new(i, i).as_item_ref()).await.expect("write failed");
            }
            writer.flush().await.expect("flush failed");
        }
        let layer = SimplePersistentLayer::open(handle, BLOCK_SIZE).await.expect("new failed");
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
        const BLOCK_SIZE: u32 = 512;
        const ITEM_COUNT: i32 = 10000;

        let handle = FakeObjectHandle::new(Arc::new(FakeObject::new()));
        {
            let mut writer = SimplePersistentLayerWriter::new(Writer::new(&handle), BLOCK_SIZE);
            for i in 0..ITEM_COUNT {
                writer.write(Item::new(i, i).as_item_ref()).await.expect("write failed");
            }
            writer.flush().await.expect("flush failed");
        }
        let layer = SimplePersistentLayer::open(handle, BLOCK_SIZE).await.expect("new failed");
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
        const BLOCK_SIZE: u32 = 512;

        let handle = FakeObjectHandle::new(Arc::new(FakeObject::new()));
        {
            let mut writer = SimplePersistentLayerWriter::new(Writer::new(&handle), BLOCK_SIZE);
            writer.flush().await.expect("flush failed");
        }

        let layer = SimplePersistentLayer::open(handle, BLOCK_SIZE).await.expect("new failed");
        let iterator = (layer.as_ref() as &dyn Layer<i32, i32>)
            .seek(Bound::Unbounded)
            .await
            .expect("seek failed");
        assert!(iterator.get().is_none())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_large_block_size() {
        // Large enough such that we hit the 64k item limit.
        const BLOCK_SIZE: u32 = 2097152;
        const ITEM_COUNT: i32 = 70000;

        let handle = FakeObjectHandle::new(Arc::new(FakeObject::new()));
        {
            let mut writer = SimplePersistentLayerWriter::new(Writer::new(&handle), BLOCK_SIZE);
            for i in 0..ITEM_COUNT {
                writer.write(Item::new(i, i).as_item_ref()).await.expect("write failed");
            }
            writer.flush().await.expect("flush failed");
        }

        let layer = SimplePersistentLayer::open(handle, BLOCK_SIZE).await.expect("new failed");
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
        const BLOCK_SIZE: u32 = 512;
        const ITEM_COUNT: i32 = 10000;

        let handle = FakeObjectHandle::new(Arc::new(FakeObject::new()));
        {
            let mut writer = SimplePersistentLayerWriter::new(Writer::new(&handle), BLOCK_SIZE);
            for i in 0..ITEM_COUNT {
                writer.write(Item::new(i, i).as_item_ref()).await.expect("write failed");
            }
            writer.flush().await.expect("flush failed");
        }
        let layer = SimplePersistentLayer::open(handle, BLOCK_SIZE).await.expect("new failed");

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
