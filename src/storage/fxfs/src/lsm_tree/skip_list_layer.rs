// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// There are a great many optimisations that could be considered to improve performance and maybe
// memory usage.

use {
    crate::lsm_tree::{
        merge::{MergeFn, Merger},
        types::{
            BoxedLayerIterator, Item, ItemRef, Key, Layer, LayerIterator, LayerIteratorMut,
            MutableLayer, Value,
        },
    },
    anyhow::Error,
    async_trait::async_trait,
    std::{
        cmp::min,
        ops::Bound,
        sync::{Arc, RwLock, RwLockReadGuard, RwLockWriteGuard},
    },
};

// Each skip list node contains a variable sized pointer list. The head pointers also exist in the
// form of a pointer list. Index 0 in the pointer list is the chain with the most elements i.e.
// contains every element in the list.
#[derive(Debug)]
struct PointerList<K: Key, V: Value>(Box<[*mut SkipListNode<K, V>]>);

impl<K: Key, V: Value> PointerList<K, V> {
    fn new(count: usize) -> PointerList<K, V> {
        PointerList(vec![std::ptr::null_mut(); count].into_boxed_slice())
    }

    fn len(&self) -> usize {
        self.0.len()
    }

    // Extracts the pointer at the given index.
    fn get_mut<'a>(&self, index: usize) -> Option<&'a mut SkipListNode<K, V>> {
        unsafe { self.0[index].as_mut() }
    }

    // Same as previous, but returns an immutable reference.
    fn get<'a>(&self, index: usize) -> Option<&'a SkipListNode<K, V>> {
        unsafe { self.0[index].as_ref() }
    }

    // Sets the pointer at the given index.
    fn set(&mut self, index: usize, node: Option<&mut SkipListNode<K, V>>) {
        self.0[index] = match node {
            None => std::ptr::null_mut(),
            Some(mut node) => *&mut node,
        };
    }
}

unsafe impl<K: Key, V: Value> Send for PointerList<K, V> {}
unsafe impl<K: Key, V: Value> Sync for PointerList<K, V> {}

struct SkipListNode<K: Key, V: Value> {
    item: Item<K, V>,
    pointers: PointerList<K, V>,
}

pub struct SkipListLayer<K: Key, V: Value> {
    pointers: RwLock<PointerList<K, V>>,
}

impl<K: Key, V: Value> SkipListLayer<K, V> {
    pub fn new(max_item_count: usize) -> SkipListLayer<K, V> {
        SkipListLayer {
            pointers: RwLock::new(PointerList::new((max_item_count as f32).log2() as usize + 1)),
        }
    }

    // Erases the given item. Does nothing if the item doesn't exist.
    pub async fn erase(&self, item: ItemRef<'_, K, V>)
    where
        K: std::cmp::Eq,
    {
        let mut iter = SkipListLayerIterMut::new(self);
        iter.seek_sync(Bound::Included(&item.key));
        if let Some(ItemRef { key, .. }) = iter.get() {
            if key == item.key {
                iter.erase();
            }
        }
    }
}

// We have to manually manage memory.
impl<K: Key, V: Value> Drop for SkipListLayer<K, V> {
    fn drop(&mut self) {
        let mut next = self.pointers.write().unwrap().get_mut(0);
        while let Some(node) = next {
            unsafe {
                next = Box::from_raw(node).pointers.get_mut(0);
            }
        }
    }
}

impl<'layer, K: Key, V: Value> Layer<K, V> for SkipListLayer<K, V> {
    fn get_iterator(&self) -> BoxedLayerIterator<'_, K, V> {
        Box::new(SkipListLayerIter::new(self))
    }
}

