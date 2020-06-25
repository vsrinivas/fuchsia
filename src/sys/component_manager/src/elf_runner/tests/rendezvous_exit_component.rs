// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_fidl_test_components as test_protocol, fuchsia_async as fasync,
    fuchsia_component::client as component,
    fuchsia_syslog::{self as fxlog, fx_log_info},
};

/// Connects to the Trigger protocol, sends a request, and exits.
#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fxlog::init().unwrap();
    let trigger = component::connect_to_service::<test_protocol::TriggerMarker>()?;
    let _ = trigger.run().await?;
    fx_log_info!("Rendezvous complete, exiting");
    Ok(())
}
