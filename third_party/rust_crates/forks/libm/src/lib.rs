// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A shim impelementation of the libm crate, binding directly to the in-tree libc's versions of
//! these functions.

#[macro_use]
extern crate static_assertions;

// Make sure we aren't building for one of the "esoteric systems" on which c_int is not identical
// to i32 (https://doc.rust-lang.org/std/os/raw/type.c_int.html).
assert_type_eq_all!(std::os::raw::c_int, i32);

extern "C" {
    #[link_name = "cbrt"]
    fn cbrt_raw(x: f64) -> f64;

    #[link_name = "frexpf"]
    fn frexpf_raw(x: f32, exp: *mut i32) -> f32;

    #[link_name = "ldexp"]
    fn ldexp_raw(x: f64, n: i32) -> f64;

    #[link_name = "ldexpf"]
    fn ldexpf_raw(x: f32, n: i32) -> f32;

    #[link_name = "modf"]
    fn modf_raw(x: f64, integer_part: *mut f64) -> f64;
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

/// Multiplies an f64 arg by the number 2 raised to the exp power.
#[inline]
pub fn ldexp(x: f64, n: i32) -> f64 {
    unsafe { ldexp_raw(x, n) }
}

/// Multiplies an f32 arg by the number 2 raised to the exp power.
#[inline]
pub fn ldexpf(x: f32, n: i32) -> f32 {
    unsafe { ldexpf_raw(x, n) }
}

/// Returns the fractional and integral parts of an f64. The return ordering `(fractional_part,
/// integral_part)` is based on the libm crate from crates.io.
#[inline]
pub fn modf(x: f64) -> (f64, f64) {
    let mut integral_part = 0.0;
    let fractional_part = unsafe { modf_raw(x, &mut integral_part) };
    (fractional_part, integral_part)
}
