// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod aabb;
mod bezier;
mod color;
mod mat;
mod vec;

pub use aabb::Aabb;
pub use bezier::Bezier;
pub use color::Color;
pub use mat::Mat;
pub use vec::Vec;

pub const CIRCLE_CONSTANT: f32 = 0.552_284_8;

pub fn lerp(a: f32, b: f32, ratio: f32) -> f32 {
    a + (b - a) * ratio
}

pub fn arc_constant(angle: f32) -> f32 {
    4.0 / 3.0 * (angle / 4.0).tan()
}
