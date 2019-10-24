// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::ops::{Add, Mul};

use crate::{PIXEL_MASK, PIXEL_WIDTH};

/// A point in 2D space.
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct Point<T> {
    /// Point's value on the Ox axis.
    pub x: T,
    /// Point's value on the Oy axis.
    pub y: T,
}

impl<T> Point<T> {
    /// Creates a new point.
    ///
    /// # Examples
    /// ```
    /// # use crate::mold::Point;
    /// let point = Point::new(1.0, 2.0);
    /// ```
    pub fn new(x: T, y: T) -> Self {
        Self { x, y }
    }
}

impl Point<f32> {
    pub(crate) fn to_subpixel(self) -> Point<i32> {
        Point {
            x: (self.x * PIXEL_WIDTH as f32 + 0.5).floor() as i32,
            y: (self.y * PIXEL_WIDTH as f32 + 0.5).floor() as i32,
        }
    }

    pub(crate) fn lerp(t: f32, p0: Self, p1: Self) -> Self {
        p0 * (1.0 - t) + p1 * t
    }

    pub(crate) fn approx_eq(self, other: Self) -> bool {
        (self.x - other.x).abs() < std::f32::EPSILON && (self.y - other.y).abs() < std::f32::EPSILON
    }

    pub(crate) fn from_weighted(point: &[f32; 3]) -> Self {
        Self::new(point[0], point[1]) * point[2].recip()
    }
}

impl Point<i32> {
    pub(crate) fn manhattan_distance(self, other: Point<i32>) -> i32 {
        (self.x - other.x).abs() + (self.y - other.y).abs()
    }

    pub(crate) fn border(self) -> Point<i32> {
        Point { x: self.x & !PIXEL_MASK, y: self.y & !PIXEL_MASK }
    }

    pub(crate) fn translate(self, translation: Point<i32>) -> Self {
        self + translation
    }
}

impl<T> Add for Point<T>
where
    T: Add<Output = T> + Copy,
{
    type Output = Self;

    fn add(self, rhs: Self) -> Self::Output {
        Point::new(self.x + rhs.x, self.y + rhs.y)
    }
}

impl<T> Mul<T> for Point<T>
where
    T: Mul<Output = T> + Copy,
{
    type Output = Self;

    fn mul(self, rhs: T) -> Self::Output {
        Point::new(self.x * rhs, self.y * rhs)
    }
}
