// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]
#![allow(missing_docs)]

use failure::Error;
use rand::{self, Rng};
use std;
use std::ffi::{CString, OsStr, OsString};
use std::fs::{read_dir, File, OpenOptions};
use std::mem;
use std::os::raw;
use std::os::unix::ffi::OsStrExt;
use std::path::{Path, PathBuf};

use fdio::{fdio_sys, ioctl};
use zircon::{self, Handle};

pub const DEV_TEST: &str = "/dev/test/test";
pub const BTHCI_DRIVER_NAME: &str = "/system/driver/bthci-fake.so";

// Returns the name of the fake device and a File representing the device on success.
pub fn create_and_bind_device() -> Result<(File, String), Error> {
    let mut rng = rand::thread_rng();
    let id = format!("bt-hci-{}", rng.gen::<u8>());
    let devpath = create_fake_device(DEV_TEST, id.as_str())?;

    let mut retry = 0;
    let mut dev = None;
    {
        while retry < 100 {
            retry += 1;
            if let Ok(d) = open_rdwr(&devpath) {
                dev = Some(d);
                break;
            }
        }
    }
    let dev = dev.ok_or_else(|| format_err!("could not open {:?}", devpath))?;
    bind_fake_device(&dev)?;
    Ok((dev, id))
}

pub fn list_host_devices() -> Vec<PathBuf> {
    let paths = read_dir("/dev/class/bt-host/").unwrap();
    paths
        .filter_map(|entry| entry.ok().and_then(|e| Some(e.path())))
        .collect::<Vec<PathBuf>>()
}

pub fn create_fake_device(test_path: &str, dev_name: &str) -> Result<OsString, Error> {
    let test_dev = open_rdwr(test_path)?;

    let devname = CString::new(dev_name)?;

    let mut devpath = [0; 1024];
    // This is safe because the length of the output buffer is computed from the vector, and the
    // callee does not retain the pointers.
    let pathlen = unsafe {
        ioctl(
            &test_dev,
            IOCTL_TEST_CREATE_DEVICE,
            devname.as_ptr() as *const raw::c_void,
            devname.as_bytes_with_nul().len(),
            devpath.as_mut_ptr() as *mut raw::c_void,
            devpath.len(),
        )?
    };
    // Need to return an OsString with length equal to the return value of the ioctl, rather than
    // the full length of the buffer.
    let mut ospath = OsString::new();
    ospath.push(OsStr::from_bytes(&devpath[0..(pathlen - 1) as usize]));
    Ok(ospath)
}

pub fn bind_fake_device(device: &File) -> Result<(), Error> {
    let devname = CString::new(BTHCI_DRIVER_NAME)?;
    // This is safe because no memory ownership is transferred by this function and the length of
    // the input buffer is computed from the CString.
    unsafe {
        ioctl(
            device,
            IOCTL_DEVICE_BIND,
            devname.as_ptr() as *const raw::c_void,
            devname.as_bytes_with_nul().len(),
            ::std::ptr::null_mut() as *mut raw::c_void,
            0,
        ).map(|_| ())
            .map_err(|e| e.into())
    }
}

pub fn destroy_device(device: &File) -> Result<(), Error> {
    // This is safe because no memory ownership is transferred by this function.
    unsafe {
        ioctl(
            device,
            IOCTL_TEST_DESTROY_DEVICE,
            ::std::ptr::null() as *const raw::c_void,
            0,
            ::std::ptr::null_mut() as *mut raw::c_void,
            0,
        ).map(|_| ())
            .map_err(|e| e.into())
    }
}

// Ioctl called used to get the driver name of the bluetooth hci device. This is used to ensure
// the driver is the right driver to be bound to the device.
// TODO(bwb): move out to a generic crate
pub fn get_device_driver_name(device: &File) -> Result<OsString, Error> {
    let mut driver_name = [0; 1024];

    // This is safe because the length of the output buffer is computed from the vector, and the
    // callee does not retain the pointers.
    let name_size = unsafe {
        ioctl(
            device,
            IOCTL_DEVICE_GET_DRIVER_NAME,
            ::std::ptr::null_mut() as *mut raw::c_void,
            0,
            driver_name.as_mut_ptr() as *mut raw::c_void,
            driver_name.len(),
        )?
    };

    // Need to return an OsString with length equal to the return value of the ioctl, rather than
    // the full length of the buffer.
    let mut ospath = OsString::new();
    ospath.push(OsStr::from_bytes(&driver_name[0..name_size as usize]));
    Ok(ospath)
}

// Ioctl definitions for the above calls.
// TODO(bwb): move out to a generic crate
pub fn open_snoop_channel(device: &File) -> Result<zircon::Handle, Error> {
    let mut handle = zircon::sys::ZX_HANDLE_INVALID;
    unsafe {
        ioctl(
            device,
            IOCTL_BT_HCI_GET_SNOOP_CHANNEL,
            ::std::ptr::null_mut() as *mut raw::c_void,
            0,
            &mut handle as *mut _ as *mut std::os::raw::c_void,
            mem::size_of::<zircon::sys::zx_handle_t>(),
        ).map(|_| Handle::from_raw(handle))
            .map_err(|e| e.into())
    }
}

fn open_rdwr<P: AsRef<Path>>(path: P) -> Result<File, Error> {
    OpenOptions::new()
        .read(true)
        .write(true)
        .open(path)
        .map_err(|e| e.into())
}

// Ioctl definitions for the above calls.
// TODO(bwb): move out to a generic crate
const IOCTL_TEST_CREATE_DEVICE: raw::c_int =
    make_ioctl!(fdio_sys::IOCTL_KIND_DEFAULT, fdio_sys::IOCTL_FAMILY_TEST, 0);

const IOCTL_TEST_DESTROY_DEVICE: raw::c_int =
    make_ioctl!(fdio_sys::IOCTL_KIND_DEFAULT, fdio_sys::IOCTL_FAMILY_TEST, 1);

const IOCTL_DEVICE_BIND: raw::c_int = make_ioctl!(
    fdio_sys::IOCTL_KIND_DEFAULT,
    fdio_sys::IOCTL_FAMILY_DEVICE,
    0
);

const IOCTL_DEVICE_GET_DRIVER_NAME: raw::c_int = make_ioctl!(
    fdio_sys::IOCTL_KIND_DEFAULT,
    fdio_sys::IOCTL_FAMILY_DEVICE,
    2
);

const IOCTL_BT_HCI_GET_SNOOP_CHANNEL: raw::c_int = make_ioctl!(
    fdio_sys::IOCTL_KIND_GET_HANDLE,
    fdio_sys::IOCTL_FAMILY_BT_HCI,
    2
);
