// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.interface banjo file

#![allow(unused_imports, non_camel_case_types)]

use fuchsia_zircon as zircon;




#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct CookieKind(u32);

impl CookieKind {
    pub const CHOCOLATE: Self = Self(0);
    pub const GINGERBREAD: Self = Self(1);
    pub const SNICKERDOODLE: Self = Self(2);
}



