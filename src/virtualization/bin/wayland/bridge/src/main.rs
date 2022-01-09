// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![recursion_limit = "128"]

use {
    crate::dispatcher::*,
    anyhow::Error,
    fidl::endpoints::RequestStream,
    fidl_fuchsia_virtualization::{WaylandDispatcherRequest, WaylandDispatcherRequestStream},
    fidl_fuchsia_wayland::{ViewProducerRequest, ViewProducerRequestStream},
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_trace_provider::trace_provider_create_with_fdio,
    futures::prelude::*,
};

#[cfg(test)]
mod test_protocol;

mod alpha_compositing;
mod aura_shell;
mod client;
mod compositor;
mod data_device_manager;
pub mod dispatcher;
pub mod display;
mod object;
mod registry;
use crate::display::*;
mod buffer;
mod linux_dmabuf;
mod output;
mod scenic;
mod seat;
mod secure_output;
mod shm;
mod subcompositor;
mod viewporter;
mod xdg_shell;

fn spawn_wayland_dispatcher_service(mut stream: WaylandDispatcherRequestStream, display: Display) {
    fasync::Task::local(
        async move {
            while let Some(WaylandDispatcherRequest::OnNewConnection { channel, .. }) =
                stream.try_next().await.unwrap()
            {
                display.clone().spawn_new_client(fasync::Channel::from_channel(channel).unwrap());
            }
            Ok(())
        }
        .unwrap_or_else(|e: anyhow::Error| println!("{:?}", e)),
    )
    .detach();
}

fn spawn_view_producer_service(mut stream: ViewProducerRequestStream, display: Display) {
    let control_handle = stream.control_handle();
    let mut display_clone = display.clone();
    fasync::Task::local(
        async move {
            while let Some(ViewProducerRequest::RequestView { responder, view_spec }) =
                stream.try_next().await.unwrap()
            {
                display_clone.request_view_provider(view_spec);
                responder.send().expect("fidl error");
            }
            Ok(())
        }
        .unwrap_or_else(|e: anyhow::Error| println!("{:?}", e)),
    )
    .detach();

    display.bind_view_producer(control_handle);
}

fn main() -> Result<(), Error> {
    trace_provider_create_with_fdio();
    let mut executor = fasync::LocalExecutor::new()?;
    let mut dispatcher = WaylandDispatcher::new()?;

    // Try to get display properties before serving.
    let scenic = dispatcher.display.scenic().clone();
    match executor.run_singlethreaded(async { scenic.get_display_info().await }) {
        Ok(display_info) => dispatcher.display.set_display_info(&display_info),
        Err(e) => eprintln!("get_display_info error: {:?}", e),
    }

    let display = &dispatcher.display;
    let mut fs = ServiceFs::new();
    fs.dir("svc")
        .add_fidl_service(|stream| spawn_wayland_dispatcher_service(stream, display.clone()))
        .add_fidl_service(|stream| spawn_view_producer_service(stream, display.clone()));
    fs.take_and_serve_directory_handle().unwrap();
    executor.run_singlethreaded(fs.collect::<()>());
    Ok(())
}
