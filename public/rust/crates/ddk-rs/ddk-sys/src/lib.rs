// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(non_camel_case_types)]

extern crate fuchsia_zircon as zircon;

use zircon::sys as sys;
use std::os::raw::c_char;

// References to Zircon DDK's driver.h

// Copied from fuchsia-zircon-sys.
macro_rules! multiconst {
    ($typename:ident, [$($rawname:ident = $value:expr;)*]) => {
        $(
            pub const $rawname: $typename = $value;
        )*
    }
}

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
    fn default() -> Self {
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

    pub init: Option<extern "C" fn (out_ctx: *mut *mut u8) -> sys::zx_status_t>,
    pub bind: Option<extern "C" fn (ctx: *mut u8, device: *mut zx_device_t, cookie: *mut *mut u8) -> sys::zx_status_t>,
    pub unbind: Option<extern "C" fn (ctx: *mut u8, device: *mut zx_device_t, cookie: *mut u8)>,
    pub create: Option<extern "C" fn (ctx: *mut u8, parent: *mut zx_device_t, name: *const c_char, args: *const c_char, resource: sys::zx_handle_t) -> sys::zx_status_t>,
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
    pub offset: sys::zx_off_t,
    pub length: sys::zx_off_t,
    pub protocol: u32,
    pub status: sys::zx_status_t,
    pub actual: sys::zx_off_t,
    pub pflags: u32,
    pub vmo_handle: sys::zx_handle_t,
    pub vmo_offset: u64,
    pub vmo_length: u64,
    pub phys: *mut sys::zx_paddr_t,
    pub phys_count: u64,
    pub protocol_data: iotxn_proto_data_t,
    pub extra: iotxn_extra_data_t,
    pub node: list_node_t,
    pub context: *mut u8,
    pub virt: *mut u8,
    pub complete_cb: Option<extern "C" fn (txn: *mut iotxn_t, cookie: *mut u8)>,
    pub cookie: *mut u8,
    pub release_cb: Option<extern "C" fn (txn: *mut iotxn_t)>,
    pub phys_inline: [sys::zx_paddr_t; 3],
}

pub const DEVICE_OPS_VERSION: u64 = 0xc9410d2a24f57424;

#[repr(C)]
pub struct zx_protocol_device_t {
    pub version: u64,

    pub get_protocol: Option<extern "C" fn (ctx: *mut u8, proto_id: u32, protocol: *mut u8) -> sys::zx_status_t>,
    pub open: Option<extern "C" fn (ctx: *mut u8, dev_out: *mut *mut zx_device_t, flags: u32) -> sys::zx_status_t>,
    pub open_at: Option<extern "C" fn (ctx: *mut u8, dev_out: *mut *mut zx_device_t, path: *const c_char, flags: u32) -> sys::zx_status_t>,
    pub close: Option<extern "C" fn (ctx: *mut u8, flags: u32) -> sys::zx_status_t>,
    pub unbind: Option<extern "C" fn (ctx: *mut u8)>,
    pub release: Option<extern "C" fn (ctx: *mut u8)>,
    pub read: Option<extern "C" fn (ctx: *mut u8, buf: *mut u8, count: usize, off: sys::zx_off_t, actual: *mut usize) -> sys::zx_status_t>,
    pub write: Option<extern "C" fn (ctx: *mut u8, buf: *const u8, count: usize, off: sys::zx_off_t, actual: *mut usize) -> sys::zx_status_t>,
    pub iotxn_queue: Option<extern "C" fn (ctx: *mut u8, txn: *mut iotxn_t)>,
    pub get_size: Option<extern "C" fn (ctx: *mut u8) -> sys::zx_off_t>,
    pub ioctl: Option<extern "C" fn (ctx: *mut u8, op: u32, in_buf: *const u8, in_len: usize, out_buf: *mut u8, out_len: usize, out_actual: *mut usize) -> sys::zx_status_t>,
    pub suspend: Option<extern "C" fn (ctx: *mut u8, flags: u32) -> sys::zx_status_t>,
    pub resume: Option<extern "C" fn (ctx: *mut u8, flags: u32) -> sys::zx_status_t>,
}

pub type device_add_flags_t = u32;

multiconst!(device_add_flags_t, [
    DEVICE_ADD_NONE         = 0;
    DEVICE_ADD_NON_BINDABLE = 1 << 0;
    DEVICE_ADD_INSTANCE     = 1 << 1;
    DEVICE_ADD_MUST_ISOLATE = 1 << 2;
    DEVICE_ADD_INVISIBLE    = 1 << 3;
]);

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
    pub proxy_args: *const c_char,
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
            proxy_args: std::ptr::null_mut(),
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

// USB request types
pub type usb_request_type_t = u8;

multiconst!(usb_request_type_t, [
    USB_DIR_OUT         = 0 << 7;
    USB_DIR_IN          = 1 << 7;
    USB_DIR_MASK        = 1 << 7;
    USB_TYPE_STANDARD   = 0 << 5;
    USB_TYPE_CLASS      = 1 << 5;
    USB_TYPE_VENDOR     = 2 << 5;
    USB_TYPE_MASK       = 3 << 5;
    USB_RECIP_DEVICE    = 0 << 0;
    USB_RECIP_INTERFACE = 1 << 0;
    USB_RECIP_ENDPOINT  = 2 << 0;
    USB_RECIP_OTHER     = 3 << 0;
    USB_RECIP_MASK      = 0x1f << 0;
]);

