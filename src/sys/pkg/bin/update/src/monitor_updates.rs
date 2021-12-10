// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::monitor_state::monitor_state,
    anyhow::Context as _,
    fidl_fuchsia_update::{
        AttemptsMonitorMarker, AttemptsMonitorRequest, AttemptsMonitorRequestStream, ManagerMarker,
    },
    fuchsia_component::client::connect_to_protocol,
    futures::prelude::*,
};

pub async fn handle_monitor_updates_cmd() -> Result<(), anyhow::Error> {
    let update_manager =
        connect_to_protocol::<ManagerMarker>().context("Failed to connect to update manager")?;

    let (client_end, request_stream) =
        fidl::endpoints::create_request_stream::<AttemptsMonitorMarker>()?;

    if let Err(e) = update_manager.monitor_all_update_checks(client_end) {
        anyhow::bail!("Failed to monitor all update checks: {:?}", e);
    }
    monitor_all(request_stream).await?;
    Ok(())
}

async fn monitor_all(mut stream: AttemptsMonitorRequestStream) -> Result<(), anyhow::Error> {
    while let Some(event) = stream.try_next().await? {
        match event {
            AttemptsMonitorRequest::OnStart { options, monitor, responder } => {
                responder.send()?;

                match options.initiator {
                    Some(initiator) => {
                        println!("{:?} started an update attempt", initiator)
                    }
                    None => println!("an update attempt was started"),
                }
                if let Err(e) = monitor_state(monitor.into_stream()?).await {
                    eprintln!("Error: {:?}", e);
                }
            }
        }
    }
    Ok(())
}
