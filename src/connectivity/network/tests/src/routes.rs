// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::environments::{Netstack2, TestSandboxExt as _};
use crate::Result;
use anyhow::Context as _;
use fidl_fuchsia_net_stack_ext::FidlReturn as _;
use net_declare::{fidl_ip, fidl_subnet};
use std::convert::TryFrom as _;

#[fuchsia_async::run_singlethreaded(test)]
async fn test_resolve_route() -> Result {
    const HOST_IP_V4: fidl_fuchsia_net::Subnet = fidl_subnet!(192.168.0.2/24);
    const GATEWAY_IP_V4: fidl_fuchsia_net::Subnet = fidl_subnet!(192.168.0.1/24);
    const GATEWAY_IP_V6: fidl_fuchsia_net::Subnet = fidl_subnet!(3080::1/64);
    const HOST_IP_V6: fidl_fuchsia_net::Subnet = fidl_subnet!(3080::2/64);

    let sandbox = netemul::TestSandbox::new().context("failed to create sandbox")?;
    let net = sandbox.create_network("net").await.context("failed to create network")?;

    // Configure a host.
    let host = sandbox
        .create_netstack_environment::<Netstack2, _>("resolve_route_host")
        .context("failed to create client environment")?;

    let host_netstack = host
        .connect_to_service::<fidl_fuchsia_netstack::NetstackMarker>()
        .context("failed to connect to netstack")?;
    let host_interface_state = host
        .connect_to_service::<fidl_fuchsia_net_interfaces::StateMarker>()
        .context("failed to connect to fuchsia.net.interfaces/State")?;

    let host_ep = host
        .join_network::<netemul::NetworkDevice, _>(
            &net,
            "host",
            netemul::InterfaceConfig::StaticIp(HOST_IP_V4),
        )
        .await
        .context("host failed to join network")?;
    let () = host_ep.add_ip_addr(HOST_IP_V6).await.context("failed to add IPv6 address to host")?;
    let () = fidl_fuchsia_net_interfaces_ext::wait_interface_with_id(
        fidl_fuchsia_net_interfaces_ext::event_stream_from_state(&host_interface_state)?,
        &mut fidl_fuchsia_net_interfaces_ext::InterfaceState::Unknown(host_ep.id()),
        |properties| {
            if properties.addresses.as_ref()?.iter().any(|a| a.addr == Some(HOST_IP_V6)) {
                Some(())
            } else {
                None
            }
        },
    )
    .await
    .context("failed to observe host IPv6")?;

    // Configure a gateway.
    let gateway = sandbox
        .create_netstack_environment::<Netstack2, _>("resolve_route_gateway")
        .context("failed to create server environment")?;

    let gateway_interface_state = gateway
        .connect_to_service::<fidl_fuchsia_net_interfaces::StateMarker>()
        .context("failed to connect to fuchsia.net.interfaces/State")?;

    let gateway_ep = gateway
        .join_network::<netemul::NetworkDevice, _>(
            &net,
            "gateway",
            netemul::InterfaceConfig::StaticIp(GATEWAY_IP_V4),
        )
        .await
        .context("gateway failed to join network")?;
    let () = gateway_ep
        .add_ip_addr(GATEWAY_IP_V6)
        .await
        .context("failed to add IPv6 address to gateway")?;
    let () = fidl_fuchsia_net_interfaces_ext::wait_interface_with_id(
        fidl_fuchsia_net_interfaces_ext::event_stream_from_state(&gateway_interface_state)?,
        &mut fidl_fuchsia_net_interfaces_ext::InterfaceState::Unknown(gateway_ep.id()),
        |properties| {
            if properties.addresses.as_ref()?.iter().any(|a| a.addr == Some(GATEWAY_IP_V6)) {
                Some(())
            } else {
                None
            }
        },
    )
    .await
    .context("failed to observe gateway IPv6 address assignment")?;

    let gateway_mac = fidl_fuchsia_net::MacAddress {
        octets: gateway
            .connect_to_service::<fidl_fuchsia_net_stack::StackMarker>()
            .context("failed to connect to gateway stack")?
            .get_interface_info(gateway_ep.id())
            .await
            .squash_result()
            .context("get interface info error")?
            .properties
            .mac
            .ok_or_else(|| anyhow::anyhow!("can't get gateway MAC"))?
            .octets,
    };

    let routes = host
        .connect_to_service::<fidl_fuchsia_net_routes::StateMarker>()
        .context("failed to connect to routes/State")?;
    let routes = &routes;

    let resolve = move |mut remote: fidl_fuchsia_net::IpAddress| async move {
        Result::Ok(
            routes
                .resolve(&mut remote)
                .await
                .context("resolve FIDL error")?
                .map_err(fuchsia_zircon::Status::from_raw)
                .context("resolve error")?,
        )
    };

    let resolve_fails = move |mut remote: fidl_fuchsia_net::IpAddress| async move {
        assert_eq!(
            routes
                .resolve(&mut remote)
                .await
                .context("resolve FIDL error")?
                .map_err(fuchsia_zircon::Status::from_raw),
            Err(fuchsia_zircon::Status::ADDRESS_UNREACHABLE)
        );
        Result::Ok(())
    };

    let interface_id = host_ep.id();
    let host_netstack = &host_netstack;

    let do_test = |gateway: fidl_fuchsia_net::IpAddress,
                   unreachable_peer: fidl_fuchsia_net::IpAddress,
                   unspecified: fidl_fuchsia_net::IpAddress,
                   public_ip: fidl_fuchsia_net::IpAddress| async move {
        let gateway_node = || fidl_fuchsia_net_routes::Destination {
            address: Some(gateway),
            mac: Some(gateway_mac),
            interface_id: Some(interface_id),
        };

        // Start asking for a route for something that is directly accessible on the
        // network.
        let resolved = resolve(gateway).await.context("can't resolve peer")?;
        assert_eq!(resolved, fidl_fuchsia_net_routes::Resolved::Direct(gateway_node()));
        // Fails if MAC unreachable.
        let () =
            resolve_fails(unreachable_peer).await.context("error resolving unreachable peer")?;
        // Fails if route unreachable.
        let () = resolve_fails(public_ip).await.context("error resolving without route")?;

        // Install a default route and try to resolve through the gateway.
        let (route_transaction, route_transaction_server_end) =
            fidl::endpoints::create_proxy::<fidl_fuchsia_netstack::RouteTableTransactionMarker>()
                .context("failed to create route table transaction")?;
        let () = fuchsia_zircon::ok(
            host_netstack
                .start_route_table_transaction(route_transaction_server_end)
                .await
                .context("start_route_table_transaction FIDL error")?,
        )
        .context("start_route_table_transaction error")?;
        let () = fuchsia_zircon::ok(
            route_transaction
                .add_route(&mut fidl_fuchsia_netstack::RouteTableEntry2 {
                    destination: unspecified,
                    netmask: unspecified,
                    gateway: Some(Box::new(gateway)),
                    nicid: u32::try_from(interface_id).context("interface ID doesn't fit u32")?,
                    metric: 100,
                })
                .await
                .context("add_route FIDL error")?,
        )
        .context("add_route error")?;

        // Resolve a public IP again and check that we get the gateway response.
        let resolved = resolve(public_ip).await.context("can't resolve through gateway")?;
        assert_eq!(resolved, fidl_fuchsia_net_routes::Resolved::Gateway(gateway_node()));
        // And that the unspecified address resolves to the gateway node as well.
        let resolved = resolve(unspecified).await.context("can't resolve unspecified address")?;
        assert_eq!(resolved, fidl_fuchsia_net_routes::Resolved::Gateway(gateway_node()));

        Result::Ok(())
    };

    let () =
        do_test(GATEWAY_IP_V4.addr, fidl_ip!(192.168.0.3), fidl_ip!(0.0.0.0), fidl_ip!(8.8.8.8))
            .await
            .context("IPv4 route lookup failed")?;

    let () = do_test(
        GATEWAY_IP_V6.addr,
        fidl_ip!(3080::3),
        fidl_ip!(::),
        fidl_ip!(2001:4860:4860::8888),
    )
    .await
    .context("IPv6 route lookup failed")?;

    Ok(())
}
