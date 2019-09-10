// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::ops::Mul;

use crate::{PIXEL_SHIFT, PIXEL_WIDTH};

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct Point<T> {
    pub x: T,
    pub y: T,
}

impl<T> Point<T> {
    pub fn new(x: T, y: T) -> Self {
        Self { x, y }
    }
}

impl Point<f32> {
    pub fn to_subpixel(self) -> Point<i32> {
        Point {
            x: (self.x * PIXEL_WIDTH as f32).round() as i32,
            y: (self.y * PIXEL_WIDTH as f32).round() as i32,
        }
    }

    pub fn to_subpixel_floor(self) -> Point<i32> {
        Point {
            x: (self.x * PIXEL_WIDTH as f32).floor() as i32,
            y: (self.y * PIXEL_WIDTH as f32).floor() as i32,
        }
    }

    pub fn lerp(t: f32, p0: Self, p1: Self) -> Self {
        Self { x: p0.x * (1.0 - t) + p1.x * t, y: p0.y * (1.0 - t) + p1.y * t }
    }

    pub fn mid(p0: Self, p1: Self) -> Self {
        Self { x: (p0.x + p1.x) / 2.0, y: (p0.y + p1.y) / 2.0 }
    }

    pub fn approx_eq(self, other: Self) -> bool {
        (self.x - other.x).abs() < std::f32::EPSILON && (self.y - other.y).abs() < std::f32::EPSILON
    }

    pub fn from_weighted(point: &[f32; 3]) -> Self {
        Self::new(point[0] / point[2], point[1] / point[2])
    }
}

impl Mul<f32> for Point<f32> {
    type Output = Self;

    fn mul(self, rhs: f32) -> Self::Output {
        Point::new(self.x * rhs, self.y * rhs)
    }
}

impl Point<i32> {
    pub fn manhattan_distance(self, other: Point<i32>) -> i32 {
        (self.x - other.x).abs() + (self.y - other.y).abs()
    }

    pub fn border(self) -> Point<i32> {
        Point { x: (self.x >> PIXEL_SHIFT) * PIXEL_WIDTH, y: (self.y >> PIXEL_SHIFT) * PIXEL_WIDTH }
    }

    pub fn translate(self, translation: Point<i32>) -> Self {
        Self { x: self.x + translation.x, y: self.y + translation.y }
    }
}
