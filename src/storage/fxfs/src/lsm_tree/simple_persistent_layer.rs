use {
    crate::lsm_tree::{BoxedLayerIterator, Item, ItemRef, Key, Layer, LayerIterator, Value},
    crate::object_handle::{ObjectHandle, ObjectHandleCursor},
    anyhow::Error,
    byteorder::{ByteOrder, LittleEndian, ReadBytesExt},
    serde::Serialize,
    std::io::{BufReader, ErrorKind},
    std::ops::Bound,
    std::{
        cell::RefCell,
        ops::Deref,
        pin::Pin,
        ptr::NonNull,
        sync::{Arc, Mutex},
        vec::Vec,
    },
};

pub struct SimplePersistentLayer {
    inner: Mutex<Inner>,
}

struct Inner {
    object_handle: Arc<dyn ObjectHandle>,
    block_size: u16,
}

pub struct Iterator<'iter, K, V> {
    layer: &'iter SimplePersistentLayer,
    pos: u64,
    block_size: u16,
    reader: Option<BufReader<ObjectHandleCursor<'iter, Arc<dyn ObjectHandle + 'iter>>>>,
    item_index: u16,
    item_count: u16,
    item: Option<Item<K, V>>,
}

impl<K, V> Iterator<'_, K, V> {
    fn new<'iter>(
        layer: &'iter SimplePersistentLayer,
        pos: u64,
        block_size: u16,
    ) -> Iterator<'iter, K, V> {
        Iterator {
            layer,
            pos,
            block_size,
            reader: None,
            item_index: 0,
            item_count: 0,
            item: None,
        }
    }
}

impl<'iter, K: Key, V: Value> LayerIterator<K, V> for Iterator<'iter, K, V>
{
    fn seek(&mut self, bound: std::ops::Bound<&K>) -> Result<(), Error> {
        let key;
        self.pos = 0;
        self.item_index = 0;
        self.item_count = 0;
        match bound {
            Bound::Unbounded => {
                self.advance()?;
                return Ok(());
            }
            Bound::Included(k) => {
                key = k;
            }
            Bound::Excluded(_) => panic!("Excluded bound not supported"),
        }
        let mut left_offset = 0;
        let mut right_offset =
            round_up(self.layer.inner.lock().unwrap().object_handle.get_size(), self.block_size);
        self.advance()?;
        match self.get() {
            None => {
                return Ok(());
            }
            Some(item) => {
                if item.key >= key {
                    return Ok(());
                }
            }
        }
        while right_offset - left_offset > self.block_size as u64 {
            // Pick a block midway.
            let mid_offset =
                round_down(left_offset + (right_offset - left_offset) / 2, self.block_size);
            let mut iterator = Iterator::new(self.layer, mid_offset, self.block_size);
            iterator.advance()?;
            if iterator.get().unwrap().key >= key {
                right_offset = mid_offset;
            } else {
                left_offset = mid_offset;
                *self = iterator;
            }
        }
        // At this point, we know that left_key < key and right_key >= key, so we have to iterate
        // through left_key to find the key we want.
        loop {
            self.advance()?;
            match self.get() {
                None => {
                    return Ok(());
                }
                Some(item) => {
                    if item.key >= key {
                        return Ok(());
                    }
                }
            }
        }
    }

    fn advance(&mut self) -> Result<(), Error> {
        if self.item_index >= self.item_count {
            if self.pos >= self.layer.inner.lock().unwrap().object_handle.get_size() {
                self.item = None;
                return Ok(());
            }
            let mut reader = BufReader::new(ObjectHandleCursor::new(
                self.layer.inner.lock().unwrap().object_handle.clone(),
                self.pos,
            ));
            self.item_count = reader.read_u16::<LittleEndian>()?;
            if self.item_count == 0 {
                return Err(Error::from(std::io::Error::new(
                    ErrorKind::InvalidData,
                    "Bad layer file: zero item count",
                )));
            }
            self.reader = Some(reader);
            // self.deserializer = Some(Deserializer::with_reader(reader, serializer_options()));
            self.pos += self.block_size as u64;
            self.item_index = 0;
        }
        self.item = Some(bincode::deserialize_from(self.reader.as_mut().unwrap())?);
        // Item::deserialize(self.deserializer.as_mut().unwrap())?);
        self.item_index += 1;
        Ok(())
    }

