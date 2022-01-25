// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fmt;

/// A 24.8 fixed point number.
///
/// Internally this is stored as a single `i32` value, with methods to convert
/// to/from `f32` floating point values.
#[derive(Copy, Clone, Eq, PartialEq)]
pub struct Fixed(i32);

impl Fixed {
    /// Creates a `Fixed` from raw bytes.
    pub fn from_bits(v: i32) -> Self {
        Fixed(v)
    }

    /// Creates a `Fixed` from a floating point value.
    pub fn from_float(v: f32) -> Self {
        Fixed((v * 256.0) as i32)
    }

    /// Returns a floating point representation of this value.
    pub fn to_float(self) -> f32 {
        (self.0 as f32) / 256.0
    }

    /// Returns the underlying integer representation of this value.
    pub fn bits(self) -> i32 {
        self.0
    }
}

impl fmt::Display for Fixed {
    fn fmt(&self, fmt: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(fmt, "{}", self.to_float())
    }
}

impl fmt::Debug for Fixed {
    fn fmt(&self, fmt: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(fmt, "{:?}", self.to_float())
    }
}

impl From<f32> for Fixed {
    fn from(v: f32) -> Self {
        Self::from_float(v)
    }
}

impl From<i32> for Fixed {
    fn from(v: i32) -> Self {
        Self::from_bits(v)
    }
}

impl Into<f32> for Fixed {
    fn into(self) -> f32 {
        self.to_float()
    }
}

#[cfg(test)]
mod tests {
    use std::mem;

    use super::*;

    #[test]
    fn fixed_to_float() {
        let fixed: Fixed = 256.into();
        assert_eq!(1.0, fixed.to_float());

        let fixed: Fixed = 257.into();
        assert_eq!(1.00390625, fixed.to_float());

        unsafe {
            let fixed: Fixed = mem::transmute::<u32, i32>(0xffffff00).into();
            assert_eq!(-1.0, fixed.to_float());

            let fixed: Fixed = mem::transmute::<u32, i32>(0xfffffeff).into();
            assert_eq!(-1.00390625, fixed.to_float());
        }
    }

    #[test]
    fn float_to_fixed() {
        let fixed: Fixed = 1.0.into();
        assert_eq!(256, fixed.bits());

        let fixed: Fixed = 1.00390625.into();
        assert_eq!(257, fixed.bits());

        unsafe {
            let fixed: Fixed = (-1.0).into();
            assert_eq!(mem::transmute::<u32, i32>(0xffffff00), fixed.bits());

            let fixed: Fixed = (-1.00390625).into();
            assert_eq!(mem::transmute::<u32, i32>(0xfffffeff), fixed.bits());
        }
    }
}
