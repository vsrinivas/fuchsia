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
    pub fn ramdisk_get_path(client: *const ramdisk_client_t) -> *const raw::c_char;
    pub fn ramdisk_get_block_fd(client: *const ramdisk_client_t) -> raw::c_int;
    pub fn ramdisk_destroy(client: *const ramdisk_client_t) -> zx_status_t;
    pub fn wait_for_device(path: *const raw::c_char, timeout: u64) -> zx_status_t;
}
