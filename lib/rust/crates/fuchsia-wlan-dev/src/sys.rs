// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

use fdio::{fdio_sys, ioctl_raw};
use std::fs::File;
use std::os::raw;
use std::os::unix::io::AsRawFd;
use zx::{self, HandleBased};

pub fn connect_wlanphy_device(device: &File) -> Result<zx::Channel, zx::Status> {
    let (local, remote) = zx::Channel::create()?;
    let hnd = remote.into_raw();

    // This call is safe because the handle is never used after this call, and the output buffer is
    // null.
    unsafe {
        match ioctl_raw(
            device.as_raw_fd(),
            IOCTL_WLANPHY_CONNECT,
            &hnd as *const _ as *const raw::c_void,
            ::std::mem::size_of::<zx::sys::zx_handle_t>(),
            ::std::ptr::null_mut(),
            0,
        ) as i32
        {
            e if e < 0 => Err(zx::Status::from_raw(e)),
            e => Ok(e),
        }?;
    }
    Ok(local)
}

pub fn connect_wlaniface_device(device: &File) -> Result<zx::Channel, zx::Status> {
    let mut hnd: zx::sys::zx_handle_t = zx::sys::ZX_HANDLE_INVALID;

    // This call is safe because the callee does not retain any data from the call, and the return
    // value ensures that the handle is a valid handle to a zx::channel.
    unsafe {
        match ioctl_raw(
            device.as_raw_fd(),
            IOCTL_WLAN_GET_CHANNEL,
            ::std::ptr::null(),
            0,
            &mut hnd as *mut _ as *mut raw::c_void,
            ::std::mem::size_of::<zx::sys::zx_handle_t>(),
        ) as i32
        {
            e if e < 0 => Err(zx::Status::from_raw(e)),
            e => Ok(e),
        }?;
        Ok(From::from(zx::Handle::from_raw(hnd)))
    }
}

const IOCTL_WLANPHY_CONNECT: raw::c_int = make_ioctl!(
    fdio_sys::IOCTL_KIND_SET_HANDLE,
    fdio_sys::IOCTL_FAMILY_WLANPHY,
    0
);

const IOCTL_WLAN_GET_CHANNEL: raw::c_int = make_ioctl!(
    fdio_sys::IOCTL_KIND_GET_HANDLE,
    fdio_sys::IOCTL_FAMILY_WLAN,
    0
);
