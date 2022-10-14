// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    fidl_fuchsia_bluetooth_bredr as bredr,
    fuchsia_component::server::{ServiceFs, ServiceObj},
    profile_client::ProfileClient,
    tracing::{debug, error},
};

use crate::peers::Peers;

mod peer_info;
mod peer_task;
mod peers;
mod sdp_data;
mod types;

#[fuchsia::main]
async fn main() -> Result<(), Error> {
    debug!("Started HID component.");

    let (profile_proxy, profile_client) = register_profile()?;
    let mut fs: ServiceFs<ServiceObj<'_, ()>> = ServiceFs::new();

    let inspector = fuchsia_inspect::Inspector::new();
    if let Err(e) = inspect_runtime::serve(&inspector, &mut fs) {
        error!("Could not serve inspect: {}", e);
    }

    let mut peers = Peers::new(profile_client, profile_proxy);
    peers.run().await;

    debug!("Exiting.");
    Ok(())
}

fn register_profile() -> anyhow::Result<(bredr::ProfileProxy, ProfileClient)> {
    debug!("Registering profile.");

    let proxy = fuchsia_component::client::connect_to_protocol::<bredr::ProfileMarker>()
        .context("Failed to connect to Bluetooth Profile service")?;

    // Create a profile that does no advertising.
    let mut client = ProfileClient::new(proxy.clone());

    // Register a search for remote peers that support BR/EDR HID.
    client
        .add_search(bredr::ServiceClassProfileIdentifier::HumanInterfaceDevice, &[])
        .context("Failed to search for peers supporting HID.")?;

    Ok((proxy, client))
}
