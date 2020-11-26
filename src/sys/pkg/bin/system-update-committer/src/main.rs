// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::fidl::FidlServer,
    anyhow::{anyhow, Context, Error},
    fidl_fuchsia_paver::PaverMarker,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_syslog::{fx_log_info, fx_log_warn},
    fuchsia_zircon::{self as zx, EventPair, HandleBased},
    futures::{prelude::*, stream::FuturesUnordered},
    std::sync::Arc,
};

mod check_and_set;
mod fidl;

pub fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["system-update-committer"])
        .context("while initializing logger")?;
    fx_log_info!("starting system-update-committer");

    let mut executor = fasync::Executor::new().context("error creating executor")?;
    let () = executor.run_singlethreaded(main_inner_async())?;

    fx_log_info!("shutting down system-update-committer");
    Ok(())
}

// Soon, we will grow this to be smarter. Everyone starts somewhere. 🌱
async fn main_inner_async() -> Result<(), Error> {
    let paver = fuchsia_component::client::connect_to_service::<PaverMarker>()
        .context("while connecting to paver")?;
    let (boot_manager, boot_manager_server_end) =
        ::fidl::endpoints::create_proxy().context("while creating BootManager endpoints")?;

    paver
        .find_boot_manager(boot_manager_server_end)
        .context("transport error while calling find_boot_manager()")?;

    let futures = FuturesUnordered::new();
    let (p_check, p_fidl) = EventPair::create().context("while creating EventPairs")?;
    let p_check_clone = p_check
        .duplicate_handle(zx::Rights::SIGNAL_PEER | zx::Rights::SIGNAL)
        .context("while duplicating p_check")?;

    // Handle check and set.
    futures.push(
        async move {
            // NOTE: The docs for check_and_set_system_health say that we should respond to
            // an error here by rebooting, but we'll be refactoring this away Soon™, so for now
            // we just log it.
            if let Err(e) =
                crate::check_and_set::check_and_set_system_health(&boot_manager, &p_check_clone)
                    .await
            {
                fx_log_warn!("error checking and setting system health: {:#}", anyhow!(e));
            }
        }
        .boxed_local(),
    );

    // Handle FIDL.
    let mut fs = ServiceFs::new_local();
    fs.take_and_serve_directory_handle().context("while serving directory handle")?;
    let fidl = Arc::new(FidlServer::new(p_check, p_fidl));
    futures.push(FidlServer::run(fidl, fs).boxed_local());

    let () = futures.collect::<()>().await;

    Ok(())
}
