// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]
#![feature(async_await, await_macro, futures_api)]
#![recursion_limit = "128"]

use std::sync::Arc;

use failure::{Error, ResultExt};
use fidl::endpoints::{RequestStream, DiscoverableService};
use fidl_fuchsia_guest::{
    WaylandDispatcherMarker, WaylandDispatcherRequest, WaylandDispatcherRequestStream,
};
use fuchsia_app::server::{ServiceFactory, ServicesServer};
use fuchsia_async as fasync;
use futures::prelude::*;
use parking_lot::Mutex;
use wayland::{WlCompositor, WlDataDeviceManager, WlOutput, WlSeat, WlShm, WlSubcompositor};

mod client;
mod object;
use crate::object::*;
mod registry;
use crate::registry::*;
mod compositor;
use crate::compositor::*;
mod data_device_manager;
use crate::data_device_manager::*;
mod display;
use crate::display::*;
mod frontend;
use crate::frontend::*;
mod output;
use crate::output::*;
mod seat;
use crate::seat::*;
mod shm;
use crate::shm::*;
mod subcompositor;
use crate::subcompositor::*;

#[cfg(test)]
mod test_protocol;

/// The main FIDL server that listens for incoming client connection
/// requests.
struct WaylandDispatcher {
    /// The display handles the creation of new clients. Must be
    /// Arc/Mutex since this is shared with the future run on the executor.
    display: Arc<Mutex<Display>>,
}

impl WaylandDispatcher {
    pub fn new() -> Result<Self, Error> {
        let view_sink = TileViewSink::new()?;
        let scenic = view_sink.scenic_session();
        let mut registry = RegistryBuilder::new();
        {
            let scenic = scenic.clone();
            registry.add_global(WlCompositor, move |_, _| {
                Ok(Box::new(RequestDispatcher::new(Compositor::new(scenic.clone()))))
            });
        }
        {
            registry.add_global(WlSubcompositor, move |_, _| {
                Ok(Box::new(RequestDispatcher::new(Subcompositor::new())))
            });
        }
        {
            let view_sink = view_sink.clone();
            registry.add_global(WlOutput, move |id, client| {
                Output::update_display_info(id, client, view_sink.scenic());
                Ok(Box::new(RequestDispatcher::new(Output::new())))
            });
        }
        {
            registry.add_global(WlSeat, move |id, client| {
                let seat = Seat::new();
                seat.post_seat_info(id, client)?;
                Ok(Box::new(RequestDispatcher::new(seat)))
            });
        }
        {
            let scenic = view_sink.scenic_session();
            registry.add_global(WlShm, move |id, client| {
                let shm = Shm::new(scenic.clone());
                // announce the set of supported shm pixel formats.
                shm.post_formats(id, client)?;
                Ok(Box::new(RequestDispatcher::new(shm)))
            });
        }
        {
            registry.add_global(WlDataDeviceManager, move |_, _| {
                Ok(Box::new(RequestDispatcher::new(DataDeviceManager::new())))
            });
        }
        Ok(WaylandDispatcher { display: Arc::new(Mutex::new(Display::new(registry.build()))) })
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
        .add_service(WaylandDispatcher::new()?)
        .start()
        .context("Error starting wayland bridge services server")?;
    exec.run_singlethreaded(fut).context("Failed to execute wayland bridge future")?;
    Ok(())
}
