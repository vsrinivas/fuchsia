// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod extend;
mod layer;
pub mod painter;
mod point;
pub mod rasterizer;
mod segment;
mod uninitialized;

pub use layer::Layer;
pub use point::Point;
pub use segment::{Lines, LinesBuilder, Segment};

const PIXEL_WIDTH: usize = 16;
const PIXEL_SHIFT: usize = PIXEL_WIDTH.trailing_zeros() as usize;
const PIXEL_MASK: usize = PIXEL_WIDTH - 1;
const TILE_SIZE: usize = 16;
const TILE_SHIFT: usize = TILE_SIZE.trailing_zeros() as usize;
const TILE_MASK: usize = TILE_SIZE - 1;
