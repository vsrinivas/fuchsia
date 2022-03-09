// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(non_camel_case_types)]

#[cfg(all(target_arch = "aarch64", target_feature = "neon",))]
mod aarch64;
#[cfg(not(any(
    all(
        target_arch = "x86_64",
        target_feature = "avx",
        target_feature = "avx2",
        target_feature = "fma"
    ),
    all(target_arch = "aarch64", target_feature = "neon",),
)))]
mod auto;
#[cfg(all(
    target_arch = "x86_64",
    target_feature = "avx",
    target_feature = "avx2",
    target_feature = "fma"
))]
mod avx;

#[cfg(all(target_arch = "aarch64", target_feature = "neon",))]
pub use aarch64::*;
#[cfg(not(any(
    all(
        target_arch = "x86_64",
        target_feature = "avx",
        target_feature = "avx2",
        target_feature = "fma"
    ),
    all(target_arch = "aarch64", target_feature = "neon",),
)))]
pub use auto::*;
#[cfg(all(
    target_arch = "x86_64",
    target_feature = "avx",
    target_feature = "avx2",
    target_feature = "fma"
))]
pub use avx::*;

pub trait Simd {
    const LANES: usize;
}

impl Simd for u8x32 {
    const LANES: usize = 32;
}

impl Simd for i8x16 {
    const LANES: usize = 16;
}

impl Simd for i16x16 {
    const LANES: usize = 16;
}

impl Simd for i32x8 {
    const LANES: usize = 8;
}

impl Simd for f32x8 {
    const LANES: usize = 8;
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn f32x8_splat() {
        for v in [1.0, 0.0, f32::INFINITY, f32::NEG_INFINITY] {
            assert_eq!(f32x8::splat(v).as_array(), [v; 8]);
        }
    }

    #[test]
    fn f32x8_indexed() {
        let index: Vec<f32> = (0..8).map(|v| v as f32).collect();
        assert_eq!(f32x8::indexed().as_array(), index[..]);
    }

    #[test]
    fn f32x8_from_array() {
        let value = [-5.0, -4.0, -3.0, -2.0, -1.0, 0.0, 1.0, 2.0];
        assert_eq!(f32x8::from_array(value).as_array(), value);
    }

    #[test]
    fn f32x8_floor() {
        // Verify rounding toward negative infinity.
        let value = [0.0, 1.1, 2.5, 3.5, 4.5, 5.5, 6.6, 7.7];
        let expected = [0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0];
        assert_eq!(f32x8::from_array(value).floor().as_array(), expected);

        let value = [-0.0, -0.1, -1.5, -2.5, -3.5, -4.5, -5.6, -6.7];
        let expected = [-0.0, -1.0, -2.0, -3.0, -4.0, -5.0, -6.0, -7.0];
        assert_eq!(f32x8::from_array(value).floor().as_array(), expected);
    }
}
