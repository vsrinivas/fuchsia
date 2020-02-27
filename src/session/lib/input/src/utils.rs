// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::ops::{Add, AddAssign, Div, DivAssign, Mul, MulAssign, Sub, SubAssign};

/// Represents a generic 2D position.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Position {
    /// The x component of the position, in pixels.
    pub x: f32,

    /// The y component of the position, in pixels.
    pub y: f32,
}

impl Add for Position {
    type Output = Self;

    #[inline]
    fn add(self, other: Self) -> Self {
        Self { x: self.x + other.x, y: self.y + other.y }
    }
}

impl AddAssign for Position {
    #[inline]
    fn add_assign(&mut self, other: Self) {
        *self = Self { x: self.x + other.x, y: self.y + other.y };
    }
}

impl Sub for Position {
    type Output = Self;

    #[inline]
    fn sub(self, other: Self) -> Self {
        Self { x: self.x - other.x, y: self.y - other.y }
    }
}

impl SubAssign for Position {
    #[inline]
    fn sub_assign(&mut self, other: Self) {
        *self = Self { x: self.x - other.x, y: self.y - other.y };
    }
}

impl Div for Position {
    type Output = Self;

    #[inline]
    fn div(self, other: Self) -> Self {
        Self { x: self.x / other.x, y: self.y / other.y }
    }
}

impl DivAssign for Position {
    #[inline]
    fn div_assign(&mut self, other: Self) {
        *self = Self { x: self.x / other.x, y: self.y / other.y };
    }
}

impl Mul for Position {
    type Output = Self;

    #[inline]
    fn mul(self, other: Self) -> Self {
        Self { x: self.x * other.x, y: self.y * other.y }
    }
}

impl MulAssign for Position {
    #[inline]
    fn mul_assign(&mut self, other: Self) {
        *self = Self { x: self.x * other.x, y: self.y * other.y };
    }
}

impl Mul<Size> for Position {
    type Output = Self;

    #[inline]
    fn mul(self, other: Size) -> Position {
        Self { x: self.x * other.width, y: self.y * other.height }
    }
}

macro_rules! scale_position_impl {
    ($($t:ty)*) => ($(
        impl Div<$t> for Position {
            type Output = Position;

            #[inline]
            fn div(self, other: $t) -> Position {
                Self { x: self.x / other as f32, y: self.y / other as f32 }
            }
        }

        impl DivAssign<$t> for Position {
            #[inline]
            fn div_assign(&mut self, other: $t) {
                *self = Self { x: self.x / other as f32, y: self.y / other as f32 };
            }
        }

        impl Mul<$t> for Position {
            type Output = Position;

            #[inline]
            fn mul(self, other: $t) -> Position {
                Self { x: self.x * other as f32, y: self.y * other as f32 }
            }
        }

        impl MulAssign<$t> for Position {
            #[inline]
            fn mul_assign(&mut self, other: $t) {
                *self = Self { x: self.x * other as f32, y: self.y * other as f32 };
            }
        }
    )*)
}

scale_position_impl! { usize u8 u16 u32 u64 u128 isize i8 i16 i32 i64 i128 f32 f64 }

/// Represents a generic size.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Size {
    /// The width in pixels.
    pub width: f32,

    /// The height in pixels.
    pub height: f32,
}

impl Add for Size {
    type Output = Self;

    #[inline]
    fn add(self, other: Self) -> Self {
        Self { width: self.width + other.width, height: self.height + other.height }
    }
}

impl AddAssign for Size {
    #[inline]
    fn add_assign(&mut self, other: Self) {
        *self = Self { width: self.width + other.width, height: self.height + other.height };
    }
}

impl Sub for Size {
    type Output = Self;

    #[inline]
    fn sub(self, other: Self) -> Self {
        Self { width: self.width - other.width, height: self.height - other.height }
    }
}

impl SubAssign for Size {
    fn sub_assign(&mut self, other: Self) {
        *self = Self { width: self.width - other.width, height: self.height - other.height };
    }
}

impl Div for Size {
    type Output = Self;

    #[inline]
    fn div(self, other: Self) -> Self {
        Self { width: self.width / other.width, height: self.height / other.height }
    }
}

impl DivAssign for Size {
    #[inline]
    fn div_assign(&mut self, other: Self) {
        *self = Self { width: self.width / other.width, height: self.height / other.height };
    }
}

impl Mul for Size {
    type Output = Self;

    #[inline]
    fn mul(self, other: Self) -> Self {
        Self { width: self.width * other.width, height: self.height * other.height }
    }
}

impl MulAssign for Size {
    #[inline]
    fn mul_assign(&mut self, other: Self) {
        *self = Self { width: self.width * other.width, height: self.height * other.height };
    }
}

macro_rules! scale_size_impl {
    ($($t:ty)*) => ($(
        impl Div<$t> for Size {
            type Output = Size;

            #[inline]
            fn div(self, other: $t) -> Size {
                Self { width: self.width / other as f32, height: self.height / other as f32 }
            }
        }

        impl DivAssign<$t> for Size {
            #[inline]
            fn div_assign(&mut self, other: $t) {
                *self = Self { width: self.width / other as f32, height: self.height / other as f32 };
            }
        }

        impl Mul<$t> for Size {
            type Output = Size;

            #[inline]
            fn mul(self, other: $t) -> Size {
                Self { width: self.width * other as f32, height: self.height * other as f32 }
            }
        }

        impl MulAssign<$t> for Size {
            #[inline]
            fn mul_assign(&mut self, other: $t) {
                *self = Self { width: self.width * other as f32, height: self.height * other as f32 };
            }
        }
    )*)
}

scale_size_impl! { usize u8 u16 u32 u64 u128 isize i8 i16 i32 i64 i128 f32 f64 }