// USB request values
#[repr(u8)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum usb_request_value_t {
    USB_REQ_GET_STATUS        = 0x00,
    USB_REQ_CLEAR_FEATURE     = 0x01,
    USB_REQ_SET_FEATURE       = 0x03,
    USB_REQ_SET_ADDRESS       = 0x05,
    USB_REQ_GET_DESCRIPTOR    = 0x06,
    USB_REQ_SET_DESCRIPTOR    = 0x07,
    USB_REQ_GET_CONFIGURATION = 0x08,
    USB_REQ_SET_CONFIGURATION = 0x09,
    USB_REQ_GET_INTERFACE     = 0x0a,
    USB_REQ_SET_INTERFACE     = 0x0b,
    USB_REQ_SYNCH_FRAME       = 0x0c,
}

// USB device/interface classes
#[repr(u8)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum usb_class_t {
    USB_CLASS_AUDIO      = 0x01,
    USB_CLASS_COMM       = 0x02,
    USB_CLASS_HID        = 0x03,
    USB_CLASS_PHYSICAL   = 0x05,
    USB_CLASS_IMAGING    = 0x06,
    USB_CLASS_PRINTER    = 0x07,
    USB_CLASS_MSC        = 0x08,
    USB_CLASS_HUB        = 0x09,
    USB_CLASS_CDC        = 0x0a,
    USB_CLASS_CCID       = 0x0b,
    USB_CLASS_SECURITY   = 0x0d,
    USB_CLASS_VIDEO      = 0x0e,
    USB_CLASS_HEALTHCARE = 0x0f,
    USB_CLASS_DIAGNOSTIC = 0xdc,
    USB_CLASS_WIRELESS   = 0xe0,
    USB_CLASS_MISC       = 0xef,
    USB_CLASS_VENDOR     = 0xff,
}

pub const USB_SUBCLASS_MSC_SCSI: u8      = 0x06;
pub const USB_PROTOCOL_MSC_BULK_ONLY: u8 = 0x50;

pub const ZX_PROTOCOL_USB: u32 = 0x70555342; // 'pUSB'

#[repr(C)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum usb_speed_t {
    USB_SPEED_UNDEFINED = 0,
    USB_SPEED_FULL = 1,
    USB_SPEED_LOW = 2,
    USB_SPEED_HIGH = 3,
    USB_SPEED_SUPER = 4,
}

// Opaque struct
#[repr(u8)]
pub enum usb_request_t {
    variant1,
}

#[repr(C, packed)]
pub struct usb_interface_descriptor_t {
    bLength: u8,
    bDescriptorType: u8,
    bInterfaceNumber: u8,
    bAlternateSetting: u8,
    bNumEndpoints: u8,
    bInterfaceClass: u8,
    bInterfaceSubClass: u8,
    bInterfaceProtocol: u8,
    iInterface: u8,
}

#[repr(C)]
pub struct usb_protocol_ops_t {
    pub control: extern "C" fn (ctx: *mut u8, request_type: usb_request_type_t,
        request: usb_request_value_t, value: u16, index: u16, data: *mut u8, length: usize,
        timeout: sys::zx_time_t, out_length: *mut usize) -> sys::zx_status_t,
    pub request_queue: extern "C" fn (ctx: *mut u8, usb_request: *mut usb_request_t),
    pub get_speed: extern "C" fn (ctx: *mut u8) -> usb_speed_t,
    pub set_interface: extern "C" fn (ctx: *mut u8, interface_number: i32, alt_setting: i32)
        -> sys::zx_status_t,
    pub set_configuration: extern "C" fn (ctx: *mut u8, configuration: i32) -> sys::zx_status_t,
    pub reset_endpoint: extern "C" fn (ctx: *mut u8, ep_address: u8) -> sys::zx_status_t,
    pub get_max_transfer_size: extern "C" fn (ctx: *mut u8, ep_address: u8) -> usize,
    pub get_device_id: extern "C" fn (ctx: *mut u8) -> u32,
    pub get_descriptor_list: extern "C" fn (ctx: *mut u8, out_descriptors: *mut *mut u8,
        out_length: *mut usize) -> sys::zx_status_t,
    pub get_additional_descriptor_list: extern "C" fn (ctx: *mut u8, out_descriptors: *mut *mut u8,
        out_length: *mut usize) -> sys::zx_status_t,
    pub claim_interface: extern "C" fn (ctx: *mut u8, intf: *mut usb_interface_descriptor_t,
        length: usize) -> sys::zx_status_t,
    pub cancel_all: extern "C" fn (ctx: *mut u8, ep_address: u8) -> sys::zx_status_t,
}

#[repr(C)]
pub struct usb_protocol_t {
    pub ops: *mut usb_protocol_ops_t,
    pub ctx: *mut u8,
}

impl Default for usb_protocol_t {
    fn default() -> Self {
        usb_protocol_t {
            ops: std::ptr::null_mut(),
            ctx: std::ptr::null_mut(),
        }
    }
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
