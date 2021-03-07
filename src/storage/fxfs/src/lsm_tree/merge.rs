// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::lsm_tree::types::{BoxedLayerIterator, Item, ItemRef, LayerIterator, OrdLowerBound},
    anyhow::Error,
    futures::try_join,
    std::{cmp::Ordering, collections::BinaryHeap, convert::From, pin::Pin, ptr::NonNull},
};

#[derive(Debug)]
pub enum ItemOp<K, V> {
    /// Keeps the item to be presented to the merger subsequently with a new merge pair.
    Keep,

    /// Discards the item and moves on to the next item in the respective layer.
    Discard,

    /// Replaces the item with something new which will be presented to the merger subsequently with
    /// a new pair.
    Replace(Item<K, V>),
}

#[derive(Debug)]
pub enum MergeResult<K, V> {
    /// Emits the left item unchanged. Keeps the right item. This is the common case. Once an item
    /// has been emitted, it will never be seen again by the merge function.
    EmitLeft,

    /// All other merge results are covered by the following. Take care when replacing items
    /// that you replace the correct item. The merger will never merge two items together from
    /// the same layer. Consider the following scenario:
    ///
    ///        +-----------+              +-----------+
    /// 0:     |    A      |              |    C      |
    ///        +-----------+--------------+-----------+
    /// 1:                 |      B       |
    ///                    +--------------+
    ///
    /// Let's say that all three items can be merged together. The merge function will first be
    /// presented with items A and B, at which point it has the option of replacing the left item
    /// (i.e. A, in layer 0) or the right item (i.e. B in layer 1). However, if you replace the left
    /// item, the merge function will not then be given the opportunity to merge it with C, so the
    /// correct thing to do in this case is to replace the right item B in layer 1, and discard the
    /// left item. A rule you can use is that you should avoid replacing an item with another item
    /// whose upper bound exceeds that of the item you are replacing.
    ///
    /// There are some combinations that might lead to infinite loops (e.g. None, Keep, Keep) and
    /// should obviously be avoided.
    Other { emit: Option<Item<K, V>>, left: ItemOp<K, V>, right: ItemOp<K, V> },
}

/// Users must provide a merge function which will take pairs of items, left and right, and return a
/// merge result. The left item's key will either be less than the right item's key, or if they are
/// the same, then the left item will be in a lower layer index (lower layer indexes indicate more
/// recent entries). The last remaining item is always emitted.
pub type MergeFn<K, V> =
    fn(&MergeIterator<'_, K, V>, &MergeIterator<'_, K, V>) -> MergeResult<K, V>;

pub enum MergeItem<'item, K, V> {
    None,
    Ref(ItemRef<'item, K, V>),
    Item(Item<K, V>),
}

impl<K, V> MergeItem<'_, K, V> {
    // This function is used to take an item reference in an iterator and store it in MergeIterator.
    // It's safe because whenever the iterator is advanced or mutated in any way, we update the
    // item. It's necessary because MergeIterator stores both the item and the iterator i.e. it's a
    // self-referential struct.
    unsafe fn from(item: Option<ItemRef<'_, K, V>>) -> Self {
        match item {
            None => MergeItem::None,
            Some(ItemRef { key, value }) => {
                let key_ptr = NonNull::from(key);
                let value_ptr = NonNull::from(value);
                MergeItem::Ref(ItemRef { key: &*key_ptr.as_ptr(), value: &*value_ptr.as_ptr() })
            }
        }
    }
}

// An iterator that keeps track of where we are for each of the layers. We push these onto a
// min-heap.
pub struct MergeIterator<'iter, K, V> {
    // The underlying iterator, which can be const or mutable.
    iter: Option<Box<dyn LayerIterator<K, V> + 'iter>>,

    // The index of the layer this is for.
    pub layer: u16,

    // The item we are currently pointing at.
    item: MergeItem<'iter, K, V>,
}

impl<'iter, K, V> MergeIterator<'iter, K, V> {
    fn new(layer: u16, iter: BoxedLayerIterator<'iter, K, V>) -> Self {
        MergeIterator { iter: Some(iter), layer, item: MergeItem::None }
    }

