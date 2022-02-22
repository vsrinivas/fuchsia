// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{
    cell::RefCell,
    rc::{Rc, Weak},
};

pub use surpass::layout;

use crate::layer::SmallBitSet;

use layout::{Flusher, Layout};

#[derive(Debug)]
pub struct Buffer<'b, 'l, L: Layout> {
    pub(crate) buffer: &'b mut [u8],
    pub(crate) layout: &'l mut L,
    pub(crate) layer_cache: Option<BufferLayerCache>,
    pub(crate) flusher: Option<Box<dyn Flusher>>,
}

#[derive(Debug)]
pub struct BufferBuilder<'b, 'l, L: Layout> {
    buffer: Buffer<'b, 'l, L>,
}

impl<'b, 'l, L: Layout> BufferBuilder<'b, 'l, L> {
    #[inline]
    pub fn new(buffer: &'b mut [u8], layout: &'l mut L) -> Self {
        Self { buffer: Buffer { buffer, layout, layer_cache: None, flusher: None } }
    }

    #[inline]
    pub fn layer_cache(mut self, layer_cache: BufferLayerCache) -> Self {
        self.buffer.layer_cache = Some(layer_cache);
        self
    }

    #[inline]
    pub fn flusher(mut self, flusher: Box<dyn Flusher>) -> Self {
        self.buffer.flusher = Some(flusher);
        self
    }

    #[inline]
    pub fn build(self) -> Buffer<'b, 'l, L> {
        self.buffer
    }
}

#[derive(Clone, Debug)]
pub struct BufferLayerCache {
    pub(crate) id: u8,
    pub(crate) layers_per_tile: Rc<RefCell<Vec<Option<u32>>>>,
    pub(crate) buffers_with_caches: Weak<RefCell<SmallBitSet>>,
}

impl BufferLayerCache {
    #[inline]
    pub fn clear(&self) {
        if Weak::upgrade(&self.buffers_with_caches).is_some() {
            self.layers_per_tile.borrow_mut().fill(None);
        }
    }
}

impl Drop for BufferLayerCache {
    fn drop(&mut self) {
        if let Some(buffers_with_caches) = Weak::upgrade(&self.buffers_with_caches) {
            // We don't want to remove the current ID if there are more than 1 copies of the
            // current object, so we cheat and use the available `Rc` to make sure that's the case.
            if Rc::strong_count(&self.layers_per_tile) == 1 {
                buffers_with_caches.borrow_mut().remove(self.id);
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use std::mem;

    fn new_cache(bit_set: &Rc<RefCell<SmallBitSet>>) -> BufferLayerCache {
        bit_set
            .borrow_mut()
            .first_empty_slot()
            .map(|id| BufferLayerCache {
                id,
                layers_per_tile: Default::default(),
                buffers_with_caches: Rc::downgrade(bit_set),
            })
            .unwrap()
    }

    #[test]
    fn clone_and_drop() {
        let bit_set = Rc::new(RefCell::new(SmallBitSet::default()));

        let cache0 = new_cache(&bit_set);
        let cache1 = new_cache(&bit_set);
        let cache2 = new_cache(&bit_set);

        assert!(bit_set.borrow().contains(&0));
        assert!(bit_set.borrow().contains(&1));
        assert!(bit_set.borrow().contains(&2));

        mem::drop(cache0.clone());
        mem::drop(cache1.clone());
        mem::drop(cache2.clone());

        assert!(bit_set.borrow().contains(&0));
        assert!(bit_set.borrow().contains(&1));
        assert!(bit_set.borrow().contains(&2));

        mem::drop(cache1);

        assert!(bit_set.borrow().contains(&0));
        assert!(!bit_set.borrow().contains(&1));
        assert!(bit_set.borrow().contains(&2));

        let cache1 = new_cache(&bit_set);

        assert!(bit_set.borrow().contains(&0));
        assert!(bit_set.borrow().contains(&1));
        assert!(bit_set.borrow().contains(&2));

        mem::drop(cache0);
        mem::drop(cache1);
        mem::drop(cache2);

        assert!(!bit_set.borrow().contains(&0));
        assert!(!bit_set.borrow().contains(&1));
        assert!(!bit_set.borrow().contains(&2));
    }
}
