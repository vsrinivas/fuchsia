// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::ops::{Add, AddAssign, Div, Mul, Neg, Sub, SubAssign};

use crate::math::Mat;

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Vec {
    pub x: f32,
    pub y: f32,
}

impl Vec {
    pub const fn new(x: f32, y: f32) -> Self {
        Self { x, y }
    }

    pub fn transform_dir(self, mat: &Mat) -> Self {
        Self {
            x: self.x * mat.scale_x + self.y * mat.shear_y,
            y: self.x * mat.shear_x + self.y * mat.scale_y,
        }
    }

    pub fn length(self) -> f32 {
        (self.x * self.x + self.y * self.y).sqrt()
    }

    pub fn distance(self, other: Self) -> f32 {
        (self - other).length()
    }

    pub fn normalize(self) -> Self {
        let length = self.length();

        if length > 0.0 {
            self * length.recip()
        } else {
            self
        }
    }

    pub fn dot(self, other: Self) -> f32 {
        self.x * other.x + self.y * other.y
    }

    pub fn lerp(self, other: Self, ratio: f32) -> Self {
        self + (other - self) * ratio
    }
}

impl Eq for Vec {}

impl Add for Vec {
    type Output = Self;

    fn add(self, rhs: Self) -> Self::Output {
        Vec::new(self.x + rhs.x, self.y + rhs.y)
    }
}

impl AddAssign for Vec {
    fn add_assign(&mut self, rhs: Self) {
        *self = *self + rhs;
    }
}

impl Sub for Vec {
    type Output = Self;

    fn sub(self, rhs: Self) -> Self::Output {
        Vec::new(self.x - rhs.x, self.y - rhs.y)
    }
}

impl SubAssign for Vec {
    fn sub_assign(&mut self, rhs: Self) {
        *self = *self - rhs;
    }
}

impl Mul for Vec {
    type Output = Self;

    fn mul(self, rhs: Self) -> Self::Output {
        Self::new(self.x * rhs.x, self.y * rhs.y)
    }
}

impl Mul<f32> for Vec {
    type Output = Self;

    fn mul(self, rhs: f32) -> Self::Output {
        Self::new(self.x * rhs, self.y * rhs)
    }
}

impl Div for Vec {
    type Output = Self;

    fn div(self, rhs: Self) -> Self::Output {
        Self::new(self.x / rhs.x, self.y / rhs.y)
    }
}

impl Neg for Vec {
    type Output = Self;

    fn neg(self) -> Self::Output {
        Self::new(-self.x, -self.y)
    }
}
