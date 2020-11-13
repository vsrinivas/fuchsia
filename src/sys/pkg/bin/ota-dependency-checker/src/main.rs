// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    fidl_fuchsia_paver::PaverMarker,
    fuchsia_async as fasync,
    fuchsia_syslog::fx_log_info,
};

mod check_and_set;

pub fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["ota-dependency-checker"])
        .context("while initializing logger")?;
    fx_log_info!("starting ota-dependency-checker");

    let mut executor = fasync::Executor::new().context("error creating executor")?;
    let () = executor.run_singlethreaded(main_inner_async())?;

    fx_log_info!("shutting down ota-dependency-checker");
    Ok(())
}

// Soon, we will grow this to be smarter. Everyone starts somewhere. 🌱
async fn main_inner_async() -> Result<(), Error> {
    let paver = fuchsia_component::client::connect_to_service::<PaverMarker>()
        .context("while connecting to paver")?;
    let (boot_manager, boot_manager_server_end) =
        fidl::endpoints::create_proxy().context("while creating BootManager endpoints")?;

    paver
        .find_boot_manager(boot_manager_server_end)
        .context("transport error while calling find_boot_manager()")?;

    // NOTE: The docs for check_and_set_system_health say that we should respond to
    // an error here by rebooting, but we'll be refactoring this away Soon™, so for now
    // we just log it.
    crate::check_and_set::check_and_set_system_health(&boot_manager)
        .await
        .context("while checking and setting system health")
}
