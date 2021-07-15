// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A shim impelementation of the libm crate, binding directly to the in-tree libc's versions of
//! these functions.

extern "C" {
    #[link_name = "cbrt"]
    fn cbrt_raw(x: f64) -> f64;

    #[link_name = "frexpf"]
    fn frexpf_raw(x: f32, exp: *mut i32) -> f32;

    #[link_name = "ldexpf"]
    fn ldexpf_raw(x: f32, n: i32) -> f32;
}

/// Cube root
#[inline]
pub fn cbrt(x: f64) -> f64 {
    unsafe { cbrt_raw(x) }
}

/// Decomposes given floating point value x into a normalized fraction and an integral power of two.
#[inline]
pub fn frexpf(x: f32) -> (f32, i32) {
    let mut exp: i32 = 0;
    let v = unsafe { frexpf_raw(x, &mut exp) };
    (v, exp)
}

/// Multiplies a floating point value arg by the number 2 raised to the exp power.
#[inline]
pub fn ldexpf(x: f32, n: i32) -> f32 {
    unsafe { ldexpf_raw(x, n) }
}
