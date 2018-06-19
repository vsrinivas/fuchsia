// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::Error;
use fdio::{fdio_sys, ioctl};
use std;
use std::fs::File;
use std::mem;
use std::os::raw;
use zircon::{self, Handle};

/// Opens a Host Fidl interface on a bt-host device using an Ioctl
pub fn open_host_channel(device: &File) -> Result<zircon::Handle, Error> {
    let mut handle = zircon::sys::ZX_HANDLE_INVALID;
    unsafe {
        ioctl(
            device,
            IOCTL_BT_HOST_OPEN_CHANNEL,
            ::std::ptr::null_mut() as *mut raw::c_void,
            0,
            &mut handle as *mut _ as *mut std::os::raw::c_void,
            mem::size_of::<zircon::sys::zx_handle_t>(),
        ).map(|_| Handle::from_raw(handle))
            .map_err(|e| e.into())
    }
}

const IOCTL_BT_HOST_OPEN_CHANNEL: raw::c_int = make_ioctl!(
    fdio_sys::IOCTL_KIND_GET_HANDLE,
    fdio_sys::IOCTL_FAMILY_BT_HOST,
    0
);
