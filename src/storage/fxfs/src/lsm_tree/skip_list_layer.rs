// There are a great many optimisations that could be considered to improve performance and maybe
// memory usage.

use {
    crate::lsm_tree::{
        merge::{MergeFn, Merger},
        BoxedLayerIterator, Item, ItemRef, Key, Layer, LayerIterator, LayerIteratorMut,
        MutableLayer, Value,
    },
    anyhow::Error,
    std::{
        cmp::min,
        ops::Bound,
        sync::{Arc, RwLock, RwLockReadGuard, RwLockWriteGuard},
    },
};

#[derive(Debug)]
struct PointerList<K: Key, V: Value>(Box<[*mut SkipListNode<K, V>]>);

impl<K: Key, V: Value> PointerList<K, V> {
    fn new(count: usize) -> PointerList<K, V> {
        PointerList(vec![std::ptr::null_mut(); count].into_boxed_slice())
    }

    fn len(&self) -> usize {
        self.0.len()
    }

    fn get<'a, 'b>(&'a self, index: usize) -> Option<&'b mut SkipListNode<K, V>> {
        unsafe { self.0[index].as_mut() }
    }

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

    pub fn erase(&self, item: ItemRef<'_, K, V>)
    where
        K: std::cmp::Eq,
    {
        let mut iter = SkipListLayerIterMut::new(self);
        iter.seek(Bound::Included(&item.key)).unwrap();
        if let Some(ItemRef { key, .. }) = iter.get() {
            if key == item.key {
                println!("skiplist erasing: {:?}", item);
                iter.erase();
            }
        }
    }
    /*
        fn find_mut(self: &mut SkipListLayer<K, V>, key: &K) -> SkipListLayerIterMut<K, V> {
            let mut index = self.pointers.len() - 1;
            let mut p = self.pointers.get(index);
            let mut iter = SkipListLayerIterMut::new(self);
            loop {
                // We could optimise for == if we could be bothered (via a different method maybe).
                if let Some(node) = p {
                    if &node.item.key < key {
                        iter.prev_pointers[index] = &mut node.pointers;
                        p = node.pointers.get(index);
                        continue;
                    }
                }
                if index == 0 {
                    break;
                }
                index -= 1;
                iter.prev_pointers[index] = iter.prev_pointers[index + 1];
                unsafe {
                    p = (*iter.prev_pointers[index]).get(index);
                }
            }
            iter
        }
    */
}

impl<K: Key, V: Value> Drop for SkipListLayer<K, V> {
    fn drop(&mut self) {
        let mut next = self.pointers.write().unwrap().get(0);
        while let Some(node) = next {
            unsafe {
                next = Box::from_raw(node).pointers.get(0);
            }
        }
    }
}

impl<'layer, K: Key, V: Value> Layer<K, V> for SkipListLayer<K, V> {
    fn get_iterator(&self) -> BoxedLayerIterator<'_, K, V> {
        Box::new(SkipListLayerIter::new(self))
    }
}

impl<'layer, K: Key, V: Value> MutableLayer<K, V> for SkipListLayer<K, V> {
    fn as_layer(self: Arc<Self>) -> Arc<dyn Layer<K, V>> {
        self
    }

    fn dump(&self) {
        let pointers = self.pointers.write().unwrap();
        let mut node = pointers.get(0);
        while let Some(n) = node {
            node = n.pointers.get(0);
        }
    }

    fn insert(&self, item: Item<K, V>) {
        let mut iter = SkipListLayerIterMut::new(self);
        iter.seek(Bound::Included(&item.key)).unwrap();
        iter.insert_before(item);
    }

    fn replace_range(&self, item: Item<K, V>, lower_bound: &K, merge_fn: MergeFn<K, V>) {
        Merger::merge_into(Box::new(SkipListLayerIterMut::new(self)), item, lower_bound, merge_fn)
            .unwrap();
    }

    fn replace_or_insert(&self, item: Item<K, V>) {
        let mut iter = SkipListLayerIterMut::new(self);
        iter.seek(Bound::Included(&item.key)).unwrap();
        if let Some(found_item) = iter.get_mut() {
            if found_item.key == item.key {
                *found_item = item;
                return;
            }
        }
        iter.insert_before(item);
    }
}

// -- SkipListLayerIter --

struct SkipListLayerIter<'iter, K: Key, V: Value> {
    pointers: RwLockReadGuard<'iter, PointerList<K, V>>,
    prev_pointers: Option<Box<[*const PointerList<K, V>]>>,
}

impl<'iter, K: Key, V: Value> SkipListLayerIter<'iter, K, V> {
    fn new(skip_list: &'iter SkipListLayer<K, V>) -> SkipListLayerIter<'iter, K, V> {
        SkipListLayerIter { pointers: skip_list.pointers.read().unwrap(), prev_pointers: None }
    }
}

unsafe impl<K: Key, V: Value> Send for SkipListLayerIter<'_, K, V> {}

impl<K: Key, V: Value> LayerIterator<K, V> for SkipListLayerIter<'_, K, V> {
    fn seek(&mut self, bound: std::ops::Bound<&K>) -> Result<(), Error> {
        let len = self.pointers.len();
        self.prev_pointers =
            Some(vec![&*self.pointers as *const _; self.pointers.len()].into_boxed_slice());
        match bound {
            Bound::Unbounded => {}
            Bound::Included(key) => {
                // TODO: We don't actually need previous pointers here.
                let mut index = len - 1;
                let mut p = self.pointers.get(index);
                let pointers = self.prev_pointers.as_mut().unwrap();
                loop {
                    // We could optimise for == if we could be bothered (via a different method
                    // maybe).
                    if let Some(node) = p {
                        if &node.item.key < key {
                            pointers[index] = &node.pointers;
                            p = node.pointers.get(index);
                            continue;
                        }
                    }
                    if index == 0 {
                        break;
                    }
                    index -= 1;
                    pointers[index] = pointers[index + 1];
                    unsafe {
                        p = (*pointers[index]).get(index);
                    }
                }
            }
            Bound::Excluded(_) => panic!("Excluded bounds not supported"),
        }
        Ok(())
    }

