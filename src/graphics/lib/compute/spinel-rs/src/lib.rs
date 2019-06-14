// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Rust wrapper around Spinel.

mod context;
mod path;
mod path_builder;
mod spinel_sys;

pub use context::Context;
pub use path::Path;
pub use path_builder::PathBuilder;

/// A point in 2D display space. The origin sits at the bottom-left side of the screen, with the X
/// axis pointing towards the right and the Y axis towards the top.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Point {
    /// Horizontal displacement.
    pub x: f32,
    /// Vertical displacement.
    pub y: f32,
}

impl Point {
    pub(crate) fn is_finite(self) -> bool {
        self.x.is_finite() && self.y.is_finite()
    }

    pub(crate) fn approx_eq(self, other: Self) -> bool {
        (self.x - other.x).abs() < std::f32::EPSILON && (self.y - other.y).abs() < std::f32::EPSILON
    }
}
