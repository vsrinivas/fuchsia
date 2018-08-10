// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

extern crate byteorder;
extern crate failure;
#[macro_use] extern crate fdio;
extern crate fidl;
extern crate fidl_fuchsia_wlan_tap as wlantap;
extern crate fuchsia_async as async;
extern crate fuchsia_zircon as zx;

use byteorder::{NativeEndian, WriteBytesExt};
use failure::Error;
use fdio::{fdio_sys, ioctl};
use fidl::encoding2::{Encoder};
use zx::AsHandleRef;
use std::fs::{File, OpenOptions};
use std::os::raw;
use std::mem;
use std::path::Path;

pub struct Wlantap {
    file: File,
}

impl Wlantap {
    pub fn open() -> Result<Wlantap, Error> {
        Ok(Wlantap{
            file: OpenOptions::new().read(true).write(true)
                    .open(Path::new("/dev/test/wlantapctl"))?,
        })
    }

    pub fn create_phy(&self, mut config: wlantap::WlantapPhyConfig)
        -> Result<wlantap::WlantapPhyProxy, Error>
    {
        let (encoded_config, handles) = (&mut vec![], &mut vec![]);
        Encoder::encode(encoded_config, handles, &mut config)?;

        let (local, remote) = zx::Channel::create()?;

        let mut ioctl_in = vec![];
        ioctl_in.write_u32::<NativeEndian>(remote.raw_handle())?;
        ioctl_in.append(encoded_config);

        // Safe because the length of the buffer is computed from the length of a vector,
        // and ioctl() doesn't retain the buffer.
        unsafe {
            ioctl(&self.file,
                  IOCTL_WLANTAP_CREATE_WLANPHY,
                  ioctl_in.as_ptr() as *const std::os::raw::c_void,
                  ioctl_in.len(),
                  std::ptr::null_mut(),
                  0)?;
        }
        // Release ownership of the remote handle
        mem::forget(remote);
        Ok(wlantap::WlantapPhyProxy::new(async::Channel::from_channel(local)?))
    }

}

const IOCTL_WLANTAP_CREATE_WLANPHY: raw::c_int = make_ioctl!(
    fdio_sys::IOCTL_KIND_SET_HANDLE,
    fdio_sys::IOCTL_FAMILY_WLANTAP,
    0
);
