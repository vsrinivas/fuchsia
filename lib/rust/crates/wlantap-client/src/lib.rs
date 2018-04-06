// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(conservative_impl_trait)]

extern crate byteorder;
extern crate failure;
#[macro_use] extern crate fdio;
extern crate fidl;
extern crate fidl_wlan_device;
extern crate fidl_wlantap;
extern crate fuchsia_async as async;
extern crate fuchsia_zircon as zx;
extern crate futures;

use byteorder::{NativeEndian, WriteBytesExt};
use failure::Error;
use fdio::{fdio_sys, ioctl};
use fidl::encoding2::{Encoder};
use fidl_wlantap::WlantapPhyEvents;
use zx::AsHandleRef;
use futures::future::{self, FutureResult};
use futures::prelude::*;
use std::fs::{File, OpenOptions};
use std::os::raw;
use std::mem;
use std::path::Path;
use std::sync::Arc;

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

    pub fn create_phy<L>(&self, mut config: fidl_wlantap::WlantapPhyConfig, listener: L)
        -> Result<(Arc<fidl_wlantap::WlantapPhyProxy>,
                   impl Future<Item = (), Error = fidl::Error>), Error>
        where L: WlantapListener + Send + 'static
    {
        let (encoded_config, handles) = (&mut vec![], &mut vec![]);
        Encoder::encode(encoded_config, handles, &mut config)?;

        let (local, remote) = zx::Channel::create()?;
        let (local_ev, remote_ev) = zx::Channel::create()?;

        let mut ioctl_in = vec![];
        ioctl_in.write_u32::<NativeEndian>(remote.raw_handle())?;
        ioctl_in.write_u32::<NativeEndian>(remote_ev.raw_handle())?;
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
        // Release ownership of remote handles
        mem::forget(remote);
        mem::forget(remote_ev);
        let proxy = Arc::new(fidl_wlantap::WlantapPhyProxy::new(
            async::Channel::from_channel(local)?));
        let server = event_server(async::Channel::from_channel(local_ev)?, listener, &proxy);
        Ok((proxy, server))
    }

}

fn event_server<L: WlantapListener + Send>(channel: async::Channel,
                                           listener: L,
                                           proxy: &Arc<fidl_wlantap::WlantapPhyProxy>)
    -> impl Future<Item = (), Error = fidl::Error>
{
    fidl_wlantap::WlantapPhyEventsImpl {
        state: listener,
        tx: |listener, args, dummy| {
            println!("tx");
            future::ok(())
        },
        set_channel: {
            let proxy = proxy.clone();
            move |listener, args, dummy| {
                listener.set_channel(&proxy, args.wlanmac_id, args.chan);
                future::ok(())
            }
        },
        configure_bss: |listener, args, dummy| {
            println!("configure_bss");
            future::ok(())
        },
        set_key: |listener, args, dummy| {
            println!("set_key");
            future::ok(())
        }
    }
    .serve(channel)
}

pub trait WlantapListener {
    fn set_channel(&mut self,
                   proxy: &fidl_wlantap::WlantapPhyProxy,
                   wlanmac_id: u16,
                   channel: fidl_wlan_device::Channel);
}

const IOCTL_WLANTAP_CREATE_WLANPHY: raw::c_int = make_ioctl!(
    fdio_sys::IOCTL_KIND_SET_TWO_HANDLES,
    fdio_sys::IOCTL_FAMILY_WLANTAP,
    0
);
