use {
    crate::{
        lsm_tree::{
            merge::{MergeIterator, MergeResult},
            skip_list_layer::SkipListLayer,
            types::{Item, Layer, MutableLayer, OrdLowerBound},
            LSMTree,
        },
        testing::fake_object::{FakeObject, FakeObjectHandle},
    },
    anyhow::Error,
    fuchsia_async as fasync,
    std::{
        ops::Bound,
        rc::Rc,
        sync::{Arc, Mutex},
    },
};

fn merge<K: std::fmt::Debug, V: std::fmt::Debug>(
    _left: &MergeIterator<'_, K, V>,
    _right: &MergeIterator<'_, K, V>,
) -> MergeResult<K, V> {
    MergeResult::EmitLeft
}

#[derive(Eq, PartialEq, PartialOrd, Ord, Debug, serde::Serialize, serde::Deserialize)]
struct TestKey(i32);

impl OrdLowerBound for TestKey {
    fn cmp_lower_bound(&self, other: &Self) -> std::cmp::Ordering {
        std::cmp::Ord::cmp(self, other)
    }
}

#[fasync::run_singlethreaded(test)]
async fn test_lsm_tree_commit() -> Result<(), Error> {
    let object1 = Arc::new(Mutex::new(FakeObject::new()));
    let object2 = Arc::new(Mutex::new(FakeObject::new()));
    let tree = LSMTree::<TestKey, u8>::new(merge);
    tree.insert(Item::new(TestKey(1), 2)).await;
    tree.insert(Item::new(TestKey(3), 4)).await;
    let object_handle = FakeObjectHandle::new(object1.clone());
    tree.seal();
    tree.compact(object_handle).await?;
    tree.insert(Item::new(TestKey(2), 5)).await;
    let object_handle = FakeObjectHandle::new(object2.clone());
    tree.seal();
    tree.compact(object_handle).await?;
    let mut merger = tree.range_from(std::ops::Bound::Unbounded).await?;
    assert_eq!(merger.get().unwrap().key, &TestKey(1));
    merger.advance().await?;
    assert_eq!(merger.get().unwrap().key, &TestKey(2));
    merger.advance().await?;
    assert_eq!(merger.get().unwrap().key, &TestKey(3));
    merger.advance().await?;
    assert!(merger.get().is_none());
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_skip_list() -> Result<(), Error> {
    let mut skip_list = Rc::new(SkipListLayer::new(100));
    let sl = Rc::get_mut(&mut skip_list).unwrap();
    sl.merge_into(Item::new(TestKey(1), 1), &TestKey(1), merge).await;
    {
        let mut iter = sl.get_iterator();
        iter.seek(Bound::Included(&TestKey(1))).await?;
        assert_eq!(iter.get().unwrap().key, &TestKey(1));
    }
    sl.merge_into(Item::new(TestKey(2), 1), &TestKey(2), merge).await;
    sl.merge_into(Item::new(TestKey(3), 1), &TestKey(3), merge).await;
    let mut iter = skip_list.get_iterator();
    iter.seek(Bound::Included(&TestKey(2))).await?;
    assert_eq!(iter.get().unwrap().key, &TestKey(2));
    iter.advance().await?;
    assert_eq!(iter.get().unwrap().key, &TestKey(3));
    iter.advance().await?;
    assert!(iter.get().is_none());
    let mut iter = skip_list.get_iterator();
    iter.seek(Bound::Included(&TestKey(1))).await?;
    assert_eq!(iter.get().unwrap().key, &TestKey(1));
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_skip_list_with_large_number_of_items() -> Result<(), Error> {
    let mut skip_list = Rc::new(SkipListLayer::new(100));
    let sl = Rc::get_mut(&mut skip_list).unwrap();
    let item_count = 10;
    for i in 1..item_count {
        sl.merge_into(Item::new(TestKey(i), 1), &TestKey(i), merge).await;
    }
    let mut iter = skip_list.get_iterator();
    iter.seek(Bound::Included(&TestKey(item_count - 2))).await?;
    for i in item_count - 2..item_count {
        assert_eq!(iter.get().unwrap().key, &TestKey(i));
        iter.advance().await?;
    }
    assert!(iter.get().is_none());
    Ok(())
}
