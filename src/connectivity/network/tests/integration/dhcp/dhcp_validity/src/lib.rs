// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context as _, Error},
    fidl_fuchsia_net as fnet, fidl_fuchsia_net_dhcpv6 as fdhcpv6,
    fidl_fuchsia_net_name as fnetname, fidl_fuchsia_net_stack as fstack,
    fidl_fuchsia_net_stack_ext::FidlReturn as _,
    fidl_fuchsia_netemul_guest::{
        CommandListenerMarker, GuestDiscoveryMarker, GuestInteractionMarker,
    },
    fidl_fuchsia_netstack::{NetInterface, NetstackEvent, NetstackMarker},
    fuchsia_async::{Time, Timer},
    fuchsia_component::client,
    fuchsia_zircon::Duration as ZirconDuration,
    futures::{future, select, StreamExt},
    netemul_guest_lib::wait_for_command_completion,
    std::{fmt::Write, time::Duration},
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
pub async fn verify_v4_addr_present(addr: fnet::IpAddress, timeout: Duration) -> Result<(), Error> {
    let timeout = ZirconDuration::from(timeout);

    let netstack = client::connect_to_service::<NetstackMarker>()?;
    let mut stream = netstack.take_event_stream();
    let mut timer = future::maybe_done(Timer::new(Time::after(timeout)));
    let mut ifs: Vec<NetInterface> = Vec::new();

    loop {
        select! {
            event = stream.next() => {
                let NetstackEvent::OnInterfacesChanged { interfaces } = event
                    .ok_or(format_err!("netstack event stream ended"))?
                    .context("failed to get network interface")?;
                if interfaces.iter().any(|interface| interface.addr == addr) {
                    return Ok(());
                }
                ifs = interfaces;
            },
            () = timer => {
                break;
            }
        }
    }

    let mut addresses_present = String::from("addresses present:");
    for interface in ifs {
        let () = write!(addresses_present, " {:?}: {:?}", interface.name, interface.addr)
            .context("writing to bail string")?;
    }
    Err(format_err!(
        "DHCPv4 client got unexpected addresses: {}, address missing: {:?}",
        addresses_present,
        addr
    ))
}

/// Verifies a DHCPv6 client can receive an expected list of DNS servers.
///
/// # Arguments
///
/// * `interface_id` - ID identifying the interface to start the DHCPv6 client on.
/// * `want_dns_servers` - Vector of DNS servers that the DHCPv6 client is expected to receive.
pub async fn verify_v6_dns_servers(
    interface_id: u64,
    want_dns_servers: Vec<fnetname::DnsServer_>,
) -> Result<(), Error> {
    let stack = client::connect_to_service::<fstack::StackMarker>()
        .context("connecting to stack service")?;
    let info = stack
        .get_interface_info(interface_id)
        .await
        .squash_result()
        .context("getting interface info")?;

    let addr = info
        .properties
        .addresses
        .into_iter()
        .find_map(|addr: fnet::Subnet| match addr.addr {
            fnet::IpAddress::Ipv4(_addr) => None,
            fnet::IpAddress::Ipv6(addr) => {
                // Only use link-local addresses.
                //
                // TODO(https://github.com/rust-lang/rust/issues/27709): use
                // `is_unicast_link_local_strict` when it's available in stable Rust.
                if addr.addr[..8] == [0xfe, 0x80, 0, 0, 0, 0, 0, 0] {
                    Some(addr)
                } else {
                    None
                }
            }
        })
        .ok_or(format_err!("no addresses found to start DHCPv6 client on"))?;

    let provider = client::connect_to_service::<fdhcpv6::ClientProviderMarker>()
        .context("connecting to DHCPv6 client")?;

    let (client_end, server_end) = fidl::endpoints::create_endpoints::<fdhcpv6::ClientMarker>()
        .context("creating DHCPv6 client channel")?;
    let () = provider.new_client(
        fdhcpv6::NewClientParams {
            interface_id: Some(interface_id),
            address: Some(fnet::Ipv6SocketAddress {
                address: addr,
                port: fdhcpv6::DEFAULT_CLIENT_PORT,
                zone_index: interface_id,
            }),
            models: Some(fdhcpv6::OperationalModels {
                stateless: Some(fdhcpv6::Stateless {
                    options_to_request: Some(vec![fdhcpv6::RequestableOptionCode::DnsServers]),
                }),
            }),
        },
        server_end,
    )?;
    let client_proxy = client_end.into_proxy().context("getting client proxy from channel")?;

    let got_dns_servers = client_proxy.watch_servers().await.context("watching DNS servers")?;
    if got_dns_servers == want_dns_servers {
        Ok(())
    } else {
        Err(format_err!(
            "DHCPv6 client received unexpected DNS servers:\ngot dns servers: {:?}\n, want dns servers: {:?}\n",
            got_dns_servers,
            want_dns_servers
        ))
    }
}
