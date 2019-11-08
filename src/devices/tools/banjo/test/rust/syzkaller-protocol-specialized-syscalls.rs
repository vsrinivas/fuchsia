// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.syzkaller.protocol.specialized.syscalls banjo file

#![allow(unused_imports, non_camel_case_types)]

use fuchsia_zircon as zircon;


// C ABI compat




#[repr(u32)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum zx_create_options_t {
    VARIANT0 = 0,
    VARIANT1 = 1,
    VARIANT2 = 2,
}


#[repr(C)]
#[derive(Debug, Default, PartialEq)]
pub union zx_create_types {
    pub type0: [i8; 1 as usize],
    pub type1: [i16; 1 as usize],
    pub type2: [i32; 1 as usize],
}

#[repr(C)]
#[derive(Debug, Default, PartialEq)]
pub union zx_handle_types {
    pub type0: zircon::sys::zx_handle_t,
    pub type1: zircon::sys::zx_handle_t,
    pub type2: zircon::sys::zx_handle_t,
}