#[async_trait]
impl<'layer, K: Key, V: Value> MutableLayer<K, V> for SkipListLayer<K, V> {
    fn as_layer(self: Arc<Self>) -> Arc<dyn Layer<K, V>> {
        self
    }

    // Inserts the given item.
    async fn insert(&self, item: Item<K, V>) {
        let mut iter = SkipListLayerIterMut::new(self);
        iter.seek_sync(Bound::Included(&item.key));
        iter.insert_before(item);
    }

    // Replaces or inserts the given item.
    async fn replace_or_insert(&self, item: Item<K, V>) {
        let mut iter = SkipListLayerIterMut::new(self);
        iter.seek_sync(Bound::Included(&item.key));
        if let Some(found_item) = iter.get_mut() {
            if found_item.key == item.key {
                *found_item = item;
                return;
            }
        }
        iter.insert_before(item);
    }

    async fn merge_into(&self, item: Item<K, V>, lower_bound: &K, merge_fn: MergeFn<K, V>) {
        Merger::merge_into(Box::new(SkipListLayerIterMut::new(self)), item, lower_bound, merge_fn)
            .await
            .unwrap();
    }
}

// -- SkipListLayerIter --

struct SkipListLayerIter<'iter, K: Key, V: Value> {
    pointers: RwLockReadGuard<'iter, PointerList<K, V>>,
    // Points to the pointer list immediately prior to the iterator location, or null if at the
    // beginning.
    prev_pointers: *const PointerList<K, V>,
}

impl<'iter, K: Key, V: Value> SkipListLayerIter<'iter, K, V> {
    fn new(skip_list: &'iter SkipListLayer<K, V>) -> SkipListLayerIter<'iter, K, V> {
        SkipListLayerIter {
            pointers: skip_list.pointers.read().unwrap(),
            prev_pointers: std::ptr::null(),
        }
    }

    // A non-async version of seek.
    fn seek_sync(&mut self, bound: std::ops::Bound<&K>) {
        match bound {
            Bound::Unbounded => self.prev_pointers = &*self.pointers,
            Bound::Included(key) => {
                let mut index = self.pointers.len() - 1;
                let mut last_pointers = &*self.pointers;
                loop {
                    // We could optimise for == if we could be bothered (via a different method
                    // maybe).
                    if let Some(node) = last_pointers.get(index) {
                        // Keep iterating along this level until we encounter a key that's >= our
                        // search key.
                        if &node.item.key < key {
                            last_pointers = &node.pointers;
                            continue;
                        }
                    }
                    // There are no more levels so we are done.
                    if index == 0 {
                        self.prev_pointers = last_pointers;
                        break;
                    }
                    // Move to the next level.
                    index -= 1;
                }
            }
            Bound::Excluded(_) => panic!("Excluded bounds not supported"),
        }
    }
}

unsafe impl<K: Key, V: Value> Send for SkipListLayerIter<'_, K, V> {}

#[async_trait]
impl<K: Key, V: Value> LayerIterator<K, V> for SkipListLayerIter<'_, K, V> {
    async fn seek(&mut self, bound: std::ops::Bound<&K>) -> Result<(), Error> {
        self.seek_sync(bound);
        Ok(())
    }

    async fn advance(&mut self) -> Result<(), Error> {
        match unsafe { self.prev_pointers.as_ref() } {
            None => self.seek_sync(Bound::Unbounded),
            Some(pointers) => {
                if let Some(next) = pointers.get(0) {
                    self.prev_pointers = &next.pointers;
                }
            }
        }
        Ok(())
    }

    fn get(&self) -> Option<ItemRef<'_, K, V>> {
        unsafe { self.prev_pointers.as_ref() }
            .and_then(|pointers| pointers.get(0))
            .map(|node| node.item.as_item_ref())
    }
}

// -- SkipListLayerIterMut --

struct SkipListLayerIterMut<'iter, K: Key, V: Value> {
    pointers: RwLockWriteGuard<'iter, PointerList<K, V>>,

    // Since this is a mutable iterator, we need to keep pointers to all the nodes that precede the
    // current position at every level, so that we can update them when inserting or erasing
    // elements.
    prev_pointers: Option<Box<[*mut PointerList<K, V>]>>,
}

