// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.simple banjo file

#![allow(unused_imports, non_camel_case_types)]

use fuchsia_zircon as zircon;



#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct Point {
    pub x: i32,
    pub y: i32,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct StructWithZxField {
    pub status: zircon::sys::zx_status_t,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct Direction(u32);

impl Direction {
    pub const UP: Self = Self(0);
    pub const DOWN: Self = Self(1);
    pub const LEFT: Self = Self(2);
    pub const RIGHT: Self = Self(3);
}



