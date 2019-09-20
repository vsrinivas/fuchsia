// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_paver::PaverMarker, fuchsia_component::client::connect_to_service,
    fuchsia_syslog::fx_log_err, fuchsia_zircon::Status,
};

/// Inform the Paver service that Fuchsia booted successfully, so it marks the partition healthy
/// and stops decrementing the boot counter.
pub async fn mark_active_configuration_successful() {
    if let Err(err) = mark_active_configuration_successful_impl().await {
        fx_log_err!("error marking active configuration successful: {}", err);
    }
}

async fn mark_active_configuration_successful_impl() -> Result<(), failure::Error> {
    let paver = connect_to_service::<PaverMarker>()?;
    Status::ok(paver.mark_active_configuration_successful().await?)?;
    Ok(())
}
