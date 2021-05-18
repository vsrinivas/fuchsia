// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{
    cell::RefCell,
    rc::{Rc, Weak},
};

use dashmap::DashMap;

pub use surpass::painter::Flusher;
use surpass::painter::{BufferLayout, BufferLayoutBuilder};

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
}

#[derive(Clone, Debug)]
pub struct BufferLayerCache {
    pub(crate) id: u8,
    pub(crate) layers_per_tile: Rc<DashMap<(usize, usize), usize>>,
    pub(crate) buffers_with_caches: Weak<RefCell<SmallBitSet>>,
}

impl BufferLayerCache {
    pub fn clear(&self) {
        if let Some(buffers_with_caches) = Weak::upgrade(&self.buffers_with_caches) {
            self.layers_per_tile.clear();
            buffers_with_caches.borrow_mut().clear();
        }
    }
}

impl Drop for BufferLayerCache {
    fn drop(&mut self) {
        if let Some(buffers_with_caches) = Weak::upgrade(&self.buffers_with_caches) {
            buffers_with_caches.borrow_mut().remove(self.id);
        }
    }
}
