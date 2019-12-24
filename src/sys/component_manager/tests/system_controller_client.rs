// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow, fidl_fuchsia_sys2 as fuchsia_sys2, fuchsia_async as fasync,
    fuchsia_component::client as component_client, fuchsia_syslog::fx_log_err,
};

#[fasync::run_singlethreaded]
async fn main() -> Result<(), anyhow::Error> {
    let _ = fuchsia_syslog::init_with_tags(&["system_controller_consumer"]);

    let shutdown = component_client::connect_to_service::<fuchsia_sys2::SystemControllerMarker>()
        .expect("Failed to connect to fuchsia.sys2.SystemController");
    match shutdown.shutdown().await {
        Ok(()) => Ok(()),
        Err(e) => {
            fx_log_err!("Failure calling shutdown {:?}", e);
            Err(e.into())
        }
    }
}
