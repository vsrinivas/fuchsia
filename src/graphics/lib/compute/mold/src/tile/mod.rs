// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Tile-aware operators and rendering.
//!
//! mold uses [`TILE_SIZE`]<sup>2</sup>-wide pixel squares to render its content. Each tile contains
//! a set of [operations] that are executed on the tile's five registers:
//!
//! | Register    | Purpose                                                                    |
//! |-------------|----------------------------------------------------------------------------|
//! | `CoverWip`  | Stores raster's immediate pixel coverage                                   |
//! | `CoverAcc`  | Used to accumulate multiple coverages                                      |
//! | `CoverMask` | Masks `CoverWip` with its coverage                                         |
//! | `ColorWip`  | Stores immediate color information                                         |
//! | `ColorAcc`  | Stores accumulated color information that will be the result of the render |
//!
//! The operations are executed only on the effected tiles, these being the raster's covered tiles
//! in the case of [`Map::print`], or all tiles in the case of [`Map::global`].
//!
//! [`TILE_SIZE`]: tile::TILE_SIZE
//! [operations]: tile::Op
//! [`Map::print`]: tile::Map::print
//! [`Map::global`]: tile::Map::global

use std::ops::Range;

use crate::point::Point;

pub(crate) mod contour;
pub(crate) mod map;
mod op;

use map::LayerNode;

pub use map::Map;
pub use op::Op;

/// Size of a [tile], in pixels.
///
/// [tile]: crate::tile
pub const TILE_SIZE: usize = 32;
pub(crate) const TILE_SHIFT: i32 = TILE_SIZE.trailing_zeros() as i32;
pub(crate) const TILE_MASK: i32 = !-(TILE_SIZE as i32);

#[derive(Clone, Debug)]
pub(crate) struct Tile {
    pub i: usize,
    pub j: usize,
    pub layers: Vec<LayerNode>,
    pub needs_render: bool,
    pub is_enabled: bool,
}

impl Tile {
    pub fn new(i: usize, j: usize) -> Self {
        Self { i, j, layers: vec![], needs_render: true, is_enabled: true }
    }

    pub fn new_layer(&mut self, id: u32, translation: Point<i32>) {
        if !self.is_enabled {
            return;
        }

        self.layers.push(LayerNode::Layer(id, translation));
        self.needs_render = true;
    }

    pub fn push_segment(&mut self, start_point: Point<i32>, segment_range: Range<usize>) {
        if !self.is_enabled {
            return;
        }

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