    fn get(&self) -> Option<ItemRef<'_, K, V>> {
        return self.item.as_ref().map(<&Item<K, V>>::into);
    }

    fn discard_or_advance(&mut self) -> Result<(), Error> {
        self.advance()
    }
}

fn round_down(value: u64, block_size: u16) -> u64 {
    value - value % block_size as u64
}

fn round_up(value: u64, block_size: u16) -> u64 {
    round_down(value + block_size as u64 - 1, block_size)
}

impl SimplePersistentLayer {
    pub fn new(
        object_handle: impl ObjectHandle + 'static,
        block_size: u16,
    ) -> SimplePersistentLayer {
        SimplePersistentLayer {
            inner: Mutex::new(Inner { object_handle: Arc::new(object_handle), block_size }),
        }
    }
}

impl<K: Key, V: Value> Layer<K, V> for SimplePersistentLayer
{
    fn get_iterator(&self) -> BoxedLayerIterator<'_, K, V> {
        Box::new(Iterator::new(self, 0, self.inner.lock().unwrap().block_size))
    }
}

// -- Writer support --

struct BufWriter {
    buf: NonNull<RefCell<Vec<u8>>>,
}

impl std::io::Write for BufWriter {
    fn write(&mut self, buf: &[u8]) -> std::io::Result<usize> {
        unsafe { self.buf.as_ref().borrow_mut().write(buf) }
    }
    fn flush(&mut self) -> std::io::Result<()> {
        unsafe { self.buf.as_ref().borrow_mut().flush() }
    }
}

pub struct SimplePersistentLayerWriter<'handle> {
    block_size: u16,
    buf: Pin<Box<RefCell<Vec<u8>>>>,
    // serializer: Serializer<BufWriter, bincode::DefaultOptions>,
    writer: BufWriter,
    object_handle: &'handle mut dyn ObjectHandle,
    item_count: u16,
    offset: u64,
}

impl<'handle> SimplePersistentLayerWriter<'handle> {
    pub fn new(
        object_handle: &'handle mut dyn ObjectHandle,
        block_size: u16,
    ) -> SimplePersistentLayerWriter<'handle> {
        let buf = Box::pin(RefCell::new(vec![0; 2]));
        let writer = BufWriter { buf: NonNull::from(buf.deref()) };
        SimplePersistentLayerWriter {
            block_size,
            buf: buf,
            writer,
            // serializer: Serializer::new(buf_writer, serializer_options()),
            object_handle,
            item_count: 0,
            offset: 0,
        }
    }

    pub fn write<K: Serialize, V: Serialize>(
        &mut self,
        item: ItemRef<'_, K, V>,
    ) -> Result<(), Error> {
        let len = self.buf.borrow().len();
        bincode::serialize_into(&mut self.writer, &item)?;
        if self.buf.borrow().len() > self.block_size as usize - 1 || self.item_count == 65535 {
            self.flush(len)?;
        }
        self.item_count += 1;
        Ok(())
    }

    fn flush(&mut self, len: usize) -> Result<(), Error> {
        println!("flushing {:?}", len);
        let mut buf = (*self.buf).borrow_mut();
        LittleEndian::write_u16(&mut &mut buf[0..2], self.item_count);
        self.object_handle.write(self.offset, &buf[..len])?;
        buf.drain(..len - 2); // 2 bytes are used for the next item count.
        self.item_count = 0;
        self.offset += self.block_size as u64;
        Ok(())
    }

    pub fn close(&mut self) -> Result<(), Error> {
        println!("closing");
        let len = self.buf.borrow().len();
        self.flush(len)
    }
}

// TODO(csuter): Edge case: write o entries?
