// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::args,
    anyhow::{Context, Error},
    fidl_fuchsia_update::{
        CheckOptions, Initiator, ManagerMarker, ManagerProxy, MonitorMarker, MonitorRequest,
        MonitorRequestStream,
    },
    fidl_fuchsia_update_ext::State,
    fuchsia_component::client::connect_to_service,
    futures::prelude::*,
};

pub async fn handle_check_now_cmd(cmd: args::CheckNow) -> Result<(), Error> {
    let update_manager =
        connect_to_service::<ManagerMarker>().context("Failed to connect to update manager")?;
    handle_check_now_cmd_impl(cmd, &update_manager).await
}

fn print_state(state: &State) {
    println!("State: {:?}", state);
}

async fn monitor_state(mut stream: MonitorRequestStream) -> Result<(), Error> {
    while let Some(event) = stream.try_next().await? {
        match event {
            MonitorRequest::OnState { state, responder } => {
                responder.send()?;

                let state = State::from(state);

                // Exit if we encounter an error during an update.
                if state.is_error() {
                    anyhow::bail!("Update failed: {:?}", state)
                } else {
                    print_state(&state);
                }
            }
        }
    }
    Ok(())
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
