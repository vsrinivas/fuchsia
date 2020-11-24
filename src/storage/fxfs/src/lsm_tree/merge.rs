use {
    super::{BoxedLayerIterator, Item, ItemRef, LayerIterator, LayerIteratorMut, OrdLowerBound},
    anyhow::Error,
    std::{
        cmp::Ordering, collections::BinaryHeap, convert::From, ops::Bound, pin::Pin, ptr::NonNull,
    },
};

#[derive(Debug)]
#[allow(dead_code)] // TODO: fixme
pub enum MergeResult<K, V> {
    // Emits the left item unchanged. Keeps the right item.
    EmitLeft,
    // TODO: This probably needs to change to emit allow no remainder.
    EmitPartLeftWithRemainder(Item<K, V>, Item<K, V>),
    // Replaces the right item.
    DiscardPartRightWithRemainder(Item<K, V>),
    // Discards the left item and replaces the right item.
    DiscardLeftAndPartRightWithRemainder(Item<K, V>),
    // Discards both items.
    DiscardBoth,
    // Discards the left item.
    DiscardLeft,
    // Discards the right item.
    DiscardRight,
}

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

// An iterator that keeps track of where we are for each of the
// layers. We push these onto a min-heap.
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

    fn iter_mut(&mut self) -> &mut dyn LayerIterator<K, V> {
        self.iter.as_mut().unwrap().as_mut()
    }

    fn iter(&self) -> &dyn LayerIterator<K, V> {
        self.iter.as_ref().unwrap().as_ref()
    }

    fn set_item_from_iter(&mut self) -> bool {
        self.set_item(unsafe { MergeItem::from(self.iter().get()) })
    }

    fn set_item(&mut self, item: MergeItem<'iter, K, V>) -> bool {
        self.item = item;
        match self.item {
            MergeItem::None => false,
            _ => true,
        }
    }

    fn advance(&mut self) -> Result<bool, Error> {
        self.iter_mut().advance()?;
        Ok(self.set_item_from_iter())
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

    // TODO: best name?
    pub fn set_remainder_item(&mut self, item: Item<K, V>) {
        self.item = MergeItem::Item(item);
    }

    fn is_none(&self) -> bool {
        if let MergeItem::None = self.item {
            true
        } else {
            false
        }
    }
}

struct MergeIteratorRef<'iter, K, V> {
    ptr: NonNull<MergeIterator<'iter, K, V>>,
}

