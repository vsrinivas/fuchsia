// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    fidl_fuchsia_hardware_power_statecontrol::{AdminMarker, AdminProxy, RebootReason},
    fidl_fuchsia_paver::{BootManagerMarker, BootManagerProxy, PaverMarker},
    fuchsia_component::client::connect_to_service,
    fuchsia_zircon::Status,
};

/// Connects to FIDL services and reverts the update.
pub async fn handle_revert() -> Result<(), Error> {
    let admin = connect_to_service::<AdminMarker>().context("while connecting to admin")?;

    let paver = connect_to_service::<PaverMarker>().context("while connecting to paver")?;
    let (boot_manager, server_end) = fidl::endpoints::create_proxy::<BootManagerMarker>()?;
    let () = paver.find_boot_manager(server_end).context("while connecting to boot manager")?;

    println!("Reverting the update.");
    handle_revert_impl(&admin, &boot_manager).await
}

/// Reverts the update using the passed in FIDL proxies.
async fn handle_revert_impl(
    admin: &AdminProxy,
    boot_manager: &BootManagerProxy,
) -> Result<(), Error> {
    let current_config = boot_manager
        .query_current_configuration()
        .await
        .context("while calling query_current_configuration")?
        .map_err(Status::from_raw)
        .context("query_current_configuration responded with")?;

    let () = Status::ok(
        boot_manager
            .set_configuration_unbootable(current_config)
            .await
            .context("while calling set_configuration_unbootable")?,
    )
    .context("set_configuration_unbootable responded with")?;

    let () = Status::ok(boot_manager.flush().await.context("while calling flush")?)
        .context("flush responded with")?;

    admin
        .reboot(RebootReason::UserRequest)
        .await
        .context("while performing reboot call")?
        .map_err(Status::from_raw)
        .context("reboot responded with")
}
