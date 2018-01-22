// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

use failure::Error;
use fdio::{self, fdio_sys};
use std::ffi::{CString, OsStr, OsString};
use std::fs::File;
use std::os::raw;
use std::os::unix::ffi::OsStrExt;
use std::os::unix::io::AsRawFd;
use zircon;

use super::open_rdwr;

// TODO(tkilbourn): consider whether this belongs in the fdio crate
unsafe fn ioctl(dev: &File, op: raw::c_int, in_buf: *const raw::c_void, in_len: usize,
         out_buf: *mut raw::c_void, out_len: usize) -> Result<i32, Error> {
   match fdio::ioctl(dev.as_raw_fd(), op, in_buf, in_len, out_buf, out_len) as i32 {
     e if e < 0 => Err(zircon::Status::from_raw(e).into()),
     e => Ok(e),
   }
}

pub fn create_test_device(test_path: &str, dev_name: &str) -> Result<OsString, Error> {
    let test_dev = open_rdwr(test_path)?;

    let devname = CString::new(dev_name)?;
    // Buffer to hold the device path output from the ioctl.
    let mut devpath = vec![0; 1024];
    // This is safe because the length of the output buffer is computed from the vector, and the
    // callee does not retain the pointers.
    let pathlen = unsafe {
        ioctl(&test_dev,
              IOCTL_TEST_CREATE_DEVICE,
              devname.as_ptr() as *const raw::c_void,
              devname.as_bytes_with_nul().len(),
              devpath.as_mut_ptr() as *mut raw::c_void,
              devpath.len())?
    };
    // Need to return an OsString with length equal to the return value of the ioctl, rather than
    // the full length of the buffer.
    let mut ospath = OsString::new();
    ospath.push(OsStr::from_bytes(&devpath[0..(pathlen - 1) as usize]));
    Ok(ospath)
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
              0).map(|_| ())
    }
}

pub fn destroy_test_device(device: &File) -> Result<(), Error> {
    // This is safe because no memory ownership is transferred by this function.
    unsafe {
        ioctl(device,
              IOCTL_TEST_DESTROY_DEVICE,
              ::std::ptr::null() as *const raw::c_void,
              0,
              ::std::ptr::null_mut() as *mut raw::c_void,
              0).map(|_| ())
    }
}

const IOCTL_TEST_CREATE_DEVICE: raw::c_int = make_ioctl!(
    fdio_sys::IOCTL_KIND_DEFAULT,
    fdio_sys::IOCTL_FAMILY_TEST,
    0
);

const IOCTL_TEST_DESTROY_DEVICE: raw::c_int = make_ioctl!(
    fdio_sys::IOCTL_KIND_DEFAULT,
    fdio_sys::IOCTL_FAMILY_TEST,
    1
);

const IOCTL_DEVICE_BIND: raw::c_int = make_ioctl!(
    fdio_sys::IOCTL_KIND_DEFAULT,
    fdio_sys::IOCTL_FAMILY_DEVICE,
    0
);
