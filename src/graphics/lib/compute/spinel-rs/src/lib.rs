// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Rust wrapper around Spinel.

mod context;
mod path;
mod path_builder;
mod raster;
mod raster_builder;
mod spinel_sys;

pub use context::Context;
pub use path::Path;
pub use path_builder::PathBuilder;
pub use raster::Raster;
pub use raster_builder::RasterBuilder;

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

/// Spinel-specific 2D transformation.
#[derive(Clone, Debug)]
pub struct Transform {
    /// X-axis scaling factor.
    pub scale_x: f32,
    /// X-axis shear factor.
    pub shear_x: f32,
    /// Y-axis shear factor.
    pub shear_y: f32,
    /// Y-axis scaling factor.
    pub scale_y: f32,
    /// X-axis translation.
    pub translate_x: f32,
    /// Y-axis translation.
    pub translate_y: f32,
    /// Perspective factor *w0*.
    pub w0: f32,
    /// Perspective factor *w1*.
    pub w1: f32,
}

impl Transform {
    /// Creates an identity transform.
    pub fn identity() -> Self {
        Self {
            scale_x: 1.0,
            shear_x: 0.0,
            shear_y: 0.0,
            scale_y: 1.0,
            translate_x: 0.0,
            translate_y: 0.0,
            w0: 0.0,
            w1: 0.0,
        }
    }

    /// Creates a transform from a 3x3 matrix placed in [column-major] fashion in the `[f32; 9]`
    /// array.
    ///
    /// [column-major]: https://en.wikipedia.org/wiki/Row-_and_column-major_order
    pub fn from_matrix(matrix: [f32; 9]) -> Self {
        Self {
            scale_x: matrix[0] / matrix[8],
            shear_x: matrix[3] / matrix[8],
            shear_y: matrix[1] / matrix[8],
            scale_y: matrix[4] / matrix[8],
            translate_x: matrix[6] / matrix[8],
            translate_y: matrix[7] / matrix[8],
            w0: matrix[2] / matrix[8],
            w1: matrix[5] / matrix[8],
        }
    }

    pub(crate) fn as_array(&self) -> [f32; 8] {
        [
            self.scale_x,
            self.shear_x,
            self.shear_y,
            self.scale_y,
            self.translate_x,
            self.translate_y,
            self.w0,
            self.w1,
        ]
    }

    pub(crate) fn is_finite(&self) -> bool {
        self.scale_x.is_finite()
            && self.shear_x.is_finite()
            && self.shear_y.is_finite()
            && self.scale_y.is_finite()
            && self.translate_x.is_finite()
            && self.translate_y.is_finite()
            && self.w0.is_finite()
            && self.w1.is_finite()
    }
}

impl Default for Transform {
    fn default() -> Self {
        Self::identity()
    }
}

/// Rectangular clip.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Clip {
    /// Bottom-left point of the rectangle.
    pub bottom_left: Point,
    /// Top-right point of the rectangle.
    pub top_right: Point,
}

impl Clip {
    pub(crate) fn is_finite(&self) -> bool {
        self.bottom_left.is_finite() && self.top_right.is_finite()
    }
}

impl Default for Clip {
    fn default() -> Self {
        Self {
            bottom_left: Point { x: std::f32::MIN, y: std::f32::MIN },
            top_right: Point { x: std::f32::MAX, y: std::f32::MAX },
        }
    }
}
