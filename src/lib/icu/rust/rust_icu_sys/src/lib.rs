// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![doc(test(ignore))]
#![allow(dead_code, non_snake_case, non_camel_case_types, non_upper_case_globals, unused_imports)]

include!(concat!("../bindgen", "/macros.rs"));
include!(concat!("../bindgen", "/lib.rs"));

// Add the ability to print the error code, so that it can be reported in
// aggregaated errors.
impl std::fmt::Display for UErrorCode {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
            write!(f, "{:?}", self)
        }
}


// A "fake" extern used to express link preferences.
#[link(name = "icui18n", kind = "dylib")]
#[link(name = "icuuc", kind = "dylib")]
extern "C" {}
