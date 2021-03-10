// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::lsm_tree::types::{
        BoxedLayerIterator, Item, ItemRef, LayerIterator, LayerIteratorMut, OrdLowerBound,
    },
    anyhow::Error,
    fuchsia_syslog::fx_log_debug,
    futures::try_join,
    std::{
        cmp::Ordering, collections::BinaryHeap, convert::From, ops::Bound, pin::Pin, ptr::NonNull,
    },
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

    // Merges the given item into a mutable layer. |lower_bound| should be the first possible key
    // that the item might need to be merged with.
    pub(super) async fn merge_into(
        mut mut_iter: Box<dyn LayerIteratorMut<K, V> + '_>,
        item: Item<K, V>,
        lower_bound: &K,
        merge_fn: MergeFn<K, V>,
    ) -> Result<(), Error> {
        mut_iter.seek(Bound::Included(lower_bound)).await?;
        let mut mut_merge_iter = MergeIterator {
            iter: None,
            layer: 1,
            item: unsafe { MergeItem::from(mut_iter.get()) },
        };
        let mut item_merge_iter =
            MergeIterator { iter: None, layer: 0, item: MergeItem::Item(item) };
        while mut_merge_iter.is_some() && item_merge_iter.is_some() {
            if MergeIteratorRef::new(&mut mut_merge_iter)
                > MergeIteratorRef::new(&mut item_merge_iter)
            {
                // In this branch the mutable layer is left and the item we're merging-in is right.
                let merge_result = merge_fn(&mut_merge_iter, &item_merge_iter);
                fx_log_debug!(
                    "(1) merge for {:?} {:?} -> {:?}",
                    mut_merge_iter.key(),
                    item_merge_iter.key(),
                    merge_result
                );
                match merge_result {
                    MergeResult::EmitLeft => {
                        if let MergeItem::Item(item) = mut_merge_iter.item {
                            mut_iter.insert_before(item);
                        }
                        mut_iter.advance().await?;
                        mut_merge_iter.item = unsafe { MergeItem::from(mut_iter.get()) };
                    }
                    MergeResult::Other { emit, left, right } => {
                        if let Some(emit) = emit {
                            mut_iter.insert_before(emit);
                            mut_iter.advance().await?;
                            if let ItemOp::Keep = left {
                                // This isn't necessary with our skip list implementation because
                                // the existing node shouldn't have been moved, but other layers
                                // might do things differently.
                                if let MergeItem::Ref(_) = mut_merge_iter.item {
                                    mut_merge_iter.item =
                                        unsafe { MergeItem::from(mut_iter.get()) };
                                }
                            }
                        }
                        match left {
                            ItemOp::Keep => {}
                            ItemOp::Discard => {
                                if let MergeItem::Ref(_) = mut_merge_iter.item {
                                    mut_iter.erase();
                                }
                                mut_merge_iter.item = unsafe { MergeItem::from(mut_iter.get()) };
                            }
                            ItemOp::Replace(item) => {
                                if let MergeItem::Ref(_) = mut_merge_iter.item {
                                    mut_iter.erase();
                                }
                                mut_merge_iter.item = MergeItem::Item(item)
                            }
                        }
                        match right {
                            ItemOp::Keep => {}
                            ItemOp::Discard => item_merge_iter.item = MergeItem::None,
                            ItemOp::Replace(item) => item_merge_iter.item = MergeItem::Item(item),
                        }
                    }
                }
            } else {
                // In this branch, the item we're merging-in is left and the mutable layer is right.
                let merge_result = merge_fn(&item_merge_iter, &mut_merge_iter);
                fx_log_debug!(
                    "(2) merge for {:?} {:?} -> {:?}",
                    item_merge_iter.key(),
                    mut_merge_iter.key(),
                    merge_result
                );
                match merge_result {
                    MergeResult::EmitLeft => break, // Item is inserted outside the loop
                    MergeResult::Other { emit, left, right } => {
                        if let Some(emit) = emit {
                            mut_iter.insert_before(emit);
                            mut_iter.advance().await?;
                            if let ItemOp::Keep = right {
                                // This isn't necessary with our skip list implementation because
                                // the existing node shouldn't have been moved, but other layers
                                // might do things differently.
                                if let MergeItem::Ref(_) = mut_merge_iter.item {
                                    mut_merge_iter.item =
                                        unsafe { MergeItem::from(mut_iter.get()) };
                                }
                            }
                        }
                        match left {
                            ItemOp::Keep => {}
                            ItemOp::Discard => item_merge_iter.item = MergeItem::None,
                            ItemOp::Replace(item) => item_merge_iter.item = MergeItem::Item(item),
                        }
                        match right {
                            ItemOp::Keep => {}
                            ItemOp::Discard => {
                                if let MergeItem::Ref(_) = mut_merge_iter.item {
                                    mut_iter.erase();
                                }
                                mut_merge_iter.item = unsafe { MergeItem::from(mut_iter.get()) };
                            }
                            ItemOp::Replace(item) => {
                                if let MergeItem::Ref(_) = mut_merge_iter.item {
                                    mut_iter.erase();
                                }
                                mut_merge_iter.item = MergeItem::Item(item)
                            }
                        }
                    }
                }
            }
        } // while ...
          // The only way we could get here with both items is via the break above, so we know the
          // correct order required here.
        if let MergeItem::Item(item) = mut_merge_iter.item {
            mut_iter.insert_before(item);
        }
        if let MergeItem::Item(item) = item_merge_iter.item {
            mut_iter.insert_before(item);
        }
        Ok(())
    }

    /// Advances the iterator to the next item, but will stop querying iterators when a key is
    /// encountered that is <= |hint|, so it will not necessarily perform a merge with all base
    /// layers.  This function exists to allow more efficient point and range queries; if only the
    /// top layer needs to be consulted, you will not pay the price of seeking in lower layers.  If
    /// new iterators need to be consulted, a search is done using std::cmp::Ord, so the hint should
    /// be set accordingly i.e. if your keys are range based and you want to search for a key that
    /// covers, say, 100..200, the hint should be ?..101 so that you find a key that is, say,
    /// 50..101.  Calling advance after calling advance_with_hint is undefined.
    pub async fn advance_with_hint(&mut self, hint: &K) -> Result<(), Error> {
        // Push the iterator for the current item (if we have one) onto the heap.
        if let Some(mut iterator) = self.item.take_iterator() {
            iterator.advance().await?;
            if iterator.is_some() {
                self.heap.push(iterator);
            }
        }
        // If the lower bound of the next item is > hint, add more iterators.
        while self.iterators_initialized < self.iterators.len()
            && (self.heap.is_empty()
                || self.heap.peek().unwrap().key().cmp_lower_bound(&hint) == Ordering::Greater)
        {
            let index = self.iterators_initialized;
            let iter = &mut self.iterators[index];
            iter.iter_mut().seek(std::ops::Bound::Included(hint)).await?;
            iter.set_item_from_iter();
            if iter.is_some() {
                self.heap.push(MergeIteratorRef::new(iter));
            }
            self.iterators_initialized += 1;
        }
        // Call advance to do the merge.
        self.advance().await
    }

    /// Positions the iterator on the first item with key >= |key|.  Like advance_with_hint, this
    /// only calls on iterators that it needs to, so this won't necessarily perform a full merge.
    /// An Unbounded seek will necessarily involve all iterators.  Calling advance after calling
    /// seek is undefined; use advance_with_hint (or fix advance so that it works if you need it
    /// to).
    pub async fn seek(&mut self, bound: Bound<&K>) -> Result<(), Error> {
        self.heap.clear();
        self.iterators_initialized = 0;
        match bound {
            Bound::Unbounded => self.advance().await,
            Bound::Included(key) => self.advance_with_hint(key).await,
            Bound::Excluded(_) => panic!("Excluded bounds not supported!"),
        }
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
        std::ops::Bound,
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

    #[fasync::run_singlethreaded(test)]
    async fn test_merge_into_emit_left() {
        let skip_list = SkipListLayer::new(100);
        let items =
            [Item::new(TestKey(1..1), 1), Item::new(TestKey(2..2), 2), Item::new(TestKey(3..3), 3)];
        skip_list.insert(items[0].clone()).await;
        skip_list.insert(items[2].clone()).await;
        skip_list
            .merge_into(items[1].clone(), &items[0].key, |_left, _right| MergeResult::EmitLeft)
            .await;

        let mut iter = skip_list.get_iterator();
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
    async fn test_merge_into_emit_last_after_replacing() {
        let skip_list = SkipListLayer::new(100);
        let items = [Item::new(TestKey(1..1), 1), Item::new(TestKey(2..2), 2)];
        skip_list.insert(items[0].clone()).await;

        skip_list
            .merge_into(items[1].clone(), &items[0].key, |left, right| {
                if left.key() == &TestKey(1..1) {
                    assert_eq!(right.key(), &TestKey(2..2));
                    MergeResult::Other {
                        emit: None,
                        left: Replace(Item::new(TestKey(3..3), 3)),
                        right: Keep,
                    }
                } else {
                    assert_eq!(left.key(), &TestKey(2..2));
                    assert_eq!(right.key(), &TestKey(3..3));
                    MergeResult::EmitLeft
                }
            })
            .await;

        let mut iter = skip_list.get_iterator();
        iter.advance().await.unwrap();
        let ItemRef { key, value } = iter.get().expect("missing item");
        assert_eq!((key, value), (&items[1].key, &items[1].value));
        iter.advance().await.unwrap();
        let ItemRef { key, value } = iter.get().expect("missing item");
        assert_eq!((key, value), (&TestKey(3..3), &3));
        iter.advance().await.unwrap();
        assert!(iter.get().is_none());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_merge_into_emit_left_after_replacing() {
        let skip_list = SkipListLayer::new(100);
        let items = [Item::new(TestKey(1..1), 1), Item::new(TestKey(3..3), 3)];
        skip_list.insert(items[0].clone()).await;

        skip_list
            .merge_into(items[1].clone(), &items[0].key, |left, right| {
                if left.key() == &TestKey(1..1) {
                    assert_eq!(right.key(), &TestKey(3..3));
                    MergeResult::Other {
                        emit: None,
                        left: Replace(Item::new(TestKey(2..2), 2)),
                        right: Keep,
                    }
                } else {
                    assert_eq!(left.key(), &TestKey(2..2));
                    assert_eq!(right.key(), &TestKey(3..3));
                    MergeResult::EmitLeft
                }
            })
            .await;

        let mut iter = skip_list.get_iterator();
        iter.advance().await.unwrap();
        let ItemRef { key, value } = iter.get().expect("missing item");
        assert_eq!((key, value), (&TestKey(2..2), &2));
        iter.advance().await.unwrap();
        let ItemRef { key, value } = iter.get().expect("missing item");
        assert_eq!((key, value), (&items[1].key, &items[1].value));
        iter.advance().await.unwrap();
        assert!(iter.get().is_none());
    }

    // This tests emitting in both branches of merge_into, and most of the discard paths.
    #[fasync::run_singlethreaded(test)]
    async fn test_merge_into_emit_other_and_discard() {
        let skip_list = SkipListLayer::new(100);
        let items =
            [Item::new(TestKey(1..1), 1), Item::new(TestKey(3..3), 3), Item::new(TestKey(5..5), 3)];
        skip_list.insert(items[0].clone()).await;
        skip_list.insert(items[2].clone()).await;

        skip_list
            .merge_into(items[1].clone(), &items[0].key, |left, right| {
                if left.key() == &TestKey(1..1) {
                    // This tests the top branch in merge_into.
                    assert_eq!(right.key(), &TestKey(3..3));
                    MergeResult::Other {
                        emit: Some(Item::new(TestKey(2..2), 2)),
                        left: Discard,
                        right: Keep,
                    }
                } else {
                    // This tests the bottom branch in merge_into.
                    assert_eq!(left.key(), &TestKey(3..3));
                    assert_eq!(right.key(), &TestKey(5..5));
                    MergeResult::Other {
                        emit: Some(Item::new(TestKey(4..4), 4)),
                        left: Discard,
                        right: Discard,
                    }
                }
            })
            .await;

        let mut iter = skip_list.get_iterator();
        iter.advance().await.unwrap();
        let ItemRef { key, value } = iter.get().expect("missing item");
        assert_eq!((key, value), (&TestKey(2..2), &2));
        iter.advance().await.unwrap();
        let ItemRef { key, value } = iter.get().expect("missing item");
        assert_eq!((key, value), (&TestKey(4..4), &4));
        iter.advance().await.unwrap();
        assert!(iter.get().is_none());
    }

    // This tests replacing the item and discarding the right item (the one remaining untested
    // discard path) in the top branch in merge_into.
    #[fasync::run_singlethreaded(test)]
    async fn test_merge_into_replace_and_discard() {
        let skip_list = SkipListLayer::new(100);
        let items = [Item::new(TestKey(1..1), 1), Item::new(TestKey(3..3), 3)];
        skip_list.insert(items[0].clone()).await;

        skip_list
            .merge_into(items[1].clone(), &items[0].key, |_left, _right| MergeResult::Other {
                emit: Some(Item::new(TestKey(2..2), 2)),
                left: Replace(Item::new(TestKey(4..4), 4)),
                right: Discard,
            })
            .await;

        let mut iter = skip_list.get_iterator();
        iter.advance().await.unwrap();
        let ItemRef { key, value } = iter.get().expect("missing item");
        assert_eq!((key, value), (&TestKey(2..2), &2));
        iter.advance().await.unwrap();
        let ItemRef { key, value } = iter.get().expect("missing item");
        assert_eq!((key, value), (&TestKey(4..4), &4));
        iter.advance().await.unwrap();
        assert!(iter.get().is_none());
    }

    // This tests replacing the right item in the top branch of merge_into and the left item in the
    // bottom branch of merge_into.
    #[fasync::run_singlethreaded(test)]
    async fn test_merge_into_replace_merge_item() {
        let skip_list = SkipListLayer::new(100);
        let items =
            [Item::new(TestKey(1..1), 1), Item::new(TestKey(3..3), 3), Item::new(TestKey(5..5), 5)];
        skip_list.insert(items[0].clone()).await;
        skip_list.insert(items[2].clone()).await;

        skip_list
            .merge_into(items[1].clone(), &items[0].key, |_left, right| {
                if right.key() == &TestKey(3..3) {
                    MergeResult::Other {
                        emit: None,
                        left: Discard,
                        right: Replace(Item::new(TestKey(2..2), 2)),
                    }
                } else {
                    assert_eq!(right.key(), &TestKey(5..5));
                    MergeResult::Other {
                        emit: None,
                        left: Replace(Item::new(TestKey(4..4), 4)),
                        right: Discard,
                    }
                }
            })
            .await;

        let mut iter = skip_list.get_iterator();
        iter.advance().await.unwrap();
        let ItemRef { key, value } = iter.get().expect("missing item");
        assert_eq!((key, value), (&TestKey(4..4), &4));
        iter.advance().await.unwrap();
        assert!(iter.get().is_none());
    }

    // This tests replacing the right item in the bottom branch of merge_into.
    #[fasync::run_singlethreaded(test)]
    async fn test_merge_into_replace_existing() {
        let skip_list = SkipListLayer::new(100);
        let items = [Item::new(TestKey(1..1), 1), Item::new(TestKey(3..3), 3)];
        skip_list.insert(items[1].clone()).await;

        skip_list
            .merge_into(items[0].clone(), &items[0].key, |_left, right| {
                if right.key() == &TestKey(3..3) {
                    MergeResult::Other {
                        emit: None,
                        left: Keep,
                        right: Replace(Item::new(TestKey(2..2), 2)),
                    }
                } else {
                    MergeResult::EmitLeft
                }
            })
            .await;

        let mut iter = skip_list.get_iterator();
        iter.advance().await.unwrap();
        let ItemRef { key, value } = iter.get().expect("missing item");
        assert_eq!((key, value), (&items[0].key, &items[0].value));
        iter.advance().await.unwrap();
        let ItemRef { key, value } = iter.get().expect("missing item");
        assert_eq!((key, value), (&TestKey(2..2), &2));
        iter.advance().await.unwrap();
        assert!(iter.get().is_none());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_seek_uses_minimum_number_of_iterators() {
        let skip_lists = [SkipListLayer::new(100), SkipListLayer::new(100)];
        let items = [Item::new(TestKey(1..1), 1), Item::new(TestKey(1..1), 2)];
        skip_lists[0].insert(items[0].clone()).await;
        skip_lists[1].insert(items[1].clone()).await;
        let iterators = skip_lists.iter().map(|sl| sl.get_iterator()).collect();
        let mut merger = Merger::new(iterators, |_left, _right| MergeResult::Other {
            emit: None,
            left: Discard,
            right: Keep,
        });
        merger.seek(Bound::Included(&items[0].key)).await.expect("seek failed");

        // Seek should only search in the first skip list, so no merge should take place, and we'll
        // know if it has because we'll see a different value (2 rather than 1).
        let ItemRef { key, value } = merger.get().expect("missing item");
        assert_eq!((key, value), (&items[0].key, &items[0].value));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_advance_with_hint() {
        let skip_lists = [SkipListLayer::new(100), SkipListLayer::new(100)];
        let items =
            [Item::new(TestKey(1..1), 1), Item::new(TestKey(2..2), 2), Item::new(TestKey(3..3), 3)];
        skip_lists[0].insert(items[0].clone()).await;
        skip_lists[0].insert(items[1].clone()).await;
        skip_lists[1].insert(items[2].clone()).await;
        let iterators = skip_lists.iter().map(|sl| sl.get_iterator()).collect();
        let mut merger = Merger::new(iterators, |_left, _right| MergeResult::EmitLeft);
        merger.seek(Bound::Included(&items[0].key)).await.expect("seek failed");
        // This should still find the 2..2 key.
        merger.advance_with_hint(&items[2].key).await.expect("advance_with_hint failed");
        let ItemRef { key, value } = merger.get().expect("missing item");
        assert_eq!((key, value), (&items[1].key, &items[1].value));
        merger.advance_with_hint(&items[2].key).await.expect("advance_with_hint failed");
        let ItemRef { key, value } = merger.get().expect("missing item");
        assert_eq!((key, value), (&items[2].key, &items[2].value));
        merger.advance_with_hint(&TestKey(4..4)).await.expect("advance_with_hint failed");
        assert!(merger.get().is_none());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_advance_with_hint_no_more() {
        let skip_lists = [SkipListLayer::new(100), SkipListLayer::new(100)];
        let items = [Item::new(TestKey(1..1), 1), Item::new(TestKey(2..2), 2)];
        skip_lists[0].insert(items[0].clone()).await;
        skip_lists[1].insert(items[1].clone()).await;
        let iterators = skip_lists.iter().map(|sl| sl.get_iterator()).collect();
        let mut merger = Merger::new(iterators, |_left, _right| MergeResult::EmitLeft);
        merger.seek(Bound::Included(&items[0].key)).await.expect("seek failed");
        // This should skip over the 2..2 key.
        merger.advance_with_hint(&TestKey(100..100)).await.expect("advance_with_hint failed");
        assert!(merger.get().is_none());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_two_seeks() {
        let skip_lists = [SkipListLayer::new(100), SkipListLayer::new(100)];
        let items = [Item::new(TestKey(1..1), 1), Item::new(TestKey(2..2), 2)];
        skip_lists[0].insert(items[0].clone()).await;
        skip_lists[1].insert(items[1].clone()).await;
        let iterators = skip_lists.iter().map(|sl| sl.get_iterator()).collect();
        let mut merger = Merger::new(iterators, |_left, _right| MergeResult::Other {
            emit: None,
            left: Discard,
            right: Keep,
        });
        merger.seek(Bound::Included(&items[1].key)).await.expect("seek failed");

        let ItemRef { key, value } = merger.get().expect("missing item");
        assert_eq!((key, value), (&items[1].key, &items[1].value));

        merger.seek(Bound::Included(&items[0].key)).await.expect("seek failed");

        let ItemRef { key, value } = merger.get().expect("missing item");
        assert_eq!((key, value), (&items[0].key, &items[0].value));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_seek_less_than() {
        let skip_lists = [SkipListLayer::new(100), SkipListLayer::new(100)];
        let items = [Item::new(TestKey(1..1), 1), Item::new(TestKey(2..2), 2)];
        skip_lists[0].insert(items[0].clone()).await;
        skip_lists[1].insert(items[1].clone()).await;
        let iterators = skip_lists.iter().map(|sl| sl.get_iterator()).collect();
        let mut merger = Merger::new(iterators, |_left, _right| MergeResult::Other {
            emit: None,
            left: Discard,
            right: Keep,
        });
        // Search for a key before 1..1.
        merger.seek(Bound::Included(&TestKey(0..0))).await.expect("seek failed");

        // This should find the 2..2 key because of our merge function.
        let ItemRef { key, value } = merger.get().expect("missing item");
        assert_eq!((key, value), (&items[1].key, &items[1].value));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_seek_to_end() {
        let skip_lists = [SkipListLayer::new(100), SkipListLayer::new(100)];
        let items = [Item::new(TestKey(1..1), 1), Item::new(TestKey(2..2), 2)];
        skip_lists[0].insert(items[0].clone()).await;
        skip_lists[1].insert(items[1].clone()).await;
        let iterators = skip_lists.iter().map(|sl| sl.get_iterator()).collect();
        let mut merger = Merger::new(iterators, |_left, _right| MergeResult::Other {
            emit: None,
            left: Discard,
            right: Keep,
        });
        merger.seek(Bound::Included(&TestKey(3..3))).await.expect("seek failed");

        assert!(merger.get().is_none());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_seek_unbounded() {
        let skip_lists = [SkipListLayer::new(100), SkipListLayer::new(100)];
        let items = [Item::new(TestKey(1..1), 1), Item::new(TestKey(2..2), 2)];
        skip_lists[0].insert(items[0].clone()).await;
        skip_lists[1].insert(items[1].clone()).await;
        let iterators = skip_lists.iter().map(|sl| sl.get_iterator()).collect();
        let mut merger = Merger::new(iterators, |_left, _right| MergeResult::EmitLeft);
        merger.seek(Bound::Included(&items[1].key)).await.expect("seek failed");
        merger.seek(Bound::Unbounded).await.expect("seek failed");

        let ItemRef { key, value } = merger.get().expect("missing item");
        assert_eq!((key, value), (&items[0].key, &items[0].value));
    }
}
