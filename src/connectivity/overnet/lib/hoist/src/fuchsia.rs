// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(target_os = "fuchsia")]

use {
    anyhow::{Context as _, Error},
    fidl_fuchsia_overnet::{MeshControllerMarker, ServiceConsumerMarker, ServicePublisherMarker},
    fuchsia_component,
    futures::prelude::*,
};

pub use fidl_fuchsia_overnet::{
    MeshControllerProxyInterface, ServiceConsumerProxyInterface, ServicePublisherProxyInterface,
};
pub use fuchsia_async::spawn_local as spawn;

pub fn run<R>(future: impl Future<Output = R> + 'static) -> R {
    fuchsia_async::Executor::new().unwrap().run_singlethreaded(future)
}

pub fn connect_as_service_consumer() -> Result<impl ServiceConsumerProxyInterface, Error> {
    Ok(fuchsia_component::client::connect_to_service::<ServiceConsumerMarker>()
        .context("Failed to connect to overnet ServiceConsumer service")?)
}

pub fn connect_as_service_publisher() -> Result<impl ServicePublisherProxyInterface, Error> {
    Ok(fuchsia_component::client::connect_to_service::<ServicePublisherMarker>()
        .context("Failed to connect to overnet ServicePublisher service")?)
}

pub fn connect_as_mesh_controller() -> Result<impl MeshControllerProxyInterface, Error> {
    Ok(fuchsia_component::client::connect_to_service::<MeshControllerMarker>()
        .context("Failed to connect to overnet MeshController service")?)
}
