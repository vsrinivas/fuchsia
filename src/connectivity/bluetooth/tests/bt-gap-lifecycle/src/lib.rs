// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This module contains lifecycle tests for the bt-gap component
//! (//src/connectivity/bluetooth/core/bt-gap)

use {
    anyhow::{Context, Error},
    fidl_fuchsia_bluetooth_sys::BootstrapMarker,
    fuchsia_component::client::connect_to_protocol,
    log::error,
};

/// Test that BT-GAP can initialize and correctly and begin serving its own FIDL protocols (in this
/// case, the protocol `fuchsia.bluetooth.sys.Bootstrap`). This serves as a smoke test for any
/// changes to the system that would prevent BT-GAP from initializing correctly
#[fuchsia_async::run_singlethreaded(test)]
async fn test_bt_gap_initializes() -> Result<(), Error> {
    let bootstrap = connect_to_protocol::<BootstrapMarker>()
        .context("Error connecting to Bootstrap protocol served by bt-gap")?;
    if let Err(e) = bootstrap.commit().await {
        let error = Error::from(e)
            .context("Error calling Bootstrap.Commit(), did bt-gap not initialize correctly?");
        error!("{:?}", error);
        return Err(error);
    }
    Ok(())
}
