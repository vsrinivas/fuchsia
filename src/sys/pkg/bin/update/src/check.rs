// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{args, monitor_state::monitor_state},
    anyhow::{Context, Error},
    fidl_fuchsia_update::{CheckOptions, Initiator, ManagerMarker, ManagerProxy, MonitorMarker},
    fuchsia_component::client::connect_to_protocol,
};

pub async fn handle_check_now_cmd(cmd: args::CheckNow) -> Result<(), Error> {
    let update_manager =
        connect_to_protocol::<ManagerMarker>().context("Failed to connect to update manager")?;
    handle_check_now_cmd_impl(cmd, &update_manager).await
}

async fn handle_check_now_cmd_impl(
    cmd: args::CheckNow,
    update_manager: &ManagerProxy,
) -> Result<(), Error> {
    let args::CheckNow { service_initiated, monitor } = cmd;
    let options = CheckOptions {
        initiator: Some(if service_initiated { Initiator::Service } else { Initiator::User }),
        allow_attaching_to_existing_update_check: Some(true),
        ..CheckOptions::EMPTY
    };
    let (monitor_client, monitor_server) = if monitor {
        let (client_end, request_stream) =
            fidl::endpoints::create_request_stream::<MonitorMarker>()?;
        (Some(client_end), Some(request_stream))
    } else {
        (None, None)
    };
    if let Err(e) = update_manager.check_now(options, monitor_client).await? {
        anyhow::bail!("Update check failed to start: {:?}", e);
    }
    println!("Checking for an update.");
    if let Some(monitor_server) = monitor_server {
        monitor_state(monitor_server).await?;
    }
    Ok(())
}
