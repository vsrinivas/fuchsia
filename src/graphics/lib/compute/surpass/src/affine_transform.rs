// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::hash;

use crate::{CanonBits, Point};

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
#[derive(Copy, Clone, Debug)]
pub struct AffineTransform {
    pub ux: f32,
    pub uy: f32,
    pub vx: f32,
    pub vy: f32,
    pub tx: f32,
    pub ty: f32,
}

impl AffineTransform {
    pub(crate) fn transform(&self, point: Point) -> Point {
        Point {
            x: self.ux.mul_add(point.x, self.vx.mul_add(point.y, self.tx)),
            y: self.uy.mul_add(point.x, self.vy.mul_add(point.y, self.ty)),
        }
    }

    pub(crate) fn is_identity(&self) -> bool {
        *self == Self::default()
    }

    pub fn to_array(&self) -> [f32; 6] {
        [self.ux, self.uy, self.vx, self.vy, self.tx, self.ty]
    }
}

impl Eq for AffineTransform {}

impl PartialEq for AffineTransform {
    fn eq(&self, other: &Self) -> bool {
        self.ux == other.ux
            && self.uy == other.uy
            && self.vx == other.vx
            && self.vy == other.vy
            && self.tx == other.tx
            && self.ty == other.ty
    }
}

impl hash::Hash for AffineTransform {
    fn hash<H: hash::Hasher>(&self, state: &mut H) {
        self.ux.to_canon_bits().hash(state);
        self.uy.to_canon_bits().hash(state);
        self.vx.to_canon_bits().hash(state);
        self.vy.to_canon_bits().hash(state);
        self.tx.to_canon_bits().hash(state);
        self.ty.to_canon_bits().hash(state);
    }
}

impl Default for AffineTransform {
    fn default() -> Self {
        Self { ux: 1.0, vx: 0.0, tx: 0.0, uy: 0.0, vy: 1.0, ty: 0.0 }
    }
}

impl From<[f32; 6]> for AffineTransform {
    fn from(transform: [f32; 6]) -> Self {
        Self {
            ux: transform[0],
            uy: transform[2],
            vx: transform[1],
            vy: transform[3],
            tx: transform[4],
            ty: transform[5],
        }
    }
}