impl<K: Key, V: Value> SkipListLayerIterMut<'_, K, V> {
    fn new(skip_list: &SkipListLayer<K, V>) -> SkipListLayerIterMut<'_, K, V> {
        SkipListLayerIterMut { pointers: skip_list.pointers.write().unwrap(), prev_pointers: None }
    }

    fn get_mut(&self) -> Option<&mut Item<K, V>> {
        match self.prev_pointers {
            None => None,
            Some(ref pointers) => unsafe { (*pointers[0]).get_mut(0).map(|node| &mut node.item) },
        }
    }

    // A non-async version of seek.
    fn seek_sync(&mut self, bound: std::ops::Bound<&K>) {
        let len = self.pointers.len();

        // Start by setting all the previous pointers to the head.
        //
        // To understand how the previous pointers work, imagine the list looks something like the
        // following:
        //
        // 2  |--->|
        // 1  |--->|--|------->|
        // 0  |--->|--|--|--|->|
        //  HEAD   A  B  C  D  E  F
        //
        // Now imagine that the iterator is pointing at element D. In that case, the previous
        // pointers will point at C for index 0, B for index 1 and A for index 2. With that
        // information, it will be possible to insert an element immediately prior to D and
        // correctly update as many pointers as required (remember a new element will be given a
        // random number of levels).
        self.prev_pointers = Some(vec![&mut *self.pointers as *mut _; len].into_boxed_slice());
        match bound {
            Bound::Unbounded => {}
            Bound::Included(key) => {
                let mut index = len - 1;
                let mut p = self.pointers.get_mut(index);
                let pointers = self.prev_pointers.as_mut().unwrap();
                loop {
                    // We could optimise for == if we could be bothered (via a different method
                    // maybe).
                    if let Some(node) = p {
                        if &(node.item.key) < key {
                            pointers[index] = &mut node.pointers;
                            p = node.pointers.get_mut(index);
                            continue;
                        }
                    }
                    // There are no more levels so we are done.
                    if index == 0 {
                        break;
                    }
                    // Move to the next level.
                    index -= 1;
                    pointers[index] = pointers[index + 1];
                    unsafe {
                        p = (*pointers[index]).get_mut(index);
                    }
                }
            }
            Bound::Excluded(_) => panic!("Excluded bounds not supported"),
        }
    }
}

unsafe impl<K: Key, V: Value> Send for SkipListLayerIterMut<'_, K, V> {}

#[async_trait]
impl<K: Key, V: Value> LayerIterator<K, V> for SkipListLayerIterMut<'_, K, V> {
    async fn seek(&mut self, bound: std::ops::Bound<&K>) -> Result<(), Error> {
        self.seek_sync(bound);
        Ok(())
    }

    async fn advance(&mut self) -> Result<(), Error> {
        let pointers = self.prev_pointers.as_mut().unwrap();
        if let Some(next) = unsafe { (*pointers[0]).get_mut(0) } {
            for i in 0..next.pointers.len() {
                pointers[i] = &mut next.pointers;
            }
        }
        Ok(())
    }

    fn get(&self) -> Option<ItemRef<'_, K, V>> {
        self.prev_pointers
            .as_ref()
            .and_then(|pointers| unsafe { pointers[0].as_ref() })
            .and_then(|pointers| pointers.get(0))
            .map(|node| node.item.as_item_ref())
    }
}

