// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(target_os = "fuchsia")]
macro_rules! duration {
    ($category:expr, $name:expr $(, $key:expr => $val:expr)*) => {
        fuchsia_trace::duration!($category, $name $(, $key => $val)*)
    }
}

#[cfg(not(target_os = "fuchsia"))]
macro_rules! duration {
    ($category:expr, $name:expr $(, $key:expr => $val:expr)*) => {};
}

pub mod buffer;
mod composition;
mod utils;

pub use composition::{Composition, Layer};
pub use utils::clear_buffer;

pub use surpass::{
    painter::{
        BlendMode, Channel, Color, Fill, FillRule, Func, Gradient, GradientBuilder, GradientType,
        Image, Props, Rect, Style, Texture, BGR0, BGR1, BGRA, RGB0, RGB1, RGBA,
    },
    AffineTransform, GeomId, GeomPresTransform, GeomPresTransformError, Order, OrderError, Path,
    PathBuilder, Point,
};
