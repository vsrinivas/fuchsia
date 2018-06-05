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
use futures::future;
use futures::prelude::*;
use fidl::endpoints2::{ServiceMarker, RequestStream};
use fidl_fidl_examples_echo::{EchoMarker, EchoRequest, EchoRequestStream};

fn spawn_echo_server(chan: async::Channel) {
    async::spawn(EchoRequestStream::from_channel(chan)
        .for_each(|EchoRequest::EchoString { value, responder }| {
            println!("Received echo request for string {:?}", value);
            responder.send(value.as_ref().map(|s| &**s))
               .into_future()
               .map(|_| println!("echo response sent successfully"))
               .recover(|e| eprintln!("error sending response: {:?}", e))
        })
        .map(|_| ())
        .recover(|e| eprintln!("error running echo server: {:?}", e)))
}

fn main() -> Result<(), Error> {
    let mut executor = async::Executor::new().context("Error creating executor")?;

    let fut = ServicesServer::new()
                .add_service((EchoMarker::NAME, |chan| spawn_echo_server(chan)))
                .start()
                .context("Error starting echo services server")?;

    executor.run_singlethreaded(fut).context("failed to execute echo future")?;
    Ok(())
}
