// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.syzkaller.struct banjo file

#![allow(unused_imports, non_camel_case_types)]

use fuchsia_zircon as zircon;



#[repr(C)]
#[derive(Copy, Clone)]
pub struct primitive_types {
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
pub struct strings {
    pub rd_str: [u8; RD_STR_LEN as usize],
    pub wr_str: [u8; WR_STR_LEN as usize],
    pub rd_str_len: usize,
    pub wr_str_len: usize,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct arrays {
    pub rd_vptr: [*mut std::ffi::c_void /* Voidptr */ ; RD_VPTR_LEN as usize],
    pub wr_vptr: [*mut std::ffi::c_void /* Voidptr */ ; WR_VPTR_LEN as usize],
    pub rd_vptr_len: usize,
    pub wr_vptr_len: usize,
    pub rd_sz: [usize; RD_SZ_LEN as usize],
    pub rd_sz: [usize; WR_SZ_LEN as usize],
    pub rd_sz_len: usize,
    pub wr_sz_len: usize,
    pub rd_b: [bool; RD_B_LEN as usize],
    pub wr_b: [bool; WR_B_LEN as usize],
    pub rd_b_len: usize,
    pub wr_b_len: usize,
    pub rd_i8: [i8; RD_I8_LEN as usize],
    pub wr_i8: [i8; WR_I8_LEN as usize],
    pub rd_i8_len: usize,
    pub wr_i8_len: usize,
    pub rd_i16: [i16; RD_I16_LEN as usize],
    pub wr_i16: [i16; WR_I16_LEN as usize],
    pub rd_i16_len: usize,
    pub wr_i16_len: usize,
    pub rd_i32: [i32; RD_I32_LEN as usize],
    pub wr_i32: [i32; WR_I32_LEN as usize],
    pub rd_i32_len: usize,
    pub wr_i32_len: usize,
    pub rd_i64: [i64; RD_I64_LEN as usize],
    pub wr_i64: [i64; WR_I64_LEN as usize],
    pub rd_i64_len: usize,
    pub wr_i64_len: usize,
    pub rd_u8: [u8; RD_U8_LEN as usize],
    pub wr_u8: [u8; WR_U8_LEN as usize],
    pub rd_u8_len: usize,
    pub wr_u8_len: usize,
    pub rd_u16: [u16; RD_U16_LEN as usize],
    pub wr_u16: [u16; WR_U16_LEN as usize],
    pub rd_u16_len: usize,
    pub wr_u16_len: usize,
    pub rd_u32: [u32; RD_U32_LEN as usize],
    pub wr_u32: [u32; WR_U32_LEN as usize],
    pub rd_u32_len: usize,
    pub wr_u32_len: usize,
    pub rd_u64: [u64; RD_U64_LEN as usize],
    pub wr_u64: [u64; WR_U64_LEN as usize],
    pub rd_u64_len: usize,
    pub wr_u64_len: usize,
    pub rd_h: [zircon::sys::zx_handle_t; RD_H_LEN as usize],
    pub wr_h: [zircon::sys::zx_handle_t; WR_H_LEN as usize],
    pub rd_h_len: usize,
    pub wr_h_len: usize,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct Point {
    pub x: i32,
    pub y: i32,
}



