// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(non_camel_case_types)]

extern crate fuchsia_zircon_sys as zircon_sys;

use zircon_sys as sys;
use sys::{zx_handle_t, zx_off_t, zx_paddr_t, zx_status_t};
use std::os::raw::c_char;

pub const DRIVER_OPS_VERSION: u64 = 0x2b3490fa40d9f452;

#[repr(C)]
pub struct list_node_t {
    pub prev: *mut list_node_t,
    pub next: *mut list_node_t,
}

#[repr(C)]
pub struct zx_driver_ops_t {
    version: u64,

    pub init: Option<extern "C" fn (out_ctx: *mut *mut u8) -> zx_status_t>,
    pub bind: Option<extern "C" fn (ctx: *mut u8, device: *mut zx_device_t, cookie: *mut *mut u8) -> zx_status_t>,
    pub unbind: Option<extern "C" fn (ctx: *mut u8, device: *mut zx_device_t, cookie: *mut u8)>,
    pub create: Option<extern "C" fn (ctx: *mut u8, parent: *mut zx_device_t, name: *const c_char, args: *const c_char, resource: zx_handle_t) -> zx_status_t>,
    pub release: Option<extern "C" fn (ctx: *mut u8)>,
}

#[repr(C)]
pub struct zx_driver_t {
    pub name: *const c_char,
    pub ops: *const zx_driver_ops_t,
    pub ctx: *mut u8,
    pub libname: *const c_char,
    pub node: list_node_t,
    pub status: zx_status_t,
}

// Opaque struct
#[repr(u8)]
pub enum zx_device_t {
    variant1,
}

pub type iotxn_proto_data_t = [u64; 6];
pub type iotxn_extra_data_t = [u64; 6];

#[repr(C)]
pub struct iotxn_t {
    pub opcode: u32,
    pub flags: u32,
    pub offset: zx_off_t,
    pub length: zx_off_t,
    pub protocol: u32,
    pub status: zx_status_t,
    pub actual: zx_off_t,
    pub pflags: u32,
    pub vmo_handle: zx_handle_t,
    pub vmo_offset: u64,
    pub vmo_length: u64,
    pub phys: *mut zx_paddr_t,
    pub phys_count: u64,
    pub protocol_data: iotxn_proto_data_t,
    pub extra: iotxn_extra_data_t,
    pub node: list_node_t,
    pub context: *mut u8,
    pub virt: *mut u8,
    pub complete_cb: Option<extern "C" fn (txn: *mut iotxn_t, cookie: *mut u8)>,
    pub cookie: *mut u8,
    pub release_cb: Option<extern "C" fn (txn: *mut iotxn_t)>,
    pub phys_inline: [zx_paddr_t; 3],
}

pub const DEVICE_OPS_VERSION: u64 = 0xc9410d2a24f57424;

#[repr(C)]
pub struct zx_protocol_device_t {
    pub version: u64,

    pub get_protocol: Option<extern "C" fn (ctx: *mut u8, proto_id: u32, protocol: *mut u8) -> zx_status_t>,
    pub open: Option<extern "C" fn (ctx: *mut u8, dev_out: *mut *mut zx_device_t, flags: u32) -> zx_status_t>,
    pub open_at: Option<extern "C" fn (ctx: *mut u8, dev_out: *mut *mut zx_device_t, path: *const char, flags: u32) -> zx_status_t>,
    pub close: Option<extern "C" fn (ctx: *mut u8, flags: u32) -> zx_status_t>,
    pub unbind: Option<extern "C" fn (ctx: *mut u8)>,
    pub release: Option<extern "C" fn (ctx: *mut u8)>,
    pub read: Option<extern "C" fn (ctx: *mut u8, buf: *mut u8, count: usize, off: zx_off_t, actual: *mut usize) -> zx_status_t>,
    pub write: Option<extern "C" fn (ctx: *mut u8, buf: *const u8, count: usize, off: zx_off_t, actual: *mut usize) -> zx_status_t>,
    pub iotxn_queue: Option<extern "C" fn (ctx: *mut u8, txn: *mut iotxn_t)>,
    pub get_size: Option<extern "C" fn (ctx: *mut u8) -> zx_off_t>,
    pub ioctl: Option<extern "C" fn (ctx: *mut u8, op: u32, in_buf: *const u8, in_len: usize, out_buf: *mut u8, out_len: usize, out_actual: *mut usize) -> zx_status_t>,
    pub suspend: Option<extern "C" fn (ctx: *mut u8, flags: u32) -> zx_status_t>,
    pub resume: Option<extern "C" fn (ctx: *mut u8, flags: u32) -> zx_status_t>,
}
