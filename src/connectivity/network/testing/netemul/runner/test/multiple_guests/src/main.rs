// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    fidl_fuchsia_netemul_guest::{
        CommandListenerMarker, GuestDiscoveryMarker, GuestInteractionMarker,
    },
    fuchsia_async as fasync,
    fuchsia_component::client,
    netemul_guest_lib::wait_for_command_completion,
};

async fn test_multihop_ping() -> Result<(), Error> {
    // Configure the Debian guest VM acting as a router.
    let guest_discovery_service = client::connect_to_service::<GuestDiscoveryMarker>()?;
    let (router_gis, gis_ch) = fidl::endpoints::create_proxy::<GuestInteractionMarker>()?;
    let () = guest_discovery_service.get_guest(None, "debian_guest_1", gis_ch)?;

    let (client_proxy, server_end) = fidl::endpoints::create_proxy::<CommandListenerMarker>()
        .context("Failed to create CommandListener ends")?;

    let () = router_gis.execute_command(
        "/bin/sh -c /root/input/setup_linux_router.sh",
        &mut [].iter_mut(),
        None,
        None,
        None,
        server_end,
    )?;

    let () = wait_for_command_completion(client_proxy.take_event_stream(), None)
        .await
        .context("Failed to configure router")?;

    // Configure the Debian guest VM acting as a client endpoint.
    let (client_gis, gis_ch) = fidl::endpoints::create_proxy::<GuestInteractionMarker>()?;
    let () = guest_discovery_service.get_guest(None, "debian_guest_2", gis_ch)?;

    let (client_proxy, server_end) = fidl::endpoints::create_proxy::<CommandListenerMarker>()
        .context("Failed to create CommandListener ends")?;

    let () = client_gis.execute_command(
        "/bin/sh -c /root/input/setup_linux_client.sh",
        &mut [].iter_mut(),
        None,
        None,
        None,
        server_end,
    )?;

    let () = wait_for_command_completion(client_proxy.take_event_stream(), None)
        .await
        .context("Failed to configure client")?;

    // Ping from the Linux client through the Linux router to the Fuchsia endpoint.
    let (client_proxy, server_end) = fidl::endpoints::create_proxy::<CommandListenerMarker>()
        .context("Failed to create CommandListener ends")?;

    let () = client_gis.execute_command(
        "/bin/ping -c 1 192.168.0.2",
        &mut [].iter_mut(),
        None,
        None,
        None,
        server_end,
    )?;

    let () = wait_for_command_completion(client_proxy.take_event_stream(), None)
        .await
        .context("Failed to configure client")?;
    return Ok(());
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    test_multihop_ping().await
}
