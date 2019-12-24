// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    fidl_fuchsia_netemul_guest::{
        CommandListenerMarker, GuestDiscoveryMarker, GuestInteractionMarker,
    },
    fidl_fuchsia_netstack::{NetInterface, NetstackEvent, NetstackMarker},
    fuchsia_async::{Time, Timer},
    fuchsia_component::client,
    fuchsia_zircon::Duration as ZirconDuration,
    futures::{future, select, StreamExt},
    netemul_guest_lib::wait_for_command_completion,
    std::net::IpAddr,
    std::time::Duration,
};

/// Run a command on a guest VM to configure its DHCP server.
///
/// # Arguments
///
/// * `guest_name` - String slice name of the guest to be configured.
/// * `command_to_run` - String slice command that will be executed on the guest VM.
pub async fn configure_dhcp_server(guest_name: &str, command_to_run: &str) -> Result<(), Error> {
    // Run a bash script to start the DHCP service on the Debian guest.
    let mut env = vec![];
    let guest_discovery_service = client::connect_to_service::<GuestDiscoveryMarker>()?;
    let (gis, gis_ch) = fidl::endpoints::create_proxy::<GuestInteractionMarker>()?;
    let () = guest_discovery_service.get_guest(None, guest_name, gis_ch)?;

    let (client_proxy, server_end) = fidl::endpoints::create_proxy::<CommandListenerMarker>()?;

    gis.execute_command(command_to_run, &mut env.iter_mut(), None, None, None, server_end)?;

    // Ensure that the process completes normally.
    wait_for_command_completion(client_proxy.take_event_stream(), None).await
}

/// Ensure that an address is added to a Netstack interface.
///
/// # Arguments
///
/// * `addr` - IpAddress that should appear on a Netstack interface.
/// * `timeout` - Duration to wait for the address to appear in Netstack.
pub async fn verify_addr_present(addr: IpAddr, timeout: Duration) -> Result<(), Error> {
    let addr = fidl_fuchsia_net_ext::IpAddress(addr);
    let timeout = ZirconDuration::from(timeout);

    let netstack = client::connect_to_service::<NetstackMarker>()?;
    let mut stream = netstack.take_event_stream();
    let mut timer = future::maybe_done(Timer::new(Time::after(timeout)));
    let mut ifs: Vec<NetInterface> = Vec::new();

    loop {
        select! {
            event = stream.next() => {
                match event {
                    Some(Ok(NetstackEvent::OnInterfacesChanged {interfaces})) => {
                        let address_present = interfaces
                            .iter()
                            .any(|interface| fidl_fuchsia_net_ext::IpAddress::from(interface.addr) == addr);
                        if address_present {
                            return Ok(());
                        }
                        ifs = interfaces;
                    },
                    Some(Err(e)) => {
                        return Err(format_err!("failed to get network interfaces with {}", e))
                    },
                    None => {
                        return Err(format_err!("could not get event from netstack"))
                    }
                }
            },
            () = timer => {
                break;
            }
        }
    }

    let mut bail_string = String::from("addresses present:");
    for interface in ifs {
        bail_string = format!("{} {:?}: {:?}", bail_string, interface.name, interface.addr);
    }
    bail_string = format!("{} address missing: {:?}", bail_string, addr);
    return Err(format_err!("{}", bail_string));
}
