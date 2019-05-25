// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

use failure;
use fdio::{self, fdio_sys, ioctl_raw, make_ioctl};
use fidl::endpoints::ServerEnd;
use fidl_fuchsia_wlan_device as wlan;
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use std::fs::File;
use std::os::raw;
use std::os::unix::io::AsRawFd;

pub fn connect_wlanphy_device(device: &File) -> Result<wlan::PhyProxy, failure::Error> {
    let (local, remote) = zx::Channel::create()?;

    let connector_channel = fdio::clone_channel(device)?;
    let connector = wlan::ConnectorProxy::new(fasync::Channel::from_channel(connector_channel)?);
    connector.connect(ServerEnd::new(remote))?;

    Ok(wlan::PhyProxy::new(fasync::Channel::from_channel(local)?))
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

const IOCTL_WLAN_GET_CHANNEL: raw::c_int =
    make_ioctl(fdio_sys::IOCTL_KIND_GET_HANDLE, fdio_sys::IOCTL_FAMILY_WLAN, 0);
