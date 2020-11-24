use {
    super::simple_persistent_layer::{SimplePersistentLayer, SimplePersistentLayerWriter},
    crate::{
        lsm_tree::{
            merge::{MergeIterator, MergeResult},
            skip_list_layer::SkipListLayer,
            BoxedLayerIterator, Item, ItemRef, LSMTree, Layer, MutableLayer, OrdLowerBound,
        },
        object_handle::ObjectHandle,
        object_store::Transaction,
    },
    anyhow::Error,
    std::{
        cmp::min,
        ops::{Bound, Range},
        rc::Rc,
        sync::{Arc, Mutex},
        vec::Vec,
    },
};

struct FakeObject {
    buf: Vec<u8>,
}

impl FakeObject {
    fn read(&self, offset: u64, buf: &mut [u8]) -> std::io::Result<usize> {
        println!(
            "reading at offset {:?} len {:?} object len {:?}",
            offset,
            buf.len(),
            self.buf.len()
        );
        let to_do = min(buf.len(), self.buf.len() - offset as usize);
        buf[0..to_do].copy_from_slice(&self.buf[offset as usize..offset as usize + to_do]);
        Ok(to_do)
    }
    fn write(&mut self, offset: u64, buf: &[u8]) -> std::io::Result<()> {
        let required_len = offset as usize + buf.len();
        if self.buf.len() < required_len {
            self.buf.resize(required_len, 0);
        }
        &self.buf[offset as usize..offset as usize + buf.len()].copy_from_slice(buf);
        Ok(())
    }
    fn get_size(&self) -> u64 {
        self.buf.len() as u64
    }
}

struct FakeObjectHandle {
    object: Arc<Mutex<FakeObject>>,
}

impl ObjectHandle for FakeObjectHandle {
    fn object_id(&self) -> u64 {
        0
    }
    fn read(&self, offset: u64, buf: &mut [u8]) -> std::io::Result<usize> {
        println!("reading at offset {:?} len {:?}", offset, buf.len());
        self.object.lock().unwrap().read(offset, buf)
    }
    fn write(&self, offset: u64, buf: &[u8]) -> std::io::Result<()> {
        self.object.lock().unwrap().write(offset, buf)
    }
    fn get_size(&self) -> u64 {
        self.object.lock().unwrap().get_size()
    }
    fn preallocate_range(
        &self,
        _range: Range<u64>,
        _transaction: &mut Transaction,
    ) -> std::io::Result<Vec<Range<u64>>> {
        Ok(vec![])
    }
}

#[test]
fn test_simple_persisitent_layer_write() -> Result<(), Error> {
    let object = Arc::new(Mutex::new(FakeObject { buf: Vec::new() }));
    let mut object_handle = FakeObjectHandle { object: object.clone() };
    let mut writer = SimplePersistentLayerWriter::new(&mut object_handle, 512);
    writer.write(ItemRef::from(&Item { key: TestKey(1), value: 1 }))?;
    writer.write(ItemRef::from(&Item { key: TestKey(3), value: 1 }))?;
    writer.close()?;
    let layer =
        Rc::new(SimplePersistentLayer::new(FakeObjectHandle { object: object.clone() }, 512));
    let mut iterator: BoxedLayerIterator<'_, TestKey, i32> = layer.get_iterator();
    iterator.seek(Bound::Included(&TestKey(1)))?;
    assert_eq!(iterator.get().unwrap().key, &TestKey(1));
    assert_eq!(iterator.get().unwrap().value, &1);
    iterator.advance()?;
    assert_eq!(iterator.get().unwrap().key, &TestKey(3));
    assert_eq!(iterator.get().unwrap().value, &1);
    iterator.advance()?;
    assert!(iterator.get().is_none());
    Ok(())
}

#[test]
fn test_simple_persisitent_layer_write_many_items() -> Result<(), Error> {
    let object = Arc::new(Mutex::new(FakeObject { buf: Vec::new() }));
    let mut object_handle = FakeObjectHandle { object: object.clone() };
    let mut writer = SimplePersistentLayerWriter::new(&mut object_handle, 512);
    for i in 1..20000 {
        writer.write(ItemRef::from(&Item { key: TestKey(i), value: 1 }))?;
    }
    writer.close()?;
    let layer =
        Rc::new(SimplePersistentLayer::new(FakeObjectHandle { object: object.clone() }, 512));
    let mut iterator: BoxedLayerIterator<'_, TestKey, TestKey> = layer.get_iterator();
    iterator.seek(Bound::Included(&TestKey(19950)))?;
    assert_eq!(iterator.get().unwrap().key, &TestKey(19950));
    iterator.advance()?;
    assert_eq!(iterator.get().unwrap().key, &TestKey(19951));
    Ok(())
}

