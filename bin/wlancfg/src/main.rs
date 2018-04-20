// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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
use wlan_service::DeviceServiceMarker;

fn main() -> Result<(), Error> {
    let cfg = Config::load_from_file()?;

    let mut executor = async::Executor::new().context("error creating event loop")?;
    let wlan_svc = app::client::connect_to_service::<DeviceServiceMarker>()
        .context("failed to connect to device service")?;

    let event_stream = wlan_svc.take_event_stream();
    let fut = wlan_svc
        .register_listener()
        .map_err(|e| Error::from(e.context("failed to register listener")))
        .and_then(|status| {
            zx::Status::ok(status)
                .map_err(|e| Error::from(e.context("failed to register listener")))
                .into_future()
        })
        .and_then(|_| {
            let listener = device::Listener::new(wlan_svc, cfg);
            event_stream
                .for_each(move |evt| device::handle_event(&listener, evt))
                .err_into()
        });

    executor
        .run_singlethreaded(fut)
        .map_err(|e| Error::from(e))
        .map(|_| ())
}
