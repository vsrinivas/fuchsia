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

pub use buffer::{Buffer, Flusher};
pub use composition::Composition;
pub use layer::{Layer, LayerId};
pub use path::Path;
pub use point::Point;
pub use utils::clear_buffer;

pub use surpass::painter::{BlendMode, Fill, FillRule, Rect, Style};
