// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro, futures_api)]

use failure::{Error, ResultExt};
use fidl::endpoints::{ServiceMarker, RequestStream};
use fidl_fidl_examples_echo::{EchoMarker, EchoRequest, EchoRequestStream};
use fuchsia_app::server::ServicesServer;
use fuchsia_async as fasync;
use futures::prelude::*;

use std::env;

fn spawn_echo_server(chan: fasync::Channel, quiet: bool) {
    fasync::spawn(async move {
        let mut stream = EchoRequestStream::from_channel(chan);
        while let Some(EchoRequest::EchoString { value, responder }) =
            await!(stream.try_next()).context("error running echo server")?
        {
            if !quiet {
                println!("Received echo request for string {:?}", value);
            }
            responder.send(value.as_ref().map(|s| &**s)).context("error sending response")?;
            if !quiet {
                println!("echo response sent successfully");
            }
        }
        Ok(())
    }.unwrap_or_else(|e: failure::Error| eprintln!("{:?}", e)));
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let quiet = env::args().any(|arg| arg == "-q");

    let fut = ServicesServer::new()
                .add_service((EchoMarker::NAME, move |chan| spawn_echo_server(chan, quiet)))
                .start()
                .context("Error starting echo services server")?;

    await!(fut).context("failed to execute echo future")?;
    Ok(())
}
