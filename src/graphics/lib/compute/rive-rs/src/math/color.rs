// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::ops::{Mul, MulAssign};

#[derive(Clone, Debug, PartialEq)]
pub struct Color {
    pub r: f32,
    pub g: f32,
    pub b: f32,
    pub a: f32,
}

impl Color {
    pub fn new(r: f32, g: f32, b: f32, a: f32) -> Self {
        Self { r, g, b, a }
    }

    pub fn lerp(self, other: Self, ratio: f32) -> Self {
        Self {
            r: self.r + (other.r - self.r) * ratio,
            g: self.g + (other.g - self.g) * ratio,
            b: self.b + (other.b - self.b) * ratio,
            a: self.a + (other.a - self.a) * ratio,
        }
    }
}

impl Default for Color {
    fn default() -> Self {
        Self { r: 1.0, g: 1.0, b: 1.0, a: 1.0 }
    }
}

impl Eq for Color {}

impl Mul for Color {
    type Output = Self;

    fn mul(mut self, rhs: Self) -> Self::Output {
        self.r *= rhs.r;
        self.g *= rhs.g;
        self.b *= rhs.b;
        self.a *= rhs.a;

        self
    }
}

impl MulAssign for Color {
    fn mul_assign(&mut self, rhs: Self) {
        self.r *= rhs.r;
        self.g *= rhs.g;
        self.b *= rhs.b;
        self.a *= rhs.a;
    }
}
