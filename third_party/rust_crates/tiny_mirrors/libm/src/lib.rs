// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

//! A shim impelementation of the libm crate, binding directly to the in-tree libc's versions of
//! these functions.

extern "C" {
    #[link_name = "cbrt"]
    fn cbrt_raw(x: f64) -> f64;
}

/// Cube root
pub fn cbrt(x: f64) -> f64 {
    unsafe { cbrt_raw(x) }
}
