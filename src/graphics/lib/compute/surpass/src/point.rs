// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::f32;
use std::ops::{Add, Div, Mul, Sub};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Point<T> {
    pub x: T,
    pub y: T,
}

impl<T> Point<T> {
    pub fn new(x: T, y: T) -> Self {
        Self { x, y }
    }
}

#[allow(clippy::many_single_char_names)]
fn approx_atan2(y: f32, x: f32) -> f32 {
    let x_abs = x.abs();
    let y_abs = y.abs();

    let a = x_abs.min(y_abs) / x_abs.max(y_abs);
    let s = a * a;
    let mut r = s.mul_add(-0.046_496_473, 0.159_314_22).mul_add(s, -0.327_622_77).mul_add(s * a, a);

    if y_abs > x_abs {
        r = f32::consts::FRAC_PI_2 - r;
    }

    if x < 0.0 {
        r = f32::consts::PI - r;
    }

    if y < 0.0 {
        r = -r;
    }

    r
}

// Restrict the Point functions visibility as we do not want to run into the business of delivering a linear algebra package.
impl Point<f32> {
    pub(crate) fn len(self) -> f32 {
        (self.x * self.x + self.y * self.y).sqrt()
    }

    pub(crate) fn angle(self) -> Option<f32> {
        (self.len() >= f32::EPSILON).then(|| approx_atan2(self.y, self.x))
    }
}

impl<T: Add<Output = T>> Add for Point<T> {
    type Output = Self;

    #[inline]
    fn add(self, other: Self) -> Self {
        Self { x: self.x + other.x, y: self.y + other.y }
    }
}

impl<T: Sub<Output = T>> Sub for Point<T> {
    type Output = Self;

    #[inline]
    fn sub(self, other: Self) -> Self {
        Self { x: self.x - other.x, y: self.y - other.y }
    }
}

impl<T: Mul<Output = T> + Copy> Mul<T> for Point<T> {
    type Output = Self;

    #[inline]
    fn mul(self, other: T) -> Self {
        Self { x: self.x * other, y: self.y * other }
    }
}

impl<T: Mul<Output = T> + Add<Output = T> + Copy> Mul<Point<T>> for Point<T> {
    type Output = T;

    #[inline]
    fn mul(self, other: Point<T>) -> T {
        self.x * other.x + self.y * other.y
    }
}

impl<T: Div<Output = T> + Copy> Div<T> for Point<T> {
    type Output = Self;

    #[inline]
    fn div(self, other: T) -> Self {
        Self { x: self.x / other, y: self.y / other }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::f32::consts::{FRAC_PI_2, FRAC_PI_4, PI};

    #[test]
    fn add() {
        assert_eq!(
            Point { x: 32.0, y: 126.0 },
            Point { x: 13.0, y: 27.0 } + Point { x: 19.0, y: 99.0 }
        );
    }

    #[test]
    fn sub() {
        assert_eq!(
            Point { x: -6.0, y: -72.0 },
            Point { x: 13.0, y: 27.0 } - Point { x: 19.0, y: 99.0 }
        );
    }

    #[test]
    fn mul() {
        assert_eq!(Point { x: 3.0, y: 21.0 }, Point { x: 1.0, y: 7.0 } * 3.0);
    }

    #[test]
    fn div() {
        assert_eq!(Point { x: 1.0, y: 7.0 }, Point { x: 3.0, y: 21.0 } / 3.0);
    }

    #[test]
    fn angle() {
        assert_eq!(Some(0.0), Point { x: 1.0, y: 0.0 }.angle());
        assert_eq!(Some(0.0), Point { x: 1e10, y: 0.0 }.angle());
        assert_eq!(Some(PI), Point { x: -1.0, y: 0.0 }.angle());
        assert_eq!(Some(FRAC_PI_2), Point { x: 0.0, y: 1.0 }.angle());
        assert_eq!(Some(-FRAC_PI_2), Point { x: 0.0, y: -1.0 }.angle());
        assert!((FRAC_PI_4 - Point { x: 1.0, y: 1.0 }.angle().unwrap()).abs() < 1e-3);
        assert!((-FRAC_PI_4 - Point { x: 1.0, y: -1.0 }.angle().unwrap()).abs() < 1e-3);
    }
}
