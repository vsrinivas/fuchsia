// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// 2D transformation that preserves parallel lines.
///
/// Such a transformation can combine translation, scale, flip, rotate and shears.
/// It is represented by a 3 by 3 matrix where the last row is [0, 0, 1].
///
/// ```text
/// [ x' ]   [ u.x v.x t.x ] [ x ]
/// [ y' ] = [ u.y v.y t.y ] [ y ]
/// [ 1  ]   [   0   0   1 ] [ 1 ]
/// ```
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct AffineTransform {
    ux: f32,
    uy: f32,
    vx: f32,
    vy: f32,
    tx: f32,
    ty: f32,
}

impl Default for AffineTransform {
    fn default() -> Self {
        Self { ux: 1.0, vx: 0.0, tx: 0.0, uy: 0.0, vy: 1.0, ty: 0.0 }
    }
}