    fn advance(&mut self) -> Result<(), Error> {
        match self.prev_pointers {
            None => self.seek(Bound::Unbounded),
            Some(ref mut pointers) => {
                if let Some(next) = unsafe { pointers[0].as_ref().unwrap().get(0) } {
                    for i in 0..next.pointers.len() {
                        pointers[i] = &next.pointers;
                    }
                }
                Ok(())
            }
        }
    }

    fn get(&self) -> Option<ItemRef<'_, K, V>> {
        match self.prev_pointers {
            None => None,
            Some(ref pointers) => unsafe {
                pointers[0].as_ref().unwrap().get(0).map(|node| node.item.as_item_ref())
            },
        }
    }

    fn discard_or_advance(&mut self) -> Result<(), Error> {
        self.advance()
    }
}

// -- SkipListLayerIterMut --

struct SkipListLayerIterMut<'iter, K: Key, V: Value> {
    pointers: RwLockWriteGuard<'iter, PointerList<K, V>>,
    prev_pointers: Option<Box<[*mut PointerList<K, V>]>>,
}

impl<K: Key, V: Value> SkipListLayerIterMut<'_, K, V> {
    fn new(skip_list: &SkipListLayer<K, V>) -> SkipListLayerIterMut<'_, K, V> {
        SkipListLayerIterMut { pointers: skip_list.pointers.write().unwrap(), prev_pointers: None }
    }

    fn get_mut(&self) -> Option<&mut Item<K, V>> {
        match self.prev_pointers {
            None => None,
            Some(ref pointers) => unsafe { (*pointers[0]).get(0).map(|node| &mut node.item) },
        }
    }
}

unsafe impl<K: Key, V: Value> Send for SkipListLayerIterMut<'_, K, V> {}

impl<K: Key, V: Value> LayerIterator<K, V> for SkipListLayerIterMut<'_, K, V> {
    fn seek(&mut self, bound: std::ops::Bound<&K>) -> Result<(), Error> {
        let len = self.pointers.len();
        self.prev_pointers = Some(vec![&mut *self.pointers as *mut _; len].into_boxed_slice());
        match bound {
            Bound::Unbounded => {}
            Bound::Included(key) => {
                // TODO: We don't actually need previous pointers here.
                let mut index = len - 1;
                let mut p = self.pointers.get(index);
                let pointers = self.prev_pointers.as_mut().unwrap();
                loop {
                    // We could optimise for == if we could be bothered (via a different method
                    // maybe).
                    if let Some(node) = p {
                        if &(node.item.key) < key {
                            pointers[index] = &mut node.pointers;
                            p = node.pointers.get(index);
                            continue;
                        }
                    }
                    if index == 0 {
                        break;
                    }
                    index -= 1;
                    pointers[index] = pointers[index + 1];
                    unsafe {
                        p = (*pointers[index]).get(index);
                    }
                }
            }
            Bound::Excluded(_) => panic!("Excluded bounds not supported"),
        }
        Ok(())
    }

    fn advance(&mut self) -> Result<(), Error> {
        let pointers = self.prev_pointers.as_mut().unwrap();
        if let Some(next) = unsafe { (*pointers[0]).get(0) } {
            for i in 0..next.pointers.len() {
                pointers[i] = &mut next.pointers;
            }
        }
        Ok(())
    }

    fn get(&self) -> Option<ItemRef<'_, K, V>> {
        unsafe {
            (*self.prev_pointers.as_ref().unwrap()[0]).get(0).map(|node| node.item.as_item_ref())
        }
    }

    fn discard_or_advance(&mut self) -> Result<(), Error> {
        self.erase();
        Ok(())
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
        let pointer_count = max_pointers
            - min(
                (rng.gen_range(0, 2u32.pow(max_pointers as u32) - 1) as f32).log2() as usize,
                max_pointers - 1,
            );
        let mut node =
            Box::leak(Box::new(SkipListNode { item, pointers: PointerList::new(pointer_count) }));
        for i in 0..pointer_count {
            let pointers = unsafe { &mut *self.prev_pointers.as_mut().unwrap()[i] };
            node.pointers.set(i, pointers.get(i));
            pointers.set(i, Some(&mut node));
        }
    }

    fn erase(&mut self) {
        unsafe {
            let pointers = self.prev_pointers.as_ref().unwrap();
            if let Some(next) = (*pointers[0]).get(0) {
                for i in 0..next.pointers.len() {
                    (*pointers[i]).set(i, next.pointers.get(i));
                }
                Box::from_raw(next);
            }
        }
    }
}
