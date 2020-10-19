// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context},
    fidl_fuchsia_paver::{Configuration, PaverMarker},
    fuchsia_component::client::connect_to_service,
    fuchsia_syslog::{fx_log_err, fx_log_info},
    fuchsia_zircon::Status,
};

/// Inform the Paver service that Fuchsia booted successfully, so it marks the partition healthy
/// and stops decrementing the boot counter.
pub async fn set_active_configuration_healthy() {
    if let Err(err) = set_active_configuration_healthy_impl().await {
        fx_log_err!("error marking active configuration successful: {:#}", err);
    }
}

async fn set_active_configuration_healthy_impl() -> Result<(), anyhow::Error> {
    let paver = connect_to_service::<PaverMarker>()?;

    let (boot_manager, boot_manager_server_end) = fidl::endpoints::create_proxy()?;

    paver
        .find_boot_manager(boot_manager_server_end)
        .context("transport error while calling find_boot_manager()")?;

    // TODO(51480): This should use the "current" configuration, not active, when they differ.
    let active_config = match boot_manager
        .query_active_configuration()
        .await
        .map(|res| res.map_err(Status::from_raw))
    {
        Ok(Ok(active_config)) => active_config,
        Err(fidl::Error::ClientChannelClosed { status: Status::NOT_SUPPORTED, .. }) => {
            fx_log_info!("ABR not supported");
            return Ok(());
        }
        Ok(Err(Status::NOT_SUPPORTED)) => {
            fx_log_info!("no partition is active; we're in recovery");
            return Ok(());
        }
        Err(e) => {
            return Err(e).context("transport error while calling query_active_configuration()")
        }
        Ok(Err(e)) => {
            return Err(e).context("paver error while calling query_active_configuration()")
        }
    };

    // Note: at this point, we know that ABR is supported.
    boot_manager
        .set_configuration_healthy(active_config)
        .await
        .map(Status::ok)
        .with_context(|| {
            format!("transport error while calling set_configuration_healthy({:?})", active_config)
        })?
        .with_context(|| {
            format!("paver error while calling set_configuration_healthy({:?})", active_config)
        })?;

    // Find out the inactive partition and mark it as unbootable.
    let inactive_config = match active_config {
        Configuration::A => Configuration::B,
        Configuration::B => Configuration::A,
        Configuration::Recovery => return Err(format_err!("Recovery should not be active")),
    };
    boot_manager
        .set_configuration_unbootable(inactive_config)
        .await
        .map(Status::ok)
        .with_context(|| {
            format!(
                "transport error while calling set_configuration_unbootable({:?})",
                inactive_config
            )
        })?
        .with_context(|| {
            format!("paver error while calling set_configuration_unbootable({:?})", inactive_config)
        })?;

    boot_manager
        .flush()
        .await
        .map(Status::ok)
        .context("transport error while calling flush()")?
        .context("paver error while calling flush()")?;

    Ok(())
}
