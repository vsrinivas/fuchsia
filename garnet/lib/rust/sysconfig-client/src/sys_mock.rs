// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::sys::{zx_handle_t, zx_off_t, zx_status_t, SysconfigPartition};
use fuchsia_zircon::{self as zx, Vmo};
use std::cell::RefCell;

thread_local!(pub static DATA: RefCell<Vec<u8>> = RefCell::new(vec![]));

pub fn get_data() -> Vec<u8> {
    DATA.with(|data| data.borrow().clone())
}

pub fn set_data(new_data: Vec<u8>) {
    DATA.with(|data| *data.borrow_mut() = new_data);
}

#[allow(bad_style)]
#[derive(Debug, Clone)]
pub struct sysconfig_sync_client_t {
    pub devfs_root: ::std::os::raw::c_int,
    pub partition: SysconfigPartition,
    pub partition_size: usize,
    pub freed: bool,
    pub vmo_offset: zx_off_t,
}

impl Drop for sysconfig_sync_client_t {
    fn drop(&mut self) {
        assert!(self.freed);
    }
}

pub unsafe fn sysconfig_sync_client_create(
    devfs_root: ::std::os::raw::c_int,
    out_client: *mut *mut sysconfig_sync_client_t,
) -> zx_status_t {
    let client = sysconfig_sync_client_t {
        devfs_root,
        partition: SysconfigPartition::Config,
        partition_size: 4096,
        freed: false,
        vmo_offset: 0,
    };
    *out_client = Box::into_raw(Box::new(client));
    zx::Status::OK.into_raw()
}

pub unsafe fn sysconfig_sync_client_free(client: *mut sysconfig_sync_client_t) {
    assert!(!(*client).freed);
    (*client).freed = true;
    // Free the client by converting it back into a Box.
    Box::from_raw(client);
}

pub unsafe fn sysconfig_write_partition(
    client: *mut sysconfig_sync_client_t,
    partition: SysconfigPartition,
    vmo: zx_handle_t,
    vmo_offset: zx_off_t,
) -> zx_status_t {
    (*client).partition = partition;
    (*client).vmo_offset = vmo_offset;
    set_data(vmo_handle_to_vec(vmo));
    zx::Status::OK.into_raw()
}

pub unsafe fn sysconfig_read_partition(
    client: *mut sysconfig_sync_client_t,
    partition: SysconfigPartition,
    vmo: zx_handle_t,
    vmo_offset: zx_off_t,
) -> zx_status_t {
    (*client).partition = partition;
    (*client).vmo_offset = vmo_offset;
    let vmo = Vmo::from(zx::Handle::from_raw(vmo));
    vmo.write(&get_data(), 0).expect("write vmo");
    // Don't destruct the vmo so that the caller can read from it.
    std::mem::forget(vmo);
    zx::Status::OK.into_raw()
}

pub unsafe fn sysconfig_get_partition_size(
    client: *mut sysconfig_sync_client_t,
    partition: SysconfigPartition,
    out: *mut usize,
) -> zx_status_t {
    (*client).partition = partition;
    *out = (*client).partition_size;
    zx::Status::OK.into_raw()
}

unsafe fn vmo_handle_to_vec(vmo: zx_handle_t) -> Vec<u8> {
    let vmo = Vmo::from(zx::Handle::from_raw(vmo));
    let size = vmo.get_size().unwrap();
    let mut result = vec![1u8; size as usize];
    vmo.read(result.as_mut_slice(), 0).expect("read vmo");
    std::mem::forget(vmo);
    result
}
