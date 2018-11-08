// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fdio::{fdio_sys, ioctl_raw, make_ioctl};
use std::fs::File;
use std::os::unix::io::AsRawFd;
use std::os::raw;
use fuchsia_zircon as zx;

pub fn connect_transport_device(device: &File) -> Result<zx::Channel, zx::Status> {
    let mut handle: zx::sys::zx_handle_t = zx::sys::ZX_HANDLE_INVALID;

    // This call is safe because the callee does not retain any data from the call, and the return
    // value ensures that the handle is a valid handle to a zx::channel.
    unsafe {
        match ioctl_raw(
            device.as_raw_fd(),
            IOCTL_QMI_GET_CHANNEL,
            ::std::ptr::null(),
            0,
            &mut handle as *mut _ as *mut raw::c_void,
            ::std::mem::size_of::<zx::sys::zx_handle_t>(),
        ) as i32
        {
            e if e < 0 => Err(zx::Status::from_raw(e)),
            e => Ok(e),
        }?;
        Ok(From::from(zx::Handle::from_raw(handle)))
    }
}

const IOCTL_QMI_GET_CHANNEL: raw::c_int = make_ioctl!(
    fdio_sys::IOCTL_KIND_GET_HANDLE,
    fdio_sys::IOCTL_FAMILY_QMI,
    0
);

pub fn set_network_status(device: &File, state: bool) -> Result<(), zx::Status> {
    let mut handle: zx::sys::zx_handle_t = zx::sys::ZX_HANDLE_INVALID;

    // This call is safe because the callee does not retain any data from the call, and the return
    // value ensures that the handle is a valid handle to a zx::channel.
    unsafe {
        match ioctl_raw(
            device.as_raw_fd(),
            IOCTL_QMI_SET_NETWORK,
            &state as *const _ as *const raw::c_void,
            ::std::mem::size_of::<bool>(),
            ::std::ptr::null_mut(),
            0,
        ) as i32
        {
            e if e < 0 => Err(zx::Status::from_raw(e)),
            e => Ok(e),
        }?;
        Ok(())
    }
}

const IOCTL_QMI_SET_NETWORK: raw::c_int = make_ioctl!(
    fdio_sys::IOCTL_KIND_DEFAULT,
    fdio_sys::IOCTL_FAMILY_QMI,
    1
);