fn merge<K: std::fmt::Debug, V: std::fmt::Debug>(
    left: &MergeIterator<'_, K, V>,
    right: &MergeIterator<'_, K, V>,
) -> MergeResult<K, V> {
    println!("merging {:?} {:?}", left.item().key, right.item().key);
    MergeResult::EmitLeft
}

#[derive(Eq, PartialEq, PartialOrd, Ord, Debug, serde::Serialize, serde::Deserialize)]
struct TestKey(i32);

impl OrdLowerBound for TestKey {
    fn cmp_lower_bound(&self, other: &Self) -> std::cmp::Ordering {
        std::cmp::Ord::cmp(self, other)
    }
}

#[test]
fn test_lsm_tree_commit() -> Result<(), Error> {
    let object1 = Arc::new(Mutex::new(FakeObject { buf: Vec::new() }));
    let object2 = Arc::new(Mutex::new(FakeObject { buf: Vec::new() }));
    let tree = LSMTree::new(merge);
    tree.insert(Item::new(TestKey(1), 2));
    tree.insert(Item::new(TestKey(3), 4));
    let object_handle = FakeObjectHandle { object: object1.clone() };
    tree.commit(object_handle)?;
    tree.insert(Item::new(TestKey(2), 5));
    let object_handle = FakeObjectHandle { object: object2.clone() };
    tree.commit(object_handle)?;
    let mut merger = tree.range_from(std::ops::Bound::Unbounded)?;
    assert_eq!(merger.get().unwrap().key, &TestKey(1));
    merger.advance()?;
    assert_eq!(merger.get().unwrap().key, &TestKey(2));
    merger.advance()?;
    assert_eq!(merger.get().unwrap().key, &TestKey(3));
    merger.advance()?;
    assert!(merger.get().is_none());
    Ok(())
}

#[test]
fn test_skip_list() -> Result<(), Error> {
    let mut skip_list = Rc::new(SkipListLayer::new(100));
    let sl = Rc::get_mut(&mut skip_list).unwrap();
    sl.replace_range(Item::new(TestKey(1), 1), &TestKey(1), merge);
    {
        let mut iter = sl.get_iterator();
        iter.seek(Bound::Included(&TestKey(1)))?;
        assert_eq!(iter.get().unwrap().key, &TestKey(1));
    }
    sl.replace_range(Item::new(TestKey(2), 1), &TestKey(2), merge);
    sl.replace_range(Item::new(TestKey(3), 1), &TestKey(3), merge);
    let mut iter = skip_list.get_iterator();
    iter.seek(Bound::Included(&TestKey(2)))?;
    assert_eq!(iter.get().unwrap().key, &TestKey(2));
    iter.advance()?;
    assert_eq!(iter.get().unwrap().key, &TestKey(3));
    iter.advance()?;
    assert!(iter.get().is_none());
    let mut iter = skip_list.get_iterator();
    iter.seek(Bound::Included(&TestKey(1)))?;
    assert_eq!(iter.get().unwrap().key, &TestKey(1));
    Ok(())
}

#[test]
fn test_skip_list_with_large_number_of_items() -> Result<(), Error> {
    let mut skip_list = Rc::new(SkipListLayer::new(100));
    let sl = Rc::get_mut(&mut skip_list).unwrap();
    let item_count = 10;
    for i in 1..item_count {
        sl.replace_range(Item::new(TestKey(i), 1), &TestKey(i), merge);
    }
    let mut iter = skip_list.get_iterator();
    iter.seek(Bound::Included(&TestKey(item_count - 2)))?;
    for i in item_count - 2..item_count {
        assert_eq!(iter.get().unwrap().key, &TestKey(i));
        iter.advance()?;
    }
    assert!(iter.get().is_none());
    Ok(())
}
