// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

use failure::{Error, err_msg};
use fdio::{self, fdio_sys, ioctl, make_ioctl};
use std::ffi::CString;
use std::fs::File;
use std::os::raw;
use fidl_fuchsia_device_test::{DeviceSynchronousProxy, RootDeviceSynchronousProxy};

use super::open_rdwr;

pub fn create_test_device(test_path: &str, dev_name: &str) -> Result<String, Error> {
    let test_dev = open_rdwr(test_path)?;
    let channel = fdio::clone_channel(&test_dev)?;
    let mut interface = RootDeviceSynchronousProxy::new(channel);

    let (status, devpath) = interface.create_device(dev_name, fuchsia_zircon::Time::INFINITE)?;
    fuchsia_zircon::Status::ok(status)?;
    devpath.ok_or(err_msg("RootDevice.CreateDevice received no devpath?"))
}

pub fn bind_test_device(device: &File, driver_name: &str) -> Result<(), Error> {
    let devname = CString::new(driver_name)?;
    // This is safe because no memory ownership is transferred by this function and the length of
    // the input buffer is computed from the CString.
    unsafe {
        ioctl(device,
              IOCTL_DEVICE_BIND,
              devname.as_ptr() as *const raw::c_void,
              devname.as_bytes_with_nul().len(),
              ::std::ptr::null_mut() as *mut raw::c_void,
              0).map(|_| ()).map_err(|e| e.into())
    }
}

pub fn destroy_test_device(device: &File) -> Result<(), Error> {
    let channel = fdio::clone_channel(device)?;
    let mut interface = DeviceSynchronousProxy::new(channel);
    Ok(interface.destroy()?)
}

const IOCTL_DEVICE_BIND: raw::c_int = make_ioctl!(
    fdio_sys::IOCTL_KIND_DEFAULT,
    fdio_sys::IOCTL_FAMILY_DEVICE,
    0
);
