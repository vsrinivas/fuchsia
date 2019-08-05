// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod edge;
mod painter;
mod path;
mod point;
mod raster;
pub mod tile;

// Must be power of 2.
const PIXEL_WIDTH: i32 = 16;
// Basically log2(PIXEL_WIDTH).
const PIXEL_SHIFT: i32 = 4;
const GRID_LIMIT: i32 = 32_768 * PIXEL_WIDTH;

pub use path::Path;
pub use point::Point;
pub use raster::Raster;
