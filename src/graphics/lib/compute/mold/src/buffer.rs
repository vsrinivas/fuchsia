// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{
    cell::RefCell,
    rc::{Rc, Weak},
};

pub use surpass::painter::Flusher;
use surpass::{
    painter::{BufferLayout, BufferLayoutBuilder},
    TILE_SIZE,
};

use crate::layer::SmallBitSet;

#[derive(Default)]
pub struct Buffer<'b> {
    pub buffer: &'b mut [[u8; 4]],
    pub width: usize,
    pub width_stride: Option<usize>,
    pub layer_cache: Option<BufferLayerCache>,
    pub flusher: Option<Box<dyn Flusher>>,
}

impl Buffer<'_> {
    pub(crate) fn generate_layout(&mut self) -> BufferLayout {
        let mut builder = BufferLayoutBuilder::new(self.width);
        if let Some(width_stride) = self.width_stride {
            builder = builder.set_width_stride(width_stride);
        }
        builder.build(self.buffer)
    }

    /// Returns the length required for the allocation of a per-tile buffer.
    pub(crate) fn tiles_len(&self) -> usize {
        let buffer_width = self.width_stride.unwrap_or_else(|| self.width);
        let tiles_width = (self.width + (TILE_SIZE - 1)) / TILE_SIZE;
        let buffer_height = self.buffer.len() / buffer_width;
        let tiles_height = (buffer_height + (TILE_SIZE - 1)) / TILE_SIZE;

        tiles_width * tiles_height
    }
}

#[derive(Clone, Debug)]
pub struct BufferLayerCache {
    pub(crate) id: u8,
    pub(crate) layers_per_tile: Rc<RefCell<Vec<Option<u16>>>>,
    pub(crate) buffers_with_caches: Weak<RefCell<SmallBitSet>>,
}

impl BufferLayerCache {
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

    #[test]
    fn tiles_len() {
        let buffer = Buffer {
            buffer: &mut [[0; 4]; (TILE_SIZE * 5) * (TILE_SIZE * 8)],
            width: TILE_SIZE * 4,
            width_stride: Some(TILE_SIZE * 5),
            ..Default::default()
        };

        // 4 tiles horizontally * 8 tiles vertically = 32 tiles in total
        assert_eq!(buffer.tiles_len(), 32);
    }
}