    pub fn item(&self) -> ItemRef<'_, K, V> {
        match &self.item {
            MergeItem::None => panic!("No item!"),
            MergeItem::Ref(item) => *item,
            MergeItem::Item(ref item) => ItemRef::from(item),
        }
    }

    pub fn key(&self) -> &K {
        return self.item().key;
    }

    pub fn value(&self) -> &V {
        return self.item().value;
    }

    fn iter(&self) -> &dyn LayerIterator<K, V> {
        self.iter.as_ref().unwrap().as_ref()
    }

    fn iter_mut(&mut self) -> &mut dyn LayerIterator<K, V> {
        self.iter.as_mut().unwrap().as_mut()
    }

    fn set_item(&mut self, item: MergeItem<'iter, K, V>) {
        self.item = item;
    }

    fn set_item_from_iter(&mut self) {
        self.set_item(unsafe { MergeItem::from(self.iter().get()) })
    }

    async fn advance(&mut self) -> Result<(), Error> {
        self.iter_mut().advance().await?;
        self.set_item_from_iter();
        Ok(())
    }

    fn replace(&mut self, item: Item<K, V>) {
        self.item = MergeItem::Item(item);
    }

    fn is_some(&self) -> bool {
        match self.item {
            MergeItem::None => false,
            _ => true,
        }
    }
}

struct MergeIteratorRef<'iter, K, V> {
    ptr: NonNull<MergeIterator<'iter, K, V>>,
}

unsafe impl<K, V> Send for MergeIteratorRef<'_, K, V> {}

impl<'iter, 'layer, K, V> MergeIteratorRef<'iter, K, V> {
    fn new(m: &mut MergeIterator<'iter, K, V>) -> MergeIteratorRef<'iter, K, V> {
        MergeIteratorRef { ptr: NonNull::from(m) }
    }

    // This function exists so that we can advance multiple iterators concurrently using, say,
    // try_join!.
    async fn maybe_discard(&mut self, op: &ItemOp<K, V>) -> Result<(), Error> {
        if let ItemOp::Discard = op {
            self.advance().await?;
        }
        Ok(())
    }
}

impl<'iter, 'layer, K, V> std::ops::Deref for MergeIteratorRef<'iter, K, V> {
    type Target = MergeIterator<'iter, K, V>;

    fn deref(&self) -> &Self::Target {
        unsafe { &*self.ptr.as_ptr() }
    }
}

impl<'iter, 'layer, K, V> std::convert::AsMut<MergeIterator<'iter, K, V>>
    for MergeIteratorRef<'iter, K, V>
{
    fn as_mut(&mut self) -> &mut MergeIterator<'iter, K, V> {
        return &mut *self;
    }
}

impl<K, V> std::ops::DerefMut for MergeIteratorRef<'_, K, V> {
    fn deref_mut(&mut self) -> &mut Self::Target {
        unsafe { &mut *self.ptr.as_ptr() }
    }
}

// -- Ord and friends --
impl<'a, 'b, K: OrdLowerBound, V> Ord for MergeIteratorRef<'_, K, V> {
    fn cmp(&self, other: &Self) -> Ordering {
        // Reverse ordering because we want min-heap not max-heap.
        other.key().cmp_lower_bound(self.key()).then(other.layer.cmp(&self.layer))
    }
}
impl<K: OrdLowerBound, V> PartialOrd for MergeIteratorRef<'_, K, V> {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        return Some(self.cmp(other));
    }
}
impl<K: OrdLowerBound, V> PartialEq for MergeIteratorRef<'_, K, V> {
    fn eq(&self, other: &Self) -> bool {
        return self.cmp(other) == Ordering::Equal;
    }
}
impl<K: OrdLowerBound, V> Eq for MergeIteratorRef<'_, K, V> {}

// As we merge items, the current item can be an item that has been replaced (and later emitted) by
// the merge function, or an item referenced by an iterator, or nothing.
enum CurrentItem<'iter, K, V> {
    None,
    Item(Item<K, V>),
    Iterator(MergeIteratorRef<'iter, K, V>),
}

impl<'iter, K, V> CurrentItem<'iter, K, V> {
    // Takes the iterator if one is present and replaces the current item with None; otherwise,
    // leaves the current item untouched.
    fn take_iterator(&mut self) -> Option<MergeIteratorRef<'iter, K, V>> {
        if let CurrentItem::Iterator(_) = self {
            let mut result = CurrentItem::None;
            std::mem::swap(self, &mut result);
            if let CurrentItem::Iterator(iter) = result {
                Some(iter)
            } else {
                unreachable!();
            }
        } else {
            None
        }
    }
}

impl<'a, K, V> From<&'a CurrentItem<'_, K, V>> for Option<ItemRef<'a, K, V>> {
    fn from(iter: &'a CurrentItem<'_, K, V>) -> Option<ItemRef<'a, K, V>> {
        match iter {
            CurrentItem::None => None,
            CurrentItem::Iterator(iterator) => Some(iterator.item()),
            CurrentItem::Item(item) => Some(item.into()),
        }
    }
}