impl<K: Key, V: Value> LayerIteratorMut<K, V> for SkipListLayerIterMut<'_, K, V> {
    fn as_iterator_mut(&mut self) -> &mut dyn LayerIterator<K, V> {
        self
    }
    fn as_iterator(&self) -> &dyn LayerIterator<K, V> {
        self
    }

    fn insert_before(&mut self, item: Item<K, V>) {
        use rand::Rng;
        let mut rng = rand::thread_rng();
        let max_pointers = self.pointers.len();
        // This chooses a random number of pointers such that each level has half the number of
        // pointers of the previous one.
        let pointer_count = max_pointers
            - min(
                (rng.gen_range(0, 2u32.pow(max_pointers as u32) - 1) as f32).log2() as usize,
                max_pointers - 1,
            );
        let mut node =
            Box::leak(Box::new(SkipListNode { item, pointers: PointerList::new(pointer_count) }));
        for i in 0..pointer_count {
            let pointers = unsafe { &mut *self.prev_pointers.as_mut().unwrap()[i] };
            node.pointers.set(i, pointers.get_mut(i));
            pointers.set(i, Some(&mut node));
        }
    }

    fn erase(&mut self) {
        unsafe {
            let pointers = self.prev_pointers.as_ref().unwrap();
            if let Some(next) = (*pointers[0]).get_mut(0) {
                for i in 0..next.pointers.len() {
                    (*pointers[i]).set(i, next.pointers.get_mut(i));
                }
                Box::from_raw(next);
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::SkipListLayer,
        crate::lsm_tree::{
            merge::{
                ItemOp::{Discard, Replace},
                MergeIterator, MergeResult,
            },
            types::{Item, ItemRef, Layer, MutableLayer, OrdLowerBound},
        },
        fuchsia_async as fasync, fuchsia_zircon as zx,
        std::ops::Bound,
    };

    #[derive(
        Clone, Eq, PartialEq, PartialOrd, Ord, Debug, serde::Serialize, serde::Deserialize,
    )]
    struct TestKey(i32);

    impl OrdLowerBound for TestKey {
        fn cmp_lower_bound(&self, other: &Self) -> std::cmp::Ordering {
            std::cmp::Ord::cmp(self, other)
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_iteration() {
        // Insert two items and make sure we can iterate back in the correct order.
        let skip_list = SkipListLayer::new(100);
        let items = [Item::new(TestKey(1), 1), Item::new(TestKey(2), 2)];
        skip_list.insert(items[1].clone()).await;
        skip_list.insert(items[0].clone()).await;
        let mut iter = skip_list.get_iterator();
        assert!(iter.get().is_none());
        iter.advance().await.unwrap();
        let ItemRef { key, value } = iter.get().expect("missing item");
        assert_eq!((key, value), (&items[0].key, &items[0].value));
        iter.advance().await.unwrap();
        let ItemRef { key, value } = iter.get().expect("missing item");
        assert_eq!((key, value), (&items[1].key, &items[1].value));
        iter.advance().await.unwrap();
        assert!(iter.get().is_none());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_seek_exact() {
        // Seek for an exact match.
        let skip_list = SkipListLayer::new(100);
        for i in (0..100).rev() {
            skip_list.insert(Item::new(TestKey(i), i)).await;
        }
        let mut iter = skip_list.get_iterator();
        iter.seek(Bound::Included(&TestKey(57))).await.unwrap();
        let ItemRef { key, value } = iter.get().expect("missing item");
        assert_eq!((key, value), (&TestKey(57), &57));

        // And check the next item is correct.
        iter.advance().await.unwrap();
        let ItemRef { key, value } = iter.get().expect("missing item");
        assert_eq!((key, value), (&TestKey(58), &58));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_seek_lower_bound() {
        // Seek for a non-exact match.
        let skip_list = SkipListLayer::new(100);
        for i in (0..100).rev() {
            skip_list.insert(Item::new(TestKey(i * 3), i * 3)).await;
        }
        let mut expected_index = 57 * 3;
        let mut iter = skip_list.get_iterator();
        iter.seek(Bound::Included(&TestKey(expected_index - 1))).await.unwrap();
        let ItemRef { key, value } = iter.get().expect("missing item");
        assert_eq!((key, value), (&TestKey(expected_index), &expected_index));

        // And check the next item is correct.
        expected_index += 3;
        iter.advance().await.unwrap();
        let ItemRef { key, value } = iter.get().expect("missing item");
        assert_eq!((key, value), (&TestKey(expected_index), &expected_index));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_seek_unbounded() {
        let skip_list = SkipListLayer::new(100);
        let items = [Item::new(TestKey(1), 1), Item::new(TestKey(2), 2)];
        skip_list.insert(items[1].clone()).await;
        skip_list.insert(items[0].clone()).await;
        let mut iter = skip_list.get_iterator();
        iter.seek(Bound::Unbounded).await.unwrap();

        let ItemRef { key, value } = iter.get().expect("missing item");
        assert_eq!((key, value), (&items[0].key, &items[0].value));
        iter.advance().await.unwrap();
        let ItemRef { key, value } = iter.get().expect("missing item");
        assert_eq!((key, value), (&items[1].key, &items[1].value));
        iter.advance().await.unwrap();
        assert!(iter.get().is_none());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_replace_or_insert_replaces() {
        let skip_list = SkipListLayer::new(100);
        let items = [Item::new(TestKey(1), 1), Item::new(TestKey(2), 2)];
        skip_list.insert(items[1].clone()).await;
        skip_list.insert(items[0].clone()).await;
        let replacement_value = 3;
        skip_list.replace_or_insert(Item::new(items[1].key.clone(), replacement_value)).await;

        let mut iter = skip_list.get_iterator();
        assert!(iter.get().is_none());
        iter.advance().await.unwrap();
        let ItemRef { key, value } = iter.get().expect("missing item");
        assert_eq!((key, value), (&items[0].key, &items[0].value));
        iter.advance().await.unwrap();
        let ItemRef { key, value } = iter.get().expect("missing item");
        assert_eq!((key, value), (&items[1].key, &replacement_value));
        iter.advance().await.unwrap();
        assert!(iter.get().is_none());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_replace_or_insert_inserts() {
        let skip_list = SkipListLayer::new(100);
        let items = [Item::new(TestKey(1), 1), Item::new(TestKey(2), 2), Item::new(TestKey(3), 3)];
        skip_list.insert(items[2].clone()).await;
        skip_list.insert(items[0].clone()).await;
        skip_list.replace_or_insert(items[1].clone()).await;

        let mut iter = skip_list.get_iterator();
        assert!(iter.get().is_none());
        iter.advance().await.unwrap();
        let ItemRef { key, value } = iter.get().expect("missing item");
        assert_eq!((key, value), (&items[0].key, &items[0].value));
        iter.advance().await.unwrap();
        let ItemRef { key, value } = iter.get().expect("missing item");
        assert_eq!((key, value), (&items[1].key, &items[1].value));
        iter.advance().await.unwrap();
        let ItemRef { key, value } = iter.get().expect("missing item");
        assert_eq!((key, value), (&items[2].key, &items[2].value));
        iter.advance().await.unwrap();
        assert!(iter.get().is_none());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_erase() {
        let skip_list = SkipListLayer::new(100);
        let items = [Item::new(TestKey(1), 1), Item::new(TestKey(2), 2)];
        skip_list.insert(items[1].clone()).await;
        skip_list.insert(items[0].clone()).await;

        skip_list.erase(items[1].as_item_ref()).await;

        {
            let mut iter = skip_list.get_iterator();
            assert!(iter.get().is_none());
            iter.advance().await.unwrap();
            let ItemRef { key, value } = iter.get().expect("missing item");
            assert_eq!((key, value), (&items[0].key, &items[0].value));
            iter.advance().await.unwrap();
            assert!(iter.get().is_none());
        }

        skip_list.erase(items[0].as_item_ref()).await;

        {
            let mut iter = skip_list.get_iterator();
            assert!(iter.get().is_none());
            iter.advance().await.unwrap();
            assert!(iter.get().is_none());
        }
    }

    // This test ends up being flaky on CQ. It is left here as it might be useful in case
    // significant changes are made.
    #[fasync::run_singlethreaded(test)]
    #[ignore]
    async fn test_seek_is_log_n_complexity() {
        // Keep doubling up the number of items until it takes about 500ms to search and then go
        // back and measure something that should, in theory, take about half that time.
        let mut n = 100;
        let mut loops = 0;
        let ticks_per_sec = zx::ticks_per_second();
        let target_ticks = ticks_per_sec / 2; // 500ms
        let time = loop {
            let skip_list = SkipListLayer::new(n as usize);
            for i in 0..n {
                skip_list.insert(Item::new(TestKey(i), i)).await;
            }
            let mut iter = skip_list.get_iterator();
            let start = zx::ticks_get();
            for i in 0..n {
                iter.seek(Bound::Included(&TestKey(i))).await.unwrap();
            }
            let elapsed = zx::ticks_get() - start;
            if elapsed > target_ticks {
                break elapsed;
            }
            n *= 2;
            loops += 1;
        };

        let seek_count = n;
        n >>= loops / 2; // This should, in theory, result in 50% seek time.
        let skip_list = SkipListLayer::new(n as usize);
        for i in 0..n {
            skip_list.insert(Item::new(TestKey(i), i)).await;
        }
        let mut iter = skip_list.get_iterator();
        let start = zx::ticks_get();
        for i in 0..seek_count {
            iter.seek(Bound::Included(&TestKey(i))).await.unwrap();
        }
        let elapsed = zx::ticks_get() - start;

        eprintln!(
            "{} items: {}ms, {} items: {}ms",
            seek_count,
            time * 1000 / ticks_per_sec,
            n,
            elapsed * 1000 / ticks_per_sec
        );

        // Experimental results show that typically we do a bit better than log(n), but here we just
        // check that the time we just measured is above 25% of the time we first measured, the
        // theory suggests it should be around 50%.
        assert!(elapsed * 4 > time);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_large_number_of_items() {
        let item_count = 1000;
        let skip_list = SkipListLayer::new(1000);
        for i in 1..item_count {
            skip_list.insert(Item::new(TestKey(i), 1)).await;
        }
        let mut iter = skip_list.get_iterator();
        iter.seek(Bound::Included(&TestKey(item_count - 10))).await.unwrap();
        for i in item_count - 10..item_count {
            assert_eq!(iter.get().expect("missing item").key, &TestKey(i));
            iter.advance().await.unwrap();
        }
        assert!(iter.get().is_none());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_mutliple_readers_allowed() {
        let skip_list = SkipListLayer::new(100);
        let items = [Item::new(TestKey(1), 1), Item::new(TestKey(2), 2)];
        skip_list.insert(items[1].clone()).await;
        skip_list.insert(items[0].clone()).await;

        // Create the first iterator and check the first item.
        let mut iter = skip_list.get_iterator();
        iter.advance().await.unwrap();
        let ItemRef { key, value } = iter.get().expect("missing item");
        assert_eq!((key, value), (&items[0].key, &items[0].value));

        // Create a second iterator and check the first item.
        let mut iter2 = skip_list.get_iterator();
        iter2.advance().await.unwrap();
        let ItemRef { key, value } = iter2.get().expect("missing item");
        assert_eq!((key, value), (&items[0].key, &items[0].value));

        // Now go back to the first iterator and check the second item.
        iter.advance().await.unwrap();
        let ItemRef { key, value } = iter.get().expect("missing item");
        assert_eq!((key, value), (&items[1].key, &items[1].value));
    }

    fn merge(
        left: &MergeIterator<'_, TestKey, i32>,
        right: &MergeIterator<'_, TestKey, i32>,
    ) -> MergeResult<TestKey, i32> {
        MergeResult::Other {
            emit: None,
            left: Replace(Item::new((*left.key()).clone(), *left.value() + *right.value())),
            right: Discard,
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_merge_into() {
        let skip_list = SkipListLayer::new(100);
        skip_list.insert(Item::new(TestKey(1), 1)).await;

        skip_list.merge_into(Item::new(TestKey(2), 2), &TestKey(1), merge).await;

        let mut iter = skip_list.get_iterator();
        assert!(iter.get().is_none());
        iter.advance().await.unwrap();
        let ItemRef { key, value } = iter.get().expect("missing item");
        assert_eq!((key, value), (&TestKey(1), &3));
        iter.advance().await.unwrap();
        assert!(iter.get().is_none());
    }
}
