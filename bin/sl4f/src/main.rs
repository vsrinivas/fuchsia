// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![deny(warnings)]

extern crate failure;
extern crate fidl;
extern crate fidl_fuchsia_bluetooth as fidl_bt;
extern crate fidl_fuchsia_bluetooth_le as fidl_ble;
extern crate fuchsia_app as app;
extern crate fuchsia_bluetooth as bt;
extern crate fuchsia_zircon as zx;
extern crate futures;
extern crate parking_lot;
extern crate serde;
extern crate serde_json;
extern crate slab;

#[macro_use]
extern crate fuchsia_async as async;

#[macro_use]
extern crate fuchsia_syslog as syslog;

#[macro_use]
extern crate rouille;

#[macro_use]
extern crate serde_derive;

use failure::Error;
use futures::channel::mpsc;
use parking_lot::RwLock;
use std::sync::Arc;
use std::thread;

mod bluetooth;
mod server;

use server::sl4f::{serve, Sl4f};
use server::sl4f_executor::run_fidl_loop;

// Config, flexible for any ip/port combination
const SERVER_IP: &str = "0.0.0.0";
const SERVER_PORT: &str = "80";

// HTTP Server using Rouille
fn main() -> Result<(), Error> {
    syslog::init_with_tags(&["sl4f"]).expect("Can't init logger");
    fx_log_info!("Starting sl4f server");

    let address = format!("{}:{}", SERVER_IP, SERVER_PORT);
    fx_log_info!("Now listening on: {:?}", address);

    // Session storing all information about state
    // Current support is Bluetooth, add other stacks to sl4f.rs
    let sl4f_session: Arc<RwLock<Sl4f>> = Sl4f::new();

    // Create channel for communication: rouille sync side -> async exec side
    let (rouille_sender, async_receiver) = mpsc::unbounded();
    let sl4f_session_async = sl4f_session.clone();

    // Create the async execution thread
    thread::spawn(move || run_fidl_loop(sl4f_session_async, async_receiver));

    // Start listening on address
    rouille::start_server(address, move |request| {
        serve(&request, sl4f_session.clone(), rouille_sender.clone())
    });
}
