// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(non_camel_case_types)]

extern crate magenta_sys;

use magenta_sys as sys;
use sys::{mx_handle_t, mx_off_t, mx_paddr_t, mx_status_t};
use std::os::raw::c_char;

pub const DRIVER_OPS_VERSION: u64 = 0x2b3490fa40d9f452;

#[repr(C)]
pub struct list_node_t {
    pub prev: *mut list_node_t,
    pub next: *mut list_node_t,
}

#[repr(C)]
pub struct mx_driver_ops_t {
    version: u64,

    pub init: Option<extern "C" fn (out_ctx: *mut *mut u8) -> mx_status_t>,
    pub bind: Option<extern "C" fn (ctx: *mut u8, device: *mut mx_device_t, cookie: *mut *mut u8) -> mx_status_t>,
    pub unbind: Option<extern "C" fn (ctx: *mut u8, device: *mut mx_device_t, cookie: *mut u8)>,
    pub create: Option<extern "C" fn (ctx: *mut u8, parent: *mut mx_device_t, name: *const c_char, args: *const c_char, resource: mx_handle_t) -> mx_status_t>,
    pub release: Option<extern "C" fn (ctx: *mut u8)>,
}

#[repr(C)]
pub struct mx_driver_t {
    pub name: *const c_char,
    pub ops: *const mx_driver_ops_t,
    pub ctx: *mut u8,
    pub libname: *const c_char,
    pub node: list_node_t,
    pub status: mx_status_t,
}

// Opaque struct
#[repr(u8)]
pub enum mx_device_t {
    variant1,
}

pub type iotxn_proto_data_t = [u64; 6];
pub type iotxn_extra_data_t = [u64; 6];

#[repr(C)]
pub struct iotxn_t {
    pub opcode: u32,
    pub flags: u32,
    pub offset: mx_off_t,
    pub length: mx_off_t,
    pub protocol: u32,
    pub status: mx_status_t,
    pub actual: mx_off_t,
    pub pflags: u32,
    pub vmo_handle: mx_handle_t,
    pub vmo_offset: u64,
    pub vmo_length: u64,
    pub phys: *mut mx_paddr_t,
    pub phys_count: u64,
    pub protocol_data: iotxn_proto_data_t,
    pub extra: iotxn_extra_data_t,
    pub node: list_node_t,
    pub context: *mut u8,
    pub virt: *mut u8,
    pub complete_cb: Option<extern "C" fn (txn: *mut iotxn_t, cookie: *mut u8)>,
    pub cookie: *mut u8,
    pub release_cb: Option<extern "C" fn (txn: *mut iotxn_t)>,
    pub phys_inline: [mx_paddr_t; 3],
}

pub const DEVICE_OPS_VERSION: u64 = 0xc9410d2a24f57424;

#[repr(C)]
pub struct mx_protocol_device_t {
    pub version: u64,

    pub get_protocol: Option<extern "C" fn (ctx: *mut u8, proto_id: u32, protocol: *mut u8) -> mx_status_t>,
    pub open: Option<extern "C" fn (ctx: *mut u8, dev_out: *mut *mut mx_device_t, flags: u32) -> mx_status_t>,
    pub open_at: Option<extern "C" fn (ctx: *mut u8, dev_out: *mut *mut mx_device_t, path: *const char, flags: u32) -> mx_status_t>,
    pub close: Option<extern "C" fn (ctx: *mut u8, flags: u32) -> mx_status_t>,
    pub unbind: Option<extern "C" fn (ctx: *mut u8)>,
    pub release: Option<extern "C" fn (ctx: *mut u8)>,
    pub read: Option<extern "C" fn (ctx: *mut u8, buf: *mut u8, count: usize, off: mx_off_t, actual: *mut usize) -> mx_status_t>,
    pub write: Option<extern "C" fn (ctx: *mut u8, buf: *const u8, count: usize, off: mx_off_t, actual: *mut usize) -> mx_status_t>,
    pub iotxn_queue: Option<extern "C" fn (ctx: *mut u8, txn: *mut iotxn_t)>,
    pub get_size: Option<extern "C" fn (ctx: *mut u8) -> mx_off_t>,
    pub ioctl: Option<extern "C" fn (ctx: *mut u8, op: u32, in_buf: *const u8, in_len: usize, out_buf: *mut u8, out_len: usize, out_actual: *mut usize) -> mx_status_t>,
    pub suspend: Option<extern "C" fn (ctx: *mut u8, flags: u32) -> mx_status_t>,
    pub resume: Option<extern "C" fn (ctx: *mut u8, flags: u32) -> mx_status_t>,
}
