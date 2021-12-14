// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow, fidl_fuchsia_sys2 as fuchsia_sys2, fuchsia_component::client as component_client,
    tracing::error,
};

#[fuchsia::component(logging_tags = ["system_controller_consumer"])]
async fn main() -> Result<(), anyhow::Error> {
    let shutdown = component_client::connect_to_protocol::<fuchsia_sys2::SystemControllerMarker>()
        .expect("Failed to connect to fuchsia.sys2.SystemController");
    match shutdown.shutdown().await {
        Ok(()) => Ok(()),
        Err(err) => {
            error!(?err, "Failure calling shutdown");
            Err(err.into())
        }
    }
}