/// Merger is the main entry point to merging.
pub struct Merger<'iter, K, V> {
    // The number of iterators that we've initialised.
    pub iterators_initialized: usize,

    // A buffer containing all the MergeIterator objects.
    iterators: Pin<Vec<MergeIterator<'iter, K, V>>>,

    // A heap with the merge iterators.
    heap: BinaryHeap<MergeIteratorRef<'iter, K, V>>,

    // The current item.
    item: CurrentItem<'iter, K, V>,

    // The function to be used for merging items.
    merge_fn: MergeFn<K, V>,
}

unsafe impl<K, V> Send for Merger<'_, K, V> {}
unsafe impl<K, V> Sync for Merger<'_, K, V> {}

impl<
        'iter,
        K: std::fmt::Debug + std::marker::Unpin + OrdLowerBound + 'static,
        V: std::fmt::Debug + std::marker::Unpin + 'static,
    > Merger<'iter, K, V>
{
    /// Returns a Merger prepared for merging.
    pub(super) fn new(
        layer_iterators: Box<[BoxedLayerIterator<'iter, K, V>]>,
        merge_fn: MergeFn<K, V>,
    ) -> Merger<'iter, K, V> {
        let iterators = layer_iterators
            .into_vec()
            .drain(..)
            .enumerate()
            .map(|(index, iter)| MergeIterator::new(index as u16, iter))
            .collect();
        Merger {
            iterators_initialized: 0,
            iterators: Pin::new(iterators),
            heap: BinaryHeap::new(),
            item: CurrentItem::None,
            merge_fn: merge_fn,
        }
    }

    /// Merges items from an array of layers using the provided merge function. The merge function
    /// is repeatedly provided the lowest and the second lowest element, if one exists. In cases
    /// where the two lowest elements compare equal, the element with the lowest layer
    /// (i.e. whichever comes first in the layers array) will come first.
    pub async fn advance(&mut self) -> Result<(), Error> {
        // Push the iterator for the current item (if we have one) onto the heap.
        if let Some(mut iterator) = self.item.take_iterator() {
            iterator.advance().await?;
            if iterator.is_some() {
                self.heap.push(iterator);
            }
        }
        if self.iterators_initialized == 0 {
            // Push all the iterators on.
            for iter in &mut self.iterators[self.iterators_initialized..] {
                iter.iter_mut().seek(std::ops::Bound::Unbounded).await?;
                iter.set_item_from_iter();
                if iter.is_some() {
                    self.heap.push(MergeIteratorRef::new(iter));
                }
            }
            self.iterators_initialized = self.iterators.len();
        }
        if self.heap.is_empty() {
            self.item = CurrentItem::None;
            return Ok(());
        }
        loop {
            let mut lowest = self.heap.pop().unwrap();
            let maybe_second_lowest = self.heap.pop();
            if let Some(mut second_lowest) = maybe_second_lowest {
                let result = (self.merge_fn)(lowest.as_mut(), second_lowest.as_mut());
                match result {
                    MergeResult::EmitLeft => {
                        self.heap.push(second_lowest);
                        self.item = CurrentItem::Iterator(lowest);
                        return Ok(());
                    }
                    MergeResult::Other { emit, left, right } => {
                        try_join!(
                            lowest.maybe_discard(&left),
                            second_lowest.maybe_discard(&right)
                        )?;
                        self.update_item(lowest, left);
                        self.update_item(second_lowest, right);
                        if let Some(emit) = emit {
                            self.item = CurrentItem::Item(emit);
                            return Ok(());
                        }
                    }
                }
            } else {
                self.item = CurrentItem::Iterator(lowest);
                return Ok(());
            }
        }
    }

    // Updates the merge iterator depending on |op|. If discarding, the iterator should have already
    // been advanced.
    fn update_item(&mut self, mut item: MergeIteratorRef<'iter, K, V>, op: ItemOp<K, V>) {
        match op {
            ItemOp::Keep => self.heap.push(item),
            ItemOp::Discard => {
                // The iterator should have already been advanced.
                if item.is_some() {
                    self.heap.push(item);
                }
            }
            ItemOp::Replace(replacement) => {
                item.replace(replacement);
                self.heap.push(item);
            }
        }
    }

    pub fn get(&self) -> Option<ItemRef<'_, K, V>> {
        (&self.item).into()
    }
}

