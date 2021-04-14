// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::ops::Mul;

use crate::math::Vec;

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Mat {
    pub scale_x: f32,
    pub shear_y: f32,
    pub shear_x: f32,
    pub scale_y: f32,
    pub translate_x: f32,
    pub translate_y: f32,
}

impl Mat {
    pub fn zero() -> Self {
        Self {
            scale_x: 0.0,
            shear_y: 0.0,
            shear_x: 0.0,
            scale_y: 0.0,
            translate_x: 0.0,
            translate_y: 0.0,
        }
    }

    pub fn from_rotation(rad: f32) -> Self {
        let (sin, cos) = rad.sin_cos();
        Self { scale_x: cos, shear_y: sin, shear_x: -sin, scale_y: cos, ..Default::default() }
    }

    pub fn scale(mut self, scale: Vec) -> Self {
        self.scale_x *= scale.x;
        self.shear_y *= scale.x;
        self.shear_x *= scale.y;
        self.scale_y *= scale.y;

        self
    }

    pub fn invert(self) -> Option<Self> {
        let mut det = self.scale_x * self.scale_y - self.shear_y * self.shear_x;

        if det == 0.0 {
            return None;
        }

        det = det.recip();

        Some(Mat {
            scale_x: self.scale_y * det,
            shear_y: -self.shear_y * det,
            shear_x: -self.shear_x * det,
            scale_y: self.scale_x * det,
            translate_x: (self.shear_x * self.translate_y - self.scale_y * self.translate_x) * det,
            translate_y: (self.shear_y * self.translate_x - self.scale_x * self.translate_y) * det,
        })
    }
}

impl Default for Mat {
    fn default() -> Self {
        Self {
            scale_x: 1.0,
            shear_y: 0.0,
            shear_x: 0.0,
            scale_y: 1.0,
            translate_x: 0.0,
            translate_y: 0.0,
        }
    }
}

impl Eq for Mat {}

impl Mul for Mat {
    type Output = Self;

    fn mul(self, rhs: Self) -> Self::Output {
        Self {
            scale_x: self.scale_x * rhs.scale_x + self.shear_x * rhs.shear_y,
            shear_y: self.shear_y * rhs.scale_x + self.scale_y * rhs.shear_y,
            shear_x: self.scale_x * rhs.shear_x + self.shear_x * rhs.scale_y,
            scale_y: self.shear_y * rhs.shear_x + self.scale_y * rhs.scale_y,
            translate_x: self.scale_x * rhs.translate_x
                + self.shear_x * rhs.translate_y
                + self.translate_x,
            translate_y: self.shear_y * rhs.translate_x
                + self.scale_y * rhs.translate_y
                + self.translate_y,
        }
    }
}

impl Mul<Vec> for Mat {
    type Output = Vec;

    fn mul(self, rhs: Vec) -> Self::Output {
        Vec::new(
            rhs.x * self.scale_x + rhs.y * self.shear_x + self.translate_x,
            rhs.x * self.shear_y + rhs.y * self.scale_y + self.translate_y,
        )
    }
}
