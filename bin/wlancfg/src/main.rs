// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(stable_features)]
#![feature(conservative_impl_trait)]
#![deny(warnings)]

extern crate failure;
extern crate fidl;
extern crate fidl_wlan_device as wlan;
extern crate fidl_wlan_device_service as wlan_service;
extern crate fuchsia_app as app;
extern crate fuchsia_async as async;
extern crate fuchsia_zircon as zx;
extern crate futures;
extern crate serde;
#[macro_use]
extern crate serde_derive;
extern crate serde_json;

mod config;
mod device;

use config::Config;
use failure::{Error, Fail, ResultExt};
use futures::prelude::*;
use wlan_service::{DeviceListener, DeviceListenerMarker, DeviceServiceMarker};

fn main() {
    if let Err(e) = main_res() {
        println!("Error: {:?}", e);
    }
    println!("wlancfg: Exiting");
}

fn main_res() -> Result<(), Error> {
    let cfg = Config::load_from_file()?;

    let mut executor = async::Executor::new().context("error creating event loop")?;
    let wlan_svc = app::client::connect_to_service::<DeviceServiceMarker>()
        .context("failed to connect to device service")?;

    let (remote, local) = zx::Channel::create().context("failed to create zx channel")?;
    let local = async::Channel::from_channel(local).context("failed to make async channel")?;

    let mut remote_ptr = fidl::endpoints2::ClientEnd::<DeviceListenerMarker>::new(remote);
    let fut = wlan_svc
        .register_listener(&mut remote_ptr)
        .map_err(|e| Error::from(e).context("failed to register listener"))
        .and_then(|status| {
            zx::Status::ok(status)
                .map_err(|e| Error::from(e).context("failed to register listener"))
                .into_future()
        })
        .and_then(|_| {
            device::device_listener(device::Listener::new(wlan_svc, cfg))
                .serve(local)
                .map_err(|e| e.context("Device listener failed"))
        });

    executor.run_singlethreaded(fut).map_err(|e| e.into())
}
