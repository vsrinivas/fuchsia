// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use anyhow::Context as _;
use fidl_fuchsia_net_stack_ext::FidlReturn as _;
use net_declare::{fidl_ip, fidl_mac, fidl_subnet};
use netstack_testing_common::environments::{Netstack2, TestSandboxExt as _};
use netstack_testing_common::Result;
use std::convert::TryFrom as _;

async fn resolve(
    routes: &fidl_fuchsia_net_routes::StateProxy,
    mut remote: fidl_fuchsia_net::IpAddress,
) -> Result<fidl_fuchsia_net_routes::Resolved> {
    routes
        .resolve(&mut remote)
        .await
        .context("routes/State.Resolve FIDL error")?
        .map_err(fuchsia_zircon::Status::from_raw)
        .context("routes/State.Resolve error")
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_resolve_loopback_route() -> Result {
    let sandbox = netemul::TestSandbox::new().context("failed to create sandbox")?;
    let env = sandbox
        .create_netstack_environment::<Netstack2, _>("resolve_loopback_route")
        .context("failed to create environment")?;
    let routes = env
        .connect_to_service::<fidl_fuchsia_net_routes::StateMarker>()
        .context("failed to connect to routes/State")?;
    let routes = &routes;

    let test = |remote: fidl_fuchsia_net::IpAddress, source: fidl_fuchsia_net::IpAddress| async move {
        assert_eq!(
            resolve(routes, remote).await.context("error resolving remote")?,
            fidl_fuchsia_net_routes::Resolved::Direct(fidl_fuchsia_net_routes::Destination {
                address: Some(remote),
                mac: None,
                interface_id: Some(1),
                source_address: Some(source),
                ..fidl_fuchsia_net_routes::Destination::EMPTY
            }),
        );
        Result::Ok(())
    };

    let () = test(fidl_ip!("127.0.0.1"), fidl_ip!("127.0.0.1"))
        .await
        .context("error testing resolution for IPv4 loopback")?;
    let () = test(fidl_ip!("::1"), fidl_ip!("::1"))
        .await
        .context("error testing resolution for IPv6 loopback")?;

    Ok(())
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_resolve_route() -> Result {
    const HOST_IP_V4: fidl_fuchsia_net::Subnet = fidl_subnet!("192.168.0.2/24");
    const GATEWAY_IP_V4: fidl_fuchsia_net::Subnet = fidl_subnet!("192.168.0.1/24");
    const GATEWAY_IP_V6: fidl_fuchsia_net::Subnet = fidl_subnet!("3080::1/64");
    const HOST_IP_V6: fidl_fuchsia_net::Subnet = fidl_subnet!("3080::2/64");

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
            &netemul::InterfaceConfig::StaticIp(HOST_IP_V4),
        )
        .await
        .context("host failed to join network")?;
    let () = host_ep.add_ip_addr(HOST_IP_V6).await.context("failed to add IPv6 address to host")?;
    let () = fidl_fuchsia_net_interfaces_ext::wait_interface_with_id(
        fidl_fuchsia_net_interfaces_ext::event_stream_from_state(&host_interface_state)?,
        &mut fidl_fuchsia_net_interfaces_ext::InterfaceState::Unknown(host_ep.id()),
        |fidl_fuchsia_net_interfaces_ext::Properties { addresses, .. }| {
            // TODO(https://github.com/rust-lang/rust/issues/64260): use bool::then when we're on Rust 1.50.0.
            if addresses
                .iter()
                .any(|&fidl_fuchsia_net_interfaces_ext::Address { addr }| addr == HOST_IP_V6)
            {
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
            &netemul::InterfaceConfig::StaticIp(GATEWAY_IP_V4),
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
        |fidl_fuchsia_net_interfaces_ext::Properties { addresses, .. }| {
            // TODO(https://github.com/rust-lang/rust/issues/64260): use bool::then when we're on Rust 1.50.0.
            if addresses
                .iter()
                .any(|&fidl_fuchsia_net_interfaces_ext::Address { addr }| addr == GATEWAY_IP_V6)
            {
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
                   public_ip: fidl_fuchsia_net::IpAddress,
                   source_address: fidl_fuchsia_net::IpAddress| async move {
        let gateway_node = || fidl_fuchsia_net_routes::Destination {
            address: Some(gateway),
            mac: Some(gateway_mac),
            interface_id: Some(interface_id),
            source_address: Some(source_address),
            ..fidl_fuchsia_net_routes::Destination::EMPTY
        };

        // Start asking for a route for something that is directly accessible on the
        // network.
        let resolved = resolve(routes, gateway).await.context("can't resolve peer")?;
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
        let resolved = resolve(routes, public_ip).await.context("can't resolve through gateway")?;
        assert_eq!(resolved, fidl_fuchsia_net_routes::Resolved::Gateway(gateway_node()));
        // And that the unspecified address resolves to the gateway node as well.
        let resolved =
            resolve(routes, unspecified).await.context("can't resolve unspecified address")?;
        assert_eq!(resolved, fidl_fuchsia_net_routes::Resolved::Gateway(gateway_node()));

        Result::Ok(())
    };

    let () = do_test(
        GATEWAY_IP_V4.addr,
        fidl_ip!("192.168.0.3"),
        fidl_ip!("0.0.0.0"),
        fidl_ip!("8.8.8.8"),
        HOST_IP_V4.addr,
    )
    .await
    .context("IPv4 route lookup failed")?;

    let () = do_test(
        GATEWAY_IP_V6.addr,
        fidl_ip!("3080::3"),
        fidl_ip!("::"),
        fidl_ip!("2001:4860:4860::8888"),
        HOST_IP_V6.addr,
    )
    .await
    .context("IPv6 route lookup failed")?;

    Ok(())
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_resolve_default_route_while_dhcp_is_running() -> Result {
    let sandbox = netemul::TestSandbox::new().context("failed to create sandbox")?;
    let net = sandbox.create_network("net").await.context("failed to create network")?;

    // Configure a host.
    let env = sandbox
        .create_netstack_environment::<Netstack2, _>("resolve_route_host")
        .context("failed to create client environment")?;

    let netstack = env
        .connect_to_service::<fidl_fuchsia_netstack::NetstackMarker>()
        .context("failed to connect to netstack")?;

    let ep = env
        .join_network::<netemul::NetworkDevice, _>(&net, "host", &netemul::InterfaceConfig::Dhcp)
        .await
        .context("host failed to join network")?;

    let routes = env
        .connect_to_service::<fidl_fuchsia_net_routes::StateMarker>()
        .context("failed to connect to routes/State")?;

    let resolved = routes
        .resolve(&mut fidl_ip!("0.0.0.0"))
        .await
        .context("routes/State.Resolve FIDL error")?
        .map_err(fuchsia_zircon::Status::from_raw);

    assert_eq!(resolved, Err(fuchsia_zircon::Status::ADDRESS_UNREACHABLE));

    const EP_ADDR: fidl_fuchsia_net::Subnet = fidl_subnet!("192.168.0.3/24");
    const GATEWAY_ADDR: fidl_fuchsia_net::IpAddress = fidl_ip!("192.168.0.1");
    const GATEWAY_MAC: fidl_fuchsia_net::MacAddress = fidl_mac!("02:01:02:03:04:05");
    const UNSPECIFIED_IP: fidl_fuchsia_net::IpAddress = fidl_ip!("0.0.0.0");

    // Configure stack statically with an address and a default route while DHCP is still running.
    let () = ep.add_ip_addr(EP_ADDR).await.context("failed to add address")?;

    let neigh = env
        .connect_to_service::<fidl_fuchsia_net_neighbor::ControllerMarker>()
        .context("failed to connect to neighbor API")?;
    let () = neigh
        .add_entry(ep.id(), &mut GATEWAY_ADDR.clone(), &mut GATEWAY_MAC.clone())
        .await
        .context("add_entry FIDL error")?
        .map_err(fuchsia_zircon::Status::from_raw)
        .context("add_entry error")?;

    // Install a default route and try to resolve through the gateway.
    let (route_transaction, route_transaction_server_end) =
        fidl::endpoints::create_proxy::<fidl_fuchsia_netstack::RouteTableTransactionMarker>()
            .context("failed to create route table transaction")?;
    let () = fuchsia_zircon::ok(
        netstack
            .start_route_table_transaction(route_transaction_server_end)
            .await
            .context("start_route_table_transaction FIDL error")?,
    )
    .context("start_route_table_transaction error")?;
    let () = fuchsia_zircon::ok(
        route_transaction
            .add_route(&mut fidl_fuchsia_netstack::RouteTableEntry2 {
                destination: UNSPECIFIED_IP,
                netmask: UNSPECIFIED_IP,
                gateway: Some(Box::new(GATEWAY_ADDR)),
                nicid: u32::try_from(ep.id()).context("interface ID doesn't fit u32")?,
                metric: 100,
            })
            .await
            .context("add_route FIDL error")?,
    )
    .context("add_route error")?;

    let resolved = routes
        .resolve(&mut UNSPECIFIED_IP.clone())
        .await
        .context("routes/State.Resolve FIDL error")?
        .map_err(fuchsia_zircon::Status::from_raw);

    assert_eq!(
        resolved,
        Ok(fidl_fuchsia_net_routes::Resolved::Gateway(fidl_fuchsia_net_routes::Destination {
            address: Some(GATEWAY_ADDR),
            mac: Some(GATEWAY_MAC),
            interface_id: Some(ep.id()),
            source_address: Some(EP_ADDR.addr),
            ..fidl_fuchsia_net_routes::Destination::EMPTY
        }))
    );
    Ok(())
}
