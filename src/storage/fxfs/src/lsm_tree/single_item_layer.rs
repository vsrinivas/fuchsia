use crate::lsm_tree::{BoxedLayerIterator, ItemRef, Layer, LayerIterator};
use failure::Error;
use std::ops::Bound;

pub struct SingleItemLayer<'item, K, V> {
    item: ItemRef<'item, K, V>,
}

impl<K, V> SingleItemLayer<'_, K, V> {
    pub fn new(item: ItemRef<K, V>) -> SingleItemLayer<K, V> {
        SingleItemLayer { item }
    }
}

enum SingleItemLayerIteratorState {
    Start,
    AtItem,
    End,
}

struct SingleItemLayerIterator<'item, K, V> {
    item: ItemRef<'item, K, V>,
    pos: i32,
}

impl<K: PartialOrd, V> Layer<K, V> for SingleItemLayer<'_, K, V> {
    fn get_iterator(&self) -> BoxedLayerIterator<K, V> {
        Box::new(SingleItemLayerIterator { item: self.item, pos: 0 })
    }
}

impl<K: PartialOrd, V> LayerIterator<K, V> for SingleItemLayerIterator<'_, K, V> {
    fn seek(&mut self, bound: std::ops::Bound<&K>) -> Result<(), Error> {
        match bound {
            Bound::Unbounded => self.pos = 1,
            Bound::Included(key) => {
                if key <= self.item.key {
                    self.pos = 1;
                } else {
                    self.pos = 2;
                }
            }
            Bound::Excluded(key) => {
                panic!("Not supported!");
            }
        }
        Ok(())
    }

    fn advance(&mut self) -> Result<(), Error> {
        self.pos += 1;
        Ok(())
    }

    fn get(&self) -> Option<ItemRef<K, V>> {
        match self.pos {
            1 => Some(self.item),
            _ => None,
        }
    }
}
