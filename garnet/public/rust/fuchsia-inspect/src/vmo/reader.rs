// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use crate::vmo::block::Block;
use crate::vmo::utils;

/// Iterates over a byte array containing Inspect API blocks and returns the
/// blocks in order.
pub struct BlockIterator<'h> {
    /// Current offset at which the iterator is reading.
    offset: usize,

    /// The bytes being read.
    container: &'h [u8],
}

impl<'h> BlockIterator<'h> {
    pub fn new(container: &'h [u8]) -> Self {
        BlockIterator { offset: 0, container: container }
    }
}

impl<'h> Iterator for BlockIterator<'h> {
    type Item = Block<&'h [u8]>;

    fn next(&mut self) -> Option<Block<&'h [u8]>> {
        let index = utils::index_for_offset(self.offset);
        let offset = utils::offset_for_index(index);
        if offset >= self.container.len() {
            return None;
        }
        let block = Block::new(self.container.clone(), index);
        if self.container.len() - offset < utils::order_to_size(block.order()) {
            return None;
        }
        self.offset += utils::order_to_size(block.order());
        Some(block)
    }
}
