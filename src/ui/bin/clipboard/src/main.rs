// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod defaults;
mod errors;
mod inspect;
mod items;
mod metadata;
mod service;
#[cfg(test)]
mod service_tests;
mod tasks;
mod watch;
#[cfg(test)]
mod watch_tests;

use {
    anyhow::{Context, Error},
    fidl::endpoints::ProtocolMarker,
    fidl_fuchsia_ui_focus as focus,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_component::server::ServiceFs,
    fuchsia_inspect::component::inspector,
    futures::StreamExt,
    service::{MonotonicClock, Service, ServiceDependencies},
};

use clipboard_shared as shared;
#[cfg(test)]
use clipboard_test_helpers as test_helpers;

#[derive(Debug)]
enum RealServiceDependencies {/* not instantiable */}
impl ServiceDependencies for RealServiceDependencies {
    type Clock = MonotonicClock;
}

#[fuchsia::main(logging_tags = ["clipboard"])]
async fn main() -> Result<(), Error> {
    let focus_provider_proxy = connect_to_protocol::<focus::FocusChainProviderMarker>()
        .with_context(|| {
            format!("connecting to {}", focus::FocusChainProviderMarker::DEBUG_NAME)
        })?;

    let service = Service::<RealServiceDependencies>::new(
        MonotonicClock::new(),
        focus_provider_proxy,
        inspector().root(),
    );
    let mut fs = ServiceFs::new_local();
    fs.dir("svc")
        .add_fidl_service(|stream| service.spawn_focused_writer_registry(stream))
        .add_fidl_service(|stream| service.spawn_focused_reader_registry(stream));

    inspect_runtime::serve(inspector(), &mut fs)?;

    fs.take_and_serve_directory_handle()?;

    let () = fs.collect().await;

    Ok(())
}