impl<'iter, 'layer, K, V> MergeIteratorRef<'iter, K, V> {
    fn new(m: &mut MergeIterator<'iter, K, V>) -> MergeIteratorRef<'iter, K, V> {
        MergeIteratorRef { ptr: NonNull::from(m) }
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

pub struct Merger<'iter, K, V> {
    // The number of iterators that we've initialised.
    pub iterators_initialized: usize,

    // A buffer containing all the MergeIterator objects.
    iterators: Pin<Vec<MergeIterator<'iter, K, V>>>,

    // A heap with the merge iterators.
    heap: BinaryHeap<MergeIteratorRef<'iter, K, V>>,

    // The current item.
    item: Option<Item<K, V>>,

    // The iterator for the current item.
    iterator: Option<MergeIteratorRef<'iter, K, V>>,

    // The function to be used for merging items.
    merge_fn: MergeFn<K, V>,
}

impl<
        'iter,
        K: std::fmt::Debug + std::marker::Unpin + OrdLowerBound + 'static,
        V: std::fmt::Debug + std::marker::Unpin + 'static,
    > Merger<'iter, K, V>
{
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
            item: None,
            iterator: None,
            merge_fn: merge_fn,
        }
    }

    pub fn merge_into(
        mut mut_iter: Box<dyn LayerIteratorMut<K, V> + '_>,
        item: Item<K, V>,
        lower_bound: &K,
        merge_fn: MergeFn<K, V>,
    ) -> Result<(), Error> {
        mut_iter.seek(Bound::Included(lower_bound)).unwrap();
        let mut mut_merge_iter = MergeIterator {
            iter: None,
            layer: 1,
            item: unsafe { MergeItem::from(mut_iter.get()) },
        };
        let mut item_merge_iter =
            MergeIterator { iter: None, layer: 0, item: MergeItem::Item(item) };
        while !mut_merge_iter.is_none() && !item_merge_iter.is_none() {
            if MergeIteratorRef::new(&mut mut_merge_iter)
                > MergeIteratorRef::new(&mut item_merge_iter)
            {
                let merge_result = merge_fn(&mut_merge_iter, &item_merge_iter);
                // println!("(1) merge for {:?} {:?} -> {:?}",
                //         mut_merge_iter.key(), item_merge_iter.key(), merge_result);
                match merge_result {
                    MergeResult::EmitLeft => {
                        if let MergeItem::Item(item) = mut_merge_iter.item {
                            mut_iter.insert_before(item);
                        }
                        mut_iter.advance()?;
                        mut_merge_iter.item = unsafe { MergeItem::from(mut_iter.get()) };
                    }
                    MergeResult::EmitPartLeftWithRemainder(emit, remainder) => {
                        if let MergeItem::Ref(_) = mut_merge_iter.item {
                            mut_iter.erase();
                        }
                        mut_iter.insert_before(emit);
                        mut_iter.advance()?;
                        mut_merge_iter.item = MergeItem::Item(remainder);
                    }
                    MergeResult::DiscardPartRightWithRemainder(remainder) => {
                        item_merge_iter.item = MergeItem::Item(remainder);
                    }
                    MergeResult::DiscardLeftAndPartRightWithRemainder(remainder) => {
                        if let MergeItem::Ref(_) = mut_merge_iter.item {
                            mut_iter.erase();
                        }
                        mut_merge_iter.item = unsafe { MergeItem::from(mut_iter.get()) };
                        item_merge_iter.item = MergeItem::Item(remainder);
                    }
                    MergeResult::DiscardBoth => {
                        if let MergeItem::Ref(_) = mut_merge_iter.item {
                            mut_iter.erase();
                        }
                        mut_merge_iter.item = MergeItem::None;
                        item_merge_iter.item = MergeItem::None;
                    }
                    MergeResult::DiscardLeft => {
                        if let MergeItem::Ref(_) = mut_merge_iter.item {
                            mut_iter.erase();
                        }
                        mut_merge_iter.item = unsafe { MergeItem::from(mut_iter.get()) };
                    }
                    MergeResult::DiscardRight => item_merge_iter.item = MergeItem::None,
                }
            } else {
                let merge_result = merge_fn(&item_merge_iter, &mut_merge_iter);
                // println!("(2) merge for {:?} {:?} -> {:?}",
                //         item_merge_iter.key(), mut_merge_iter.key(), merge_result);
                match merge_result {
                    MergeResult::EmitLeft => break,
                    MergeResult::EmitPartLeftWithRemainder(emit, remainder) => {
                        mut_iter.insert_before(emit);
                        item_merge_iter.item = MergeItem::Item(remainder);
                    }
                    MergeResult::DiscardPartRightWithRemainder(remainder) => {
                        if let MergeItem::Ref(_) = mut_merge_iter.item {
                            mut_iter.erase();
                        }
                        mut_merge_iter.item = MergeItem::Item(remainder);
                    }
                    MergeResult::DiscardLeftAndPartRightWithRemainder(remainder) => {
                        if let MergeItem::Ref(_) = mut_merge_iter.item {
                            mut_iter.erase();
                        }
                        mut_merge_iter.item = MergeItem::Item(remainder);
                        item_merge_iter.item = MergeItem::None;
                    }
                    MergeResult::DiscardBoth => {
                        if let MergeItem::Ref(_) = mut_merge_iter.item {
                            mut_iter.erase();
                        }
                        mut_merge_iter.item = MergeItem::None;
                        item_merge_iter.item = MergeItem::None;
                    }
                    MergeResult::DiscardLeft => item_merge_iter.item = MergeItem::None,
                    MergeResult::DiscardRight => {
                        if let MergeItem::Ref(_) = mut_merge_iter.item {
                            mut_iter.erase();
                        }
                        mut_merge_iter.item = unsafe { MergeItem::from(mut_iter.get()) };
                    }
                }
            }
        }
        if let MergeItem::Item(item) = mut_merge_iter.item {
            mut_iter.insert_before(item);
        }
        if let MergeItem::Item(item) = item_merge_iter.item {
            mut_iter.insert_before(item);
        }
        Ok(())
    }

    // Merges items from an array of layers using the provided merge function. The merge function is
    // repeatedly provided the lowest and the second lowest element, if one exists. In cases where
    // the two lowest elements compare equal, the element with the lowest layer (i.e. whichever
    // comes first in the layers array) will come first.
    pub fn advance(&mut self) -> Result<(), Error> {
        // Push the iterator for the current item (if we have one) onto the heap.
        if let Some(mut iterator) = self.iterator.take() {
            if iterator.advance()? {
                self.heap.push(iterator);
            }
        }
        if self.iterators_initialized == 0 {
            // Push all the iterators on.
            for iter in &mut self.iterators[self.iterators_initialized..] {
                iter.iter_mut().seek(std::ops::Bound::Unbounded)?;
                if iter.set_item_from_iter() {
                    self.heap.push(MergeIteratorRef::new(iter));
                }
            }
            self.iterators_initialized = self.iterators.len();
        }
        if self.heap.is_empty() {
            self.item = None;
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
                        self.iterator = Some(lowest);
                        return Ok(());
                    }
                    MergeResult::EmitPartLeftWithRemainder(emit, remainder) => {
                        lowest.advance()?;
                        lowest.set_remainder_item(remainder);
                        self.heap.push(lowest);
                        self.heap.push(second_lowest);
                        self.item = Some(emit);
                        return Ok(());
                    }
                    MergeResult::DiscardPartRightWithRemainder(remainder) => {
                        self.heap.push(lowest);
                        second_lowest.set_remainder_item(remainder);
                        self.heap.push(second_lowest);
                    }
                    MergeResult::DiscardLeftAndPartRightWithRemainder(remainder) => {
                        if lowest.advance()? {
                            self.heap.push(lowest);
                        }
                        second_lowest.advance()?;
                        second_lowest.set_remainder_item(remainder);
                        self.heap.push(second_lowest);
                    }
                    MergeResult::DiscardBoth => {
                        if lowest.advance()? {
                            self.heap.push(lowest);
                        }
                        if second_lowest.advance()? {
                            self.heap.push(second_lowest);
                        }
                        if self.heap.is_empty() {
                            self.item = None;
                            return Ok(());
                        }
                    }
                    MergeResult::DiscardLeft => {
                        if lowest.advance()? {
                            self.heap.push(lowest);
                        }
                        self.heap.push(second_lowest);
                    }
                    MergeResult::DiscardRight => {
                        self.heap.push(lowest);
                        if second_lowest.advance()? {
                            self.heap.push(second_lowest);
                        }
                    }
                }
            } else {
                self.iterator = Some(lowest);
                return Ok(());
            }
        }
    }

    // If this is the first call, this returns the item that encompasses key. On subsequent calls
    // key is merely the expected next key, but if the next key < |key|, it will be returned.
    // TODO(csuter): This doesn't feel quite right. It has different semantics for first time call
    // to subsequent calls.
    pub fn advance_to(&mut self, key: &K) -> Result<(), Error> {
        // Push the iterator for the current item (if we have one) onto the heap.
        if let Some(mut iterator) = self.iterator.take() {
            if iterator.advance()? {
                self.heap.push(iterator);
            }
        }
        // If the lower bound of the next item is > key, add more iterators.
        while self.iterators_initialized < self.iterators.len()
            && (self.heap.is_empty()
                || self.heap.peek().unwrap().key().cmp_lower_bound(&key) == Ordering::Greater)
        {
            let index = self.iterators_initialized;
            let iter = &mut self.iterators[index];
            iter.iter_mut().seek(std::ops::Bound::Included(key))?;
            if iter.set_item_from_iter() {
                self.heap.push(MergeIteratorRef::new(iter));
            }
            self.iterators_initialized += 1;
        }
        // Call advance to do the merge.
        self.advance()
    }

    pub fn get(&self) -> Option<ItemRef<'_, K, V>> {
        if let Some(iterator) = &self.iterator {
            Some(iterator.item())
        } else if let Some(item) = self.item.as_ref() {
            Some(ItemRef::from(item))
        } else {
            None
        }
    }
}
