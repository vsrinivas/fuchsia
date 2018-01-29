// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

use failure::{Error, ResultExt};
use fdio::{self, fdio_sys};
use fidl::{self, DecodablePtr, EncodablePtr};
use std::fs::File;
use std::os::raw;
use std::os::unix::io::AsRawFd;
use wlan;
use zircon;

unsafe fn ioctl(dev: &File, op: raw::c_int, in_buf: &[u8], out_buf: &mut [u8])
    -> Result<i32, zircon::Status> {

    let in_ptr = if in_buf.len() > 0 {
        in_buf.as_ptr() as *const raw::c_void
    } else {
        ::std::ptr::null()
    };
    let out_ptr = if out_buf.len() > 0 {
        out_buf.as_mut_ptr() as *mut raw::c_void
    } else {
        ::std::ptr::null_mut()
    };
    match fdio::ioctl(dev.as_raw_fd(), op, in_ptr, in_buf.len(), out_ptr, out_buf.len()) as i32 {
        e if e < 0 => Err(zircon::Status::from_raw(e)),
        e => Ok(e),
    }
}

pub fn query_wlanphy_device(device: &File) -> Result<wlan::WlanInfo, Error> {
    let mut info = vec![0; 2048];
    // This call is safe because the length of the output buffer is passed based on the length of
    // the |info| vector. The callee will not retain any pointers from this call.
    unsafe {
        ioctl(device, IOCTL_WLANPHY_QUERY, &[], &mut info).context("failure in ioctl wlan query")?;
    }
    let mut buf = fidl::DecodeBuf::new_with(zircon::MessageBuf::new_with(info, vec![]));
    wlan::WlanInfo::decode_obj(&mut buf, 0).map_err(|e| e.into())
}

pub fn create_wlaniface(device: &File, role: wlan::MacRole) -> Result<wlan::WlanIface, Error> {
    let mut buf = fidl::EncodeBuf::new();
    let req = wlan::CreateIfaceRequest{ role: role };
    req.encode_obj(&mut buf);
    let mut info = vec![0; 1024];
    // This call is safe because the length of the buffers are passed based on the length of
    // the |buf| and |info| vectors. The callee will not retain any pointers from this call.
    unsafe {
        ioctl(device, IOCTL_WLANPHY_CREATE_IFACE,
              buf.get_bytes(), &mut info).context("failure in ioctl wlan create iface")?;
    }
    let mut recvbuf = fidl::DecodeBuf::new_with(zircon::MessageBuf::new_with(info, vec![]));
    wlan::WlanIface::decode_obj(&mut recvbuf, 0).map_err(|e| e.into())
}

pub fn destroy_wlaniface(device: &File, id: u16) -> Result<(), zircon::Status> {
    let mut buf = fidl::EncodeBuf::new();
    let req = wlan::DestroyIfaceRequest{ id: id };
    req.encode_obj(&mut buf);
    // This call is safe because the length of the buffer is passed based on the length of
    // the |buf| vector. The callee will not retain any pointers from this call.
    unsafe {
        ioctl(device, IOCTL_WLANPHY_DESTROY_IFACE, buf.get_bytes(), &mut []).map(|_| ())
    }
}

const IOCTL_WLANPHY_QUERY: raw::c_int = make_ioctl!(
    fdio_sys::IOCTL_KIND_DEFAULT,
    fdio_sys::IOCTL_FAMILY_WLANPHY,
    0
);

const IOCTL_WLANPHY_CREATE_IFACE: raw::c_int = make_ioctl!(
    fdio_sys::IOCTL_KIND_DEFAULT,
    fdio_sys::IOCTL_FAMILY_WLANPHY,
    1
);

const IOCTL_WLANPHY_DESTROY_IFACE: raw::c_int = make_ioctl!(
    fdio_sys::IOCTL_KIND_DEFAULT,
    fdio_sys::IOCTL_FAMILY_WLANPHY,
    2
);
