// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(non_camel_case_types)]

#[macro_use]
extern crate bitflags;
extern crate fuchsia_zircon_sys as zircon_sys;

use zircon_sys as sys;
use sys::{zx_handle_t, zx_off_t, zx_paddr_t, zx_status_t};
use std::os::raw::c_char;

// References to Zircon DDK's driver.h

// Opaque structs
#[repr(u8)]
pub enum zx_device_t {
    variant1,
}

#[repr(u8)]
pub enum zx_device_prop_t {
    variant1,
}

#[repr(u8)]
pub enum zx_driver_t {
    variant1,
}

pub const ZX_DEVICE_NAME_MAX: usize = 31;

#[repr(C)]
pub struct list_node_t {
    pub prev: *mut list_node_t,
    pub next: *mut list_node_t,
}

impl Default for list_node_t {
    fn default() -> list_node_t {
        list_node_t {
            prev: std::ptr::null_mut(),
            next: std::ptr::null_mut(),
        }
    }
}

pub const DRIVER_OPS_VERSION: u64 = 0x2b3490fa40d9f452;

#[repr(C)]
pub struct zx_driver_ops_t {
    version: u64,

    pub init: Option<extern "C" fn (out_ctx: *mut *mut u8) -> zx_status_t>,
    pub bind: Option<extern "C" fn (ctx: *mut u8, device: *mut zx_device_t, cookie: *mut *mut u8) -> zx_status_t>,
    pub unbind: Option<extern "C" fn (ctx: *mut u8, device: *mut zx_device_t, cookie: *mut u8)>,
    pub create: Option<extern "C" fn (ctx: *mut u8, parent: *mut zx_device_t, name: *const c_char, args: *const c_char, resource: zx_handle_t) -> zx_status_t>,
    pub release: Option<extern "C" fn (ctx: *mut u8)>,
}

// References to Zircon DDK's iotxn.h

pub const IOTXN_OP_READ: u8 = 1;
pub const IOTXN_OP_WRITE: u8 = 2;

pub const IOTXN_CACHE_INVALIDATE: u32 = sys::ZX_VMO_OP_CACHE_INVALIDATE;
pub const IOTXN_CACHE_CLEAN: u32 = sys::ZX_VMO_OP_CACHE_CLEAN;
pub const IOTXN_CACHE_CLEAN_INVALIDATE: u32 = sys::ZX_VMO_OP_CACHE_CLEAN_INVALIDATE;
pub const IOTXN_CACHE_SYNC: u32 = sys::ZX_VMO_OP_CACHE_SYNC;

pub const IOTXN_SYNC_BEFORE: u8 = 1;
pub const IOTXN_SYNC_AFTER: u8 = 2;

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
    pub open_at: Option<extern "C" fn (ctx: *mut u8, dev_out: *mut *mut zx_device_t, path: *const c_char, flags: u32) -> zx_status_t>,
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

bitflags! {
    #[repr(C)]
    pub flags device_add_flags_t: u32 {
        const DEVICE_ADD_NON_BINDABLE = 1 << 0,
        const DEVICE_ADD_INSTANCE     = 1 << 1,
        const DEVICE_ADD_BUSDEV       = 1 << 2,
    }
}

// Device Manager API
const DEVICE_ADD_ARGS_VERSION: u64 = 0x96a64134d56e88e3;

#[repr(C)]
pub struct device_add_args_t {
    version: u64,
    pub name: *const c_char,
    pub ctx: *mut u8,
    pub ops: *mut zx_protocol_device_t,
    pub props: *mut zx_device_prop_t,
    pub prop_count: u32,
    pub proto_id: u32,
    pub proto_ops: *mut u8,
    pub busdev_args: *const c_char,
    pub rsrc: sys::zx_handle_t,
    pub flags: device_add_flags_t,
}

impl device_add_args_t {
    pub fn new() -> device_add_args_t {
        device_add_args_t {
            version: DEVICE_ADD_ARGS_VERSION,
            name: std::ptr::null_mut(),
            ctx: std::ptr::null_mut(),
            ops: std::ptr::null_mut(),
            props: std::ptr::null_mut(),
            prop_count: 0,
            proto_id: 0,
            proto_ops: std::ptr::null_mut(),
            busdev_args: std::ptr::null_mut(),
            rsrc: 0,
            flags: DEVICE_ADD_NON_BINDABLE,
        }
    }
}

#[repr(C)]
pub struct zx_driver_rec {
    pub ops: *const zx_driver_ops_t,
    pub driver: *mut zx_driver_t,
    pub log_flags: u32,
}

#[link(name = "ddk")]
extern "C" {
    pub fn device_add(parent: *mut zx_device_t, args: *mut device_add_args_t, out: *mut *mut zx_device_t) -> sys::zx_status_t;
    pub fn device_add_from_driver(driver: *mut zx_driver_t, parent: *mut zx_device_t, args: *mut device_add_args_t, out: *mut *mut zx_device_t) -> sys::zx_status_t;
    pub fn device_remove(device: *mut zx_device_t) -> sys::zx_status_t;
    pub fn device_rebind(device: *mut zx_device_t) -> sys::zx_status_t;
    pub fn device_unbind(device: *mut zx_device_t);
    pub fn load_firmware(device: *mut zx_device_t, path: *const c_char, fw: *mut sys::zx_handle_t, size: *mut usize) -> sys::zx_status_t;
    pub fn device_get_name(dev: *mut zx_device_t) -> *const c_char;
    pub fn device_get_parent(dev: *mut zx_device_t) -> *mut zx_device_t;
    pub fn device_get_resource(dev: *mut zx_device_t) -> sys::zx_handle_t;
    pub fn device_get_protocol(dev: *mut zx_device_t, proto_id: u32, protocol: *mut u8) -> sys::zx_status_t;
    pub fn device_read(dev: *mut zx_device_t, buf: *mut u8, count: usize, off: sys::zx_off_t, actual: *mut usize) -> sys::zx_status_t;
    pub fn device_write(dev: *mut zx_device_t, buf: *const u8, count: usize, off: sys::zx_off_t, actual: *mut usize) -> sys::zx_status_t;
    pub fn device_get_size(dev: *mut zx_device_t) -> sys::zx_off_t;
    pub fn device_ioctl(dev: *mut zx_device_t, op: u32, in_buf: *const u8, in_len: usize, out_buf: *mut u8, out_len: usize, out_actual: *mut usize) -> sys::zx_status_t;
    pub fn device_iotxn_queue(dev: *mut zx_device_t, txn: *mut iotxn_t) -> sys::zx_status_t;
    pub fn device_stat_clr_set(dev: *mut zx_device_t, clearflag: sys::zx_signals_t, setflag: sys::zx_signals_t);
}
