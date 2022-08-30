// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod defaults;
mod errors;
mod items;
mod service;
#[cfg(test)]
mod service_tests;
mod tasks;

use {
    anyhow::{Context, Error},
    fidl::endpoints::ProtocolMarker,
    fidl_fuchsia_ui_focus as focus,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_component::server::ServiceFs,
    futures::StreamExt,
    service::Service,
};

use clipboard_shared as shared;
#[cfg(test)]
use clipboard_test_helpers as test_helpers;

#[fuchsia::main(logging_tags = ["clipboard"])]
async fn main() -> Result<(), Error> {
    let focus_provider_proxy = connect_to_protocol::<focus::FocusChainProviderMarker>()
        .with_context(|| {
            format!("connecting to {}", focus::FocusChainProviderMarker::DEBUG_NAME)
        })?;

    let service = Service::new(focus_provider_proxy);
    let mut fs = ServiceFs::new_local();
    fs.dir("svc")
        .add_fidl_service(|stream| service.spawn_focused_writer_registry(stream))
        .add_fidl_service(|stream| service.spawn_focused_reader_registry(stream));

    fs.take_and_serve_directory_handle()?;

    let () = fs.collect().await;

    Ok(())
}
