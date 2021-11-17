// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod buffer;
mod composition;
mod layer;
mod path;
mod point;
mod segment;
mod utils;

const PIXEL_WIDTH: i32 = 16;
// Pixel accuracy should be within 0.5 of a sub-pixel.
const PIXEL_ACCURACY: f32 = 0.5 / PIXEL_WIDTH as f32;

pub use buffer::{Buffer, BufferLayerCache, Flusher};
pub use composition::{Composition, LayerId};
pub use layer::{AffineTransform, Layer, Order};
pub use path::Path;
pub use point::Point;
pub use utils::clear_buffer;

pub use surpass::painter::{
    BlendMode, Fill, FillRule, Func, Gradient, GradientBuilder, GradientType, Props, Rect, Style,
};
