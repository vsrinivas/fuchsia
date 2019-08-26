// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![doc(test(ignore))]
#![allow(dead_code, non_snake_case, non_camel_case_types, non_upper_case_globals, unused_imports)]

include!(concat!("../bindgen", "/macros_norename.rs"));
include!(concat!("../bindgen", "/lib.rs"));

extern crate libc;

// A "fake" extern used to express link preferences.
#[link(name = "icudata", kind = "dylib")]
#[link(name = "icui18n", kind = "dylib")]
#[link(name = "icuuc", kind = "dylib")]
#[link(name = "stdc++", kind = "dylib")]
extern "C" {}