#[cfg(test)]
mod tests {
    use {
        super::{
            ItemOp::{Discard, Keep, Replace},
            MergeResult, Merger,
        },
        crate::lsm_tree::{
            skip_list_layer::SkipListLayer,
            types::{Item, ItemRef, Layer, MutableLayer, OrdLowerBound},
        },
        fuchsia_async as fasync,
        rand::Rng,
    };

    #[derive(Clone, Eq, PartialEq, Debug, serde::Serialize, serde::Deserialize)]
    struct TestKey(std::ops::Range<u64>);

    impl Ord for TestKey {
        fn cmp(&self, other: &TestKey) -> std::cmp::Ordering {
            self.0.end.cmp(&other.0.end)
        }
    }

    impl PartialOrd for TestKey {
        fn partial_cmp(&self, other: &TestKey) -> Option<std::cmp::Ordering> {
            Some(self.cmp(other))
        }
    }

    impl OrdLowerBound for TestKey {
        fn cmp_lower_bound(&self, other: &Self) -> std::cmp::Ordering {
            self.0.start.cmp(&other.0.start)
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_emit_left() {
        let skip_lists = [SkipListLayer::new(100), SkipListLayer::new(100)];
        let items = [Item::new(TestKey(1..1), 1), Item::new(TestKey(2..2), 2)];
        skip_lists[0].insert(items[1].clone()).await;
        skip_lists[1].insert(items[0].clone()).await;
        let iterators = skip_lists.iter().map(|sl| sl.get_iterator()).collect();
        let mut merger = Merger::new(iterators, |_left, _right| MergeResult::EmitLeft);

        merger.advance().await.unwrap();
        let ItemRef { key, value } = merger.get().expect("missing item");
        assert_eq!((key, value), (&items[0].key, &items[0].value));
        merger.advance().await.unwrap();
        let ItemRef { key, value } = merger.get().expect("missing item");
        assert_eq!((key, value), (&items[1].key, &items[1].value));
        merger.advance().await.unwrap();
        assert!(merger.get().is_none());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_other_emit() {
        let skip_lists = [SkipListLayer::new(100), SkipListLayer::new(100)];
        let items = [Item::new(TestKey(1..1), 1), Item::new(TestKey(2..2), 2)];
        skip_lists[0].insert(items[1].clone()).await;
        skip_lists[1].insert(items[0].clone()).await;
        let iterators = skip_lists.iter().map(|sl| sl.get_iterator()).collect();
        let mut merger = Merger::new(iterators, |_left, _right| MergeResult::Other {
            emit: Some(Item::new(TestKey(3..3), 3)),
            left: Discard,
            right: Discard,
        });

        merger.advance().await.unwrap();
        let ItemRef { key, value } = merger.get().expect("missing item");
        assert_eq!((key, value), (&TestKey(3..3), &3));
        merger.advance().await.unwrap();
        assert!(merger.get().is_none());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_replace_left() {
        let skip_lists = [SkipListLayer::new(100), SkipListLayer::new(100)];
        let items = [Item::new(TestKey(1..1), 1), Item::new(TestKey(2..2), 2)];
        skip_lists[0].insert(items[1].clone()).await;
        skip_lists[1].insert(items[0].clone()).await;
        let iterators = skip_lists.iter().map(|sl| sl.get_iterator()).collect();
        let mut merger = Merger::new(iterators, |_left, _right| MergeResult::Other {
            emit: None,
            left: Replace(Item::new(TestKey(3..3), 3)),
            right: Discard,
        });

        // The merger should replace the left item and then after discarding the right item, it
        // should emit the replacement.
        merger.advance().await.unwrap();
        let ItemRef { key, value } = merger.get().expect("missing item");
        assert_eq!((key, value), (&TestKey(3..3), &3));
        merger.advance().await.unwrap();
        assert!(merger.get().is_none());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_replace_right() {
        let skip_lists = [SkipListLayer::new(100), SkipListLayer::new(100)];
        let items = [Item::new(TestKey(1..1), 1), Item::new(TestKey(2..2), 2)];
        skip_lists[0].insert(items[1].clone()).await;
        skip_lists[1].insert(items[0].clone()).await;
        let iterators = skip_lists.iter().map(|sl| sl.get_iterator()).collect();
        let mut merger = Merger::new(iterators, |_left, _right| MergeResult::Other {
            emit: None,
            left: Discard,
            right: Replace(Item::new(TestKey(3..3), 3)),
        });

        // The merger should replace the right item and then after discarding the left item, it
        // should emit the replacement.
        merger.advance().await.unwrap();
        let ItemRef { key, value } = merger.get().expect("missing item");
        assert_eq!((key, value), (&TestKey(3..3), &3));
        merger.advance().await.unwrap();
        assert!(merger.get().is_none());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_left_less_than_right() {
        let skip_lists = [SkipListLayer::new(100), SkipListLayer::new(100)];
        let items = [Item::new(TestKey(1..1), 1), Item::new(TestKey(2..2), 2)];
        skip_lists[0].insert(items[1].clone()).await;
        skip_lists[1].insert(items[0].clone()).await;
        let iterators = skip_lists.iter().map(|sl| sl.get_iterator()).collect();
        let mut merger = Merger::new(iterators, |left, right| {
            assert_eq!((left.key(), left.value()), (&TestKey(1..1), &1));
            assert_eq!((right.key(), right.value()), (&TestKey(2..2), &2));
            MergeResult::EmitLeft
        });

        merger.advance().await.unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_left_equals_right() {
        let skip_lists = [SkipListLayer::new(100), SkipListLayer::new(100)];
        let item = Item::new(TestKey(1..1), 1);
        skip_lists[0].insert(item.clone()).await;
        skip_lists[1].insert(item.clone()).await;
        let iterators = skip_lists.iter().map(|sl| sl.get_iterator()).collect();
        let mut merger = Merger::new(iterators, |left, right| {
            assert_eq!((left.key(), left.value()), (&TestKey(1..1), &1));
            assert_eq!((left.key(), left.value()), (&TestKey(1..1), &1));
            assert_eq!(left.layer, 0);
            assert_eq!(right.layer, 1);
            MergeResult::EmitLeft
        });

        merger.advance().await.unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_keep() {
        let skip_lists = [SkipListLayer::new(100), SkipListLayer::new(100)];
        let items = [Item::new(TestKey(1..1), 1), Item::new(TestKey(2..2), 2)];
        skip_lists[0].insert(items[1].clone()).await;
        skip_lists[1].insert(items[0].clone()).await;
        let iterators = skip_lists.iter().map(|sl| sl.get_iterator()).collect();
        let mut merger = Merger::new(iterators, |left, right| {
            if left.key() == &TestKey(1..1) {
                MergeResult::Other {
                    emit: None,
                    left: Replace(Item::new(TestKey(3..3), 3)),
                    right: Keep,
                }
            } else {
                assert_eq!(left.key(), &TestKey(2..2));
                assert_eq!(right.key(), &TestKey(3..3));
                MergeResult::Other { emit: None, left: Discard, right: Keep }
            }
        });

        // The merger should first replace left and then it should call the merger again with 2 & 3
        // and end up just keeping 3.
        merger.advance().await.unwrap();
        let ItemRef { key, value } = merger.get().expect("missing item");
        assert_eq!((key, value), (&TestKey(3..3), &3));
        merger.advance().await.unwrap();
        assert!(merger.get().is_none());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_merge_10_layers() {
        let skip_lists: Vec<_> = (0..10).map(|_| SkipListLayer::new(100)).collect();
        let mut rng = rand::thread_rng();
        for i in 0..100 {
            skip_lists[rng.gen_range(0, 10) as usize].insert(Item::new(TestKey(i..i), i)).await;
        }
        let iterators = skip_lists.iter().map(|sl| sl.get_iterator()).collect();
        let mut merger = Merger::new(iterators, |_left, _right| MergeResult::EmitLeft);

        for i in 0..100 {
            merger.advance().await.unwrap();
            let ItemRef { key, value } = merger.get().expect("missing item");
            assert_eq!((key, value), (&TestKey(i..i), &i));
        }
        merger.advance().await.unwrap();
        assert!(merger.get().is_none());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_merge_uses_cmp_lower_bound() {
        let skip_lists = [SkipListLayer::new(100), SkipListLayer::new(100)];
        let items = [Item::new(TestKey(1..10), 1), Item::new(TestKey(2..3), 2)];
        skip_lists[0].insert(items[1].clone()).await;
        skip_lists[1].insert(items[0].clone()).await;
        let iterators = skip_lists.iter().map(|sl| sl.get_iterator()).collect();
        let mut merger = Merger::new(iterators, |_left, _right| MergeResult::EmitLeft);

        merger.advance().await.unwrap();
        let ItemRef { key, value } = merger.get().expect("missing item");
        assert_eq!((key, value), (&items[0].key, &items[0].value));
        merger.advance().await.unwrap();
        let ItemRef { key, value } = merger.get().expect("missing item");
        assert_eq!((key, value), (&items[1].key, &items[1].value));
        merger.advance().await.unwrap();
        assert!(merger.get().is_none());
    }
}
