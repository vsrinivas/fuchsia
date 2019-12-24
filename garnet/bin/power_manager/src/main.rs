// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// core
mod message;
mod node;
mod power_manager;
mod types;

// nodes
mod cpu_control_handler;
mod cpu_stats_handler;
mod system_power_handler;
mod temperature_handler;
mod thermal_limiter;
mod thermal_policy;

use crate::power_manager::PowerManager;
use anyhow::{format_err, Context, Error};
use fdio;
use fidl_fuchsia_sysinfo as fsysinfo;
use fuchsia_async as fasync;
use fuchsia_syslog::fx_log_info;
use fuchsia_zircon as zx;
use futures::future;

async fn get_board_name() -> Result<String, Error> {
    let (client, server) = zx::Channel::create()?;
    fdio::service_connect("/dev/misc/sysinfo", server)?;
    let svc = fsysinfo::DeviceProxy::new(fasync::Channel::from_channel(client)?);
    let (status, name_opt) = svc.get_board_name().await.context("get_board_name failed")?;
    zx::Status::ok(status).context("get_board_name returned error status")?;
    name_opt.ok_or(format_err!("Failed to get board name"))
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["power_manager"])?;
    fuchsia_syslog::set_verbosity(1);

    fx_log_info!("started");

    let board = get_board_name().await?;
    let mut pm = PowerManager::new(board).context("failed to create PowerManager")?;
    pm.init().context("failed to init PowerManager")?;

    // At this point, the core nodes and power manager logic should be up and running. We
    // now block here forever to allow the nodes to execute their tasks. At any given moment,
    // the PowerManager most likely does not have any runnable tasks. The PowerManager will run
    // once any of its tasks are made runnable by an event such as a timer or stream event
    // from an external source.
    future::pending::<()>().await;

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
