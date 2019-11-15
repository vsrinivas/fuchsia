// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.syzkaller.union banjo file

#![allow(unused_imports, non_camel_case_types)]

use fuchsia_zircon as zircon;





#[repr(C)]
#[derive(Copy, Clone)]
pub union primitive_types {
    pub b: bool,
    pub i8: i8,
    pub i16: i16,
    pub i32: i32,
    pub i64: i64,
    pub u8: u8,
    pub u16: u16,
    pub u32: u32,
    pub u64: u64,
    pub h: zircon::sys::zx_handle_t,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub union arrays {
    pub rd_vptr: [*mut std::ffi::c_void /* Voidptr */ ; 1 as usize],
    pub wr_vptr: [*mut std::ffi::c_void /* Voidptr */ ; 1 as usize],
    pub rd_sz: [usize; 1 as usize],
    pub rd_sz: [usize; 1 as usize],
    pub rd_b: [bool; 1 as usize],
    pub wr_b: [bool; 1 as usize],
    pub rd_i8: [i8; 1 as usize],
    pub wr_i8: [i8; 1 as usize],
    pub rd_i16: [i16; 1 as usize],
    pub wr_i16: [i16; 1 as usize],
    pub rd_i32: [i32; 1 as usize],
    pub wr_i32: [i32; 1 as usize],
    pub rd_i64: [i64; 1 as usize],
    pub wr_i64: [i64; 1 as usize],
    pub rd_u8: [u8; 1 as usize],
    pub wr_u8: [u8; 1 as usize],
    pub rd_u16: [u16; 1 as usize],
    pub wr_u16: [u16; 1 as usize],
    pub rd_u32: [u32; 1 as usize],
    pub wr_u32: [u32; 1 as usize],
    pub rd_u64: [u64; 1 as usize],
    pub wr_u64: [u64; 1 as usize],
    pub rd_h: [zircon::sys::zx_handle_t; 1 as usize],
    pub wr_h: [zircon::sys::zx_handle_t; 1 as usize],
}

#[repr(C)]
#[derive(Copy, Clone)]
pub union Packet {
    pub i32: u32,
    pub u32: u32,
}

