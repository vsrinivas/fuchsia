// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fuchsia_async as fasync;
use futures::channel::mpsc;
use parking_lot::RwLock;
use std::sync::Arc;
use std::thread;

use fuchsia_syslog::macros::*;

use sl4f_lib::server::sl4f::{serve, Sl4f, Sl4fClients};
use sl4f_lib::server::sl4f_executor::run_fidl_loop;

// Config, flexible for any ip/port combination
const SERVER_IP: &str = "[::]";
const SERVER_PORT: &str = "80";

// HTTP Server using Rouille
fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["sl4f"]).expect("Can't init logger");
    fx_log_info!("Starting sl4f server");
    let mut executor = fasync::Executor::new().expect("Failed to create an executor!");

    let address = format!("{}:{}", SERVER_IP, SERVER_PORT);
    fx_log_info!("Now listening on: {:?}", address);

    // State for clients that utilize the /init endpoint
    let sl4f_clients = Arc::new(RwLock::new(Sl4fClients::new()));

    // State for facades
    let sl4f = Arc::new(Sl4f::new(Arc::clone(&sl4f_clients))?);

    // Create channel for communication: rouille sync side -> async exec side
    let (rouille_sender, async_receiver) = mpsc::unbounded();

    // Create thread for listening to test commands
    thread::spawn(move || {
        // Start listening on address
        rouille::start_server(address, move |request| {
            serve(&request, Arc::clone(&sl4f_clients), rouille_sender.clone())
        });
    });

    executor.run_singlethreaded(run_fidl_loop(sl4f, async_receiver));

    Ok(())
}
