// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{Error, ResultExt};
use fidl::endpoints2::{ServiceMarker, RequestStream};
use fidl_fidl_examples_echo::{EchoMarker, EchoRequest, EchoRequestStream};
use fuchsia_app::server::ServicesServer;
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use futures::prelude::*;

use std::env;

fn spawn_echo_server(chan: fasync::Channel, quiet: bool) {
    fasync::spawn(EchoRequestStream::from_channel(chan)
        .map_ok(move |EchoRequest::EchoString { value, responder }| {
            if !quiet {
                println!("Received echo request for string {:?}", value);
            }
            responder.send(value.as_ref().map(|s| &**s))
               .map(move |_| if !quiet {
                   println!("echo response sent successfully");
               })
               .unwrap_or_else(|e| eprintln!("error sending response: {:?}", e))
        })
        .try_collect::<()>()
        .unwrap_or_else(|e| eprintln!("error running echo server: {:?}", e)))
}

fn main() -> Result<(), Error> {
    let mut executor = fasync::Executor::new().context("Error creating executor")?;
    let quiet = env::args().any(|arg| arg == "-q");

    let fut = ServicesServer::new()
                .add_service((EchoMarker::NAME, move |chan| spawn_echo_server(chan, quiet)))
                .start()
                .context("Error starting echo services server")?;

    executor.run_singlethreaded(fut).context("failed to execute echo future")?;
    Ok(())
}
