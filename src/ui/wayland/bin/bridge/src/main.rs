// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![recursion_limit = "128"]

use {
    anyhow::Error,
    fidl_fuchsia_wayland::{Server_Request, Server_RequestStream},
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_trace_provider::trace_provider_create_with_fdio,
    futures::prelude::*,
    wayland_bridge::dispatcher::WaylandDispatcher,
    wayland_bridge::display::Display,
};

fn spawn_wayland_server_service(mut stream: Server_RequestStream, display: Display) {
    fasync::Task::local(
        async move {
            while let Some(Server_Request::Connect { channel, .. }) =
                stream.try_next().await.unwrap()
            {
                display.clone().spawn_new_client(
                    fasync::Channel::from_channel(channel).unwrap(),
                    cfg!(feature = "protocol_logging"),
                );
            }
            Ok(())
        }
        .unwrap_or_else(|e: anyhow::Error| println!("{:?}", e)),
    )
    .detach();
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
    fs.dir("svc").add_fidl_service(|stream| spawn_wayland_server_service(stream, display.clone()));
    fs.take_and_serve_directory_handle().unwrap();
    executor.run_singlethreaded(fs.collect::<()>());
    Ok(())
}
