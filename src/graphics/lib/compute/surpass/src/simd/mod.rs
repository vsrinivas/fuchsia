// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(non_camel_case_types)]

#[cfg(not(all(
    target_arch = "x86_64",
    target_feature = "avx",
    target_feature = "avx2",
    target_feature = "fma"
)))]
mod auto;
#[cfg(all(
    target_arch = "x86_64",
    target_feature = "avx",
    target_feature = "avx2",
    target_feature = "fma"
))]
mod avx;

#[cfg(not(all(
    target_arch = "x86_64",
    target_feature = "avx",
    target_feature = "avx2",
    target_feature = "fma"
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
