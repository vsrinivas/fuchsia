// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]
#![feature(async_await, await_macro, futures_api)]

use std::sync::Arc;

use failure::{Error, ResultExt};
use fidl::endpoints::{RequestStream, ServiceMarker};
use fidl_fuchsia_guest::{WaylandDispatcherMarker, WaylandDispatcherRequest,
                         WaylandDispatcherRequestStream};
use fuchsia_app::server::{ServiceFactory, ServicesServer};
use fuchsia_async as fasync;
use fuchsia_wayland_core::{self as wl, Interface};
use futures::prelude::*;
use parking_lot::Mutex;
use wayland::WlCompositor;

mod client;
use crate::client::*;
mod compositor;

/// The main FIDL server that listens for incomming client connection
/// requests.
struct WaylandDispatcher {
    /// The display handles the creation of new clients. Must be
    /// Arc/Mutex since this is shared with the future run on the executor.
    display: Arc<Mutex<Display>>,
}

impl WaylandDispatcher {
    pub fn new() -> Self {
        let registry = wl::RegistryBuilder::new()
            // TODO(tjdetwiler): This is just a placeholder to verify that we
            // can see globals in response to binding to 'wl_registry'. This
            // can be removed once we have some real interfaces.
            .add_global((WlCompositor::NAME, move || -> Box<wl::MessageReceiver> {
                Box::new(wl::RequestDispatcher::new(compositor::Compositor::new()))
            })).build();
        WaylandDispatcher {
            display: Arc::new(Mutex::new(Display::new(registry))),
        }
    }
}

/// A |ServiceFactory| for |WaylandDispatcher| exposes a public FIDL service
/// that will listen for |OnNewConnection| requests and forward those requests
/// to the |Display|, which will create and serve wayland messages on the
/// provided channels.
impl ServiceFactory for WaylandDispatcher {
    fn service_name(&self) -> &str {
        WaylandDispatcherMarker::NAME
    }

    fn spawn_service(&mut self, chan: fasync::Channel) {
        let display = self.display.clone();
        fasync::spawn(
            async move {
                let mut stream = WaylandDispatcherRequestStream::from_channel(chan);
                while let Some(WaylandDispatcherRequest::OnNewConnection { channel, .. }) =
                    await!(stream.try_next()).context("error running wayland dispatcher")?
                {
                    display
                        .lock()
                        .spawn_new_client(fasync::Channel::from_channel(channel).unwrap());
                }
                Ok(())
            }
                .unwrap_or_else(|e: failure::Error| println!("{:?}", e)),
        );
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
