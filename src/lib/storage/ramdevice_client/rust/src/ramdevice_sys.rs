// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {fuchsia_zircon::sys::*, std::os::raw};

#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct ramdisk_client {
    _unused: [u8; 0],
}
pub type ramdisk_client_t = ramdisk_client;

#[link(name = "ramdevice-client")]
extern "C" {
    pub fn ramdisk_create(
        blk_size: u64,
        blk_count: u64,
        out: *mut *mut ramdisk_client_t,
    ) -> zx_status_t;
    pub fn ramdisk_create_at(
        dev_root_fd: raw::c_int,
        blk_size: u64,
        blk_count: u64,
        out: *mut *mut ramdisk_client_t,
    ) -> zx_status_t;
    pub fn ramdisk_create_with_guid(
        blk_size: u64,
        blk_count: u64,
        type_guid: *const u8,
        guid_len: usize,
        out: *mut *mut ramdisk_client_t,
    ) -> zx_status_t;
    pub fn ramdisk_create_at_with_guid(
        dev_root_fd: raw::c_int,
        blk_size: u64,
        blk_count: u64,
        type_guid: *const u8,
        guid_len: usize,
        out: *mut *mut ramdisk_client_t,
    ) -> zx_status_t;
    pub fn ramdisk_create_from_vmo(
        raw_vmo: zx_handle_t,
        out: *mut *mut ramdisk_client_t,
    ) -> zx_status_t;
    pub fn ramdisk_create_at_from_vmo(
        dev_root_fd: raw::c_int,
        raw_vmo: zx_handle_t,
        out: *mut *mut ramdisk_client_t,
    ) -> zx_status_t;
    pub fn ramdisk_create_from_vmo_with_block_size(
        raw_vmo: zx_handle_t,
        blk_size: u64,
        out: *mut *mut ramdisk_client_t,
    ) -> zx_status_t;
    pub fn ramdisk_create_at_from_vmo_with_block_size(
        dev_root_fd: raw::c_int,
        raw_vmo: zx_handle_t,
        blk_size: u64,
        out: *mut *mut ramdisk_client_t,
    ) -> zx_status_t;
    pub fn ramdisk_get_path(client: *const ramdisk_client_t) -> *const raw::c_char;
    pub fn ramdisk_get_block_fd(client: *const ramdisk_client_t) -> raw::c_int;
    pub fn ramdisk_destroy(client: *const ramdisk_client_t) -> zx_status_t;
    pub fn wait_for_device(path: *const raw::c_char, timeout: u64) -> zx_status_t;
}
