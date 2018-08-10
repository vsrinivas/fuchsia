// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern crate fidl;
extern crate failure;
extern crate fuchsia_app as component;
extern crate fuchsia_async as async;
extern crate fuchsia_zircon as zx;
extern crate futures;
extern crate fidl_fidl_examples_echo;

use component::server::ServicesServer;
use failure::{Error, ResultExt};
use futures::prelude::*;
use fidl::endpoints2::{ServiceMarker, RequestStream};
use fidl_fidl_examples_echo::{EchoMarker, EchoRequest, EchoRequestStream};
use std::env;

fn spawn_echo_server(chan: async::Channel, quiet: bool) {
    async::spawn(EchoRequestStream::from_channel(chan)
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
    let mut executor = async::Executor::new().context("Error creating executor")?;
    let quiet = env::args().any(|arg| arg == "-q");

    let fut = ServicesServer::new()
                .add_service((EchoMarker::NAME, move |chan| spawn_echo_server(chan, quiet)))
                .start()
                .context("Error starting echo services server")?;

    executor.run_singlethreaded(fut).context("failed to execute echo future")?;
    Ok(())
}
