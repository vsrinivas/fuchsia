// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(missing_docs)]

//! Efficient CPU-backed tiled vector-content renderer with a Spinel-like API.

mod clip;
mod layer;
mod path;
mod point;
mod raster;
mod segment;
pub mod tile;

// Must be power of 2.
const PIXEL_WIDTH: i32 = 16;
const PIXEL_MASK: i32 = PIXEL_WIDTH - 1;
// Basically `PIXEL_WIDTH.log2()`.
const PIXEL_SHIFT: i32 = PIXEL_WIDTH.trailing_zeros() as i32;
// `f32` provides integer precision for +/- 16,777,216 values. Halving the value by 2 double the
// precision. In order for `f32` to accommodate `PIXEL_WIDTH` sub-pixels, the value needs to be
// divided by `PIXEL_WIDTH`. Anything beyond these values will not be able to take advantage of the
// full accuracy of the sub-pixel representation and does not make sense.
//
// `GRID_LIMIT * PIXEL_WIDTH` must not exceed `i32::max_value()`.
const GRID_LIMIT: i32 = 16_777_216 / PIXEL_WIDTH;

pub use clip::Clip;
pub use layer::Layer;
pub use path::Path;
pub use point::Point;
pub use raster::{Raster, RasterInner};
pub use tile::map::painter::buffer::{ColorBuffer, PixelFormat};
