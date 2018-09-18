// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]
#![feature(async_await, await_macro, futures_api)]

use std::sync::Arc;

use failure::{Error, ResultExt};
use fuchsia_app::server::{ServiceFactory, ServicesServer};
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use futures::prelude::*;
use fidl::endpoints::{RequestStream, ServiceMarker};
use fidl_fuchsia_guest::{WaylandDispatcherMarker, WaylandDispatcherRequest,
                         WaylandDispatcherRequestStream};
use parking_lot::Mutex;

/// The main FIDL server that listens for incomming client connection
/// requests.
struct WaylandDispatcher {
    /// The client factory handles the creation of new clients. Must be
    /// Arc/Mutex since this is shared with the future run on the executor.
    client_factory: Arc<Mutex<ClientFactory>>,
}

impl WaylandDispatcher {
    pub fn new() -> Self {
        WaylandDispatcher {
            client_factory: Arc::new(Mutex::new(ClientFactory::new())),
        }
    }
}

/// A |ServiceFactory| for |WaylandDispatcher| exposes a public FIDL service
/// that will listen for |OnNewConnection| requests and forward those requests
/// to the |ClientFactory|, which will create and serve wayland messages on the
/// provided channels.
impl ServiceFactory for WaylandDispatcher {
    fn service_name(&self) -> &str {
        WaylandDispatcherMarker::NAME
    }

    fn spawn_service(&mut self, chan: fasync::Channel) {
        let client_factory = self.client_factory.clone();
        fasync::spawn(
            async move {
                let mut stream = WaylandDispatcherRequestStream::from_channel(chan);
                while let Some(WaylandDispatcherRequest::OnNewConnection { channel, .. }) =
                    await!(stream.try_next()).context("error running wayland dispatcher")?
                {
                    client_factory.lock().add_client(channel);
                }
                Ok(())
            }
                .unwrap_or_else(|e: failure::Error| println!("{:?}", e)),
        );
    }
}

/// Creates a new client connection and serves a wayland server on the provided
/// channel.
///
/// TODO(tjdetwiler): This is just a skeleton.
struct ClientFactory;

impl ClientFactory {
    pub fn new() -> Self {
        ClientFactory
    }

    fn add_client(&mut self, chan: zx::Channel) {
        // TODO: create new wayland client and start polling |chan| for
        // messages. This just sends a single byte over the channel so an
        // intial test can verify the current plumbing is working.
        let mut handles = Vec::new();
        chan.write(&[1], &mut handles).unwrap()
    }
}

fn main() -> Result<(), Error> {
    let mut exec = fasync::Executor::new()?;
    let fut = ServicesServer::new()
        .add_service(WaylandDispatcher::new())
        .start()
        .context("Error starting wayland bridge services server")?;
    exec.run_singlethreaded(fut)
        .context("Failed to execute wayland bridge future")?;
    Ok(())
}
