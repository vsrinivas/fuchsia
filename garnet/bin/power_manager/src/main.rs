// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// core
mod error;
mod message;
mod node;
mod power_manager;
mod types;
mod utils;

// nodes
mod cpu_control_handler;
mod cpu_stats_handler;
mod dev_control_handler;
mod system_power_handler;
mod temperature_handler;
mod thermal_limiter;
mod thermal_policy;

use crate::power_manager::PowerManager;
use anyhow::{format_err, Context, Error};
use fdio;
use fidl_fuchsia_sysinfo as fsysinfo;
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use fuchsia_syslog::fx_log_info;
use fuchsia_trace_provider;
use fuchsia_zircon as zx;
use futures::stream::StreamExt;

async fn get_board_name() -> Result<String, Error> {
    let (client, server) = zx::Channel::create()?;
    fdio::service_connect("/svc/fuchsia.sysinfo.SysInfo", server)?;
    let svc = fsysinfo::SysInfoProxy::new(fasync::Channel::from_channel(client)?);
    let (status, name_opt) = svc.get_board_name().await.context("get_board_name failed")?;
    zx::Status::ok(status).context("get_board_name returned error status")?;
    name_opt.ok_or(format_err!("Failed to get board name"))
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    // Setup tracing
    fuchsia_trace_provider::trace_provider_create_with_fdio();

    // Setup syslog
    fuchsia_syslog::init_with_tags(&["power_manager"])?;
    fuchsia_syslog::set_verbosity(1);
    fx_log_info!("started");

    // Create a new ServiceFs to incoming handle service requests for the various services that the
    // PowerManager hosts.
    let mut fs = ServiceFs::new_local();

    // Setup the PowerManager
    let board = get_board_name().await?;
    let mut pm = PowerManager::new(board).context("failed to create PowerManager")?;
    pm.init(&mut fs).context("failed to init PowerManager")?;

    // Allow our services to be discovered.
    fs.take_and_serve_directory_handle()?;

    // Run the ServiceFs (handles incoming request streams). This future never completes.
    fs.collect::<()>().await;

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[fasync::run_singlethreaded(test)]
    async fn test_get_board_name() {
        let board = get_board_name().await.context("Failed to get board name").unwrap();
        assert_ne!(board, "");
    }
}
