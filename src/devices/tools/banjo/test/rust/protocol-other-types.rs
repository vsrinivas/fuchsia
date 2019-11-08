// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.protocol.other.types banjo file

#![allow(unused_imports, non_camel_case_types)]

use fuchsia_zircon as zircon;


// C ABI compat
pub const STRINGS_SIZE: u32 = 32;

#[repr(C)]
#[derive(Debug, PartialEq)]
pub struct ThisIsAStruct {
    pub s: *mut std::ffi::c_void /* String */,
}


#[repr(u32)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum this_is_an_enum_t {
    X = 23,
}


#[repr(C)]
#[derive(Debug, Default, PartialEq)]
pub union ThisIsAUnion {
    pub s: *mut std::ffi::c_void /* String */,
}

