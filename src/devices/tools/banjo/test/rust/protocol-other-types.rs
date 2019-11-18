// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.protocol.other.types banjo file

#![allow(unused_imports, non_camel_case_types)]

use fuchsia_zircon as zircon;


pub const STRINGS_SIZE: u32 = 32;
#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct ThisIsAStruct {
    pub s: *mut std::ffi::c_void /* String */,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct ThisIsAnEnum(u32);

impl ThisIsAnEnum {
    pub const X: Self = Self(23);
}


#[repr(C)]
#[derive(Copy, Clone)]
pub union ThisIsAUnion {
    pub s: *mut std::ffi::c_void /* String */,
}

// unions can't autoderive debug, but it's useful for their parent types to
impl std::fmt::Debug for ThisIsAUnion {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "<ThisIsAUnion>")
    }
}

