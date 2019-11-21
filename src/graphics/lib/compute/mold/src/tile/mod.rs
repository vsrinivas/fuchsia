// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::ops::Range;

use crate::point::Point;

pub(crate) mod contour;
pub(crate) mod map;
mod op;

use map::LayerNode;

// TODO(dragostis): Remove in later CL.
pub use crate::layer::Layer;
pub use map::painter::{ColorBuffer, PixelFormat};
pub use map::Map;
pub use op::TileOp;

pub(crate) const TILE_SIZE: usize = 32;
pub(crate) const TILE_SHIFT: i32 = TILE_SIZE.trailing_zeros() as i32;
pub(crate) const TILE_MASK: i32 = !-(TILE_SIZE as i32);

#[derive(Clone, Debug)]
pub(crate) struct Tile {
    pub tile_i: usize,
    pub tile_j: usize,
    pub layers: Vec<LayerNode>,
    pub needs_render: bool,
}

impl Tile {
    pub fn new(tile_i: usize, tile_j: usize) -> Self {
        Self { tile_i, tile_j, layers: vec![], needs_render: true }
    }

    pub fn new_layer(&mut self, id: u32, translation: Point<i32>) {
        self.layers.push(LayerNode::Layer(id, translation));
        self.needs_render = true;
    }

    pub fn push_segment(&mut self, start_point: Point<i32>, segment_range: Range<usize>) {
        if let Some(LayerNode::Layer(..)) = self.layers.last() {
            self.layers.push(LayerNode::Segments(start_point, segment_range));
            return;
        }

        let old_range = match self.layers.last_mut() {
            Some(LayerNode::Segments(_, range)) => range,
            _ => panic!("Tile::new_layer should be called before Tile::push_segment"),
        };

        if old_range.end == segment_range.start {
            old_range.end = segment_range.end;
        } else {
            self.layers.push(LayerNode::Segments(start_point, segment_range));
        }
    }

    pub fn reset(&mut self) {
        self.layers.clear();
        self.needs_render = true;
    }
}
