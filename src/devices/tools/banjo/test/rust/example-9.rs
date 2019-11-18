// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.example9 banjo file

#![allow(unused_imports, non_camel_case_types)]

use fuchsia_zircon as zircon;


pub const FAVORITE_ECHO: EchoMe = zero;
#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct EchoMore {
    pub first: u32,
    pub second: u64,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct EchoMe(u32);

impl EchoMe {
    pub const ZERO: Self = Self(0);
    pub const ONE: Self = Self(1);
}



