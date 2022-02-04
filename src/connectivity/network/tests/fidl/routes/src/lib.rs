// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use anyhow::Context as _;
use net_declare::{fidl_ip, fidl_ip_v4, fidl_mac, fidl_subnet};
use netemul::Endpoint as _;
use netstack_testing_common::Result;
use netstack_testing_common::{
    interfaces,
    realms::{Netstack2, TestSandboxExt as _},
};

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
async fn test_resolve_loopback_route() {
    let sandbox = netemul::TestSandbox::new().expect("failed to create sandbox");
    let realm = sandbox
        .create_netstack_realm::<Netstack2, _>("resolve_loopback_route")
        .expect("failed to create realm");
    let routes = realm
        .connect_to_protocol::<fidl_fuchsia_net_routes::StateMarker>()
        .expect("failed to connect to routes/State");
    let routes = &routes;

    let test = |remote: fidl_fuchsia_net::IpAddress, source: fidl_fuchsia_net::IpAddress| async move {
        assert_eq!(
            resolve(routes, remote).await.expect("error resolving remote"),
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
        .expect("error testing resolution for IPv4 loopback");
    let () = test(fidl_ip!("::1"), fidl_ip!("::1"))
        .await
        .expect("error testing resolution for IPv6 loopback");
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_resolve_route() {
    const GATEWAY_IP_V4: fidl_fuchsia_net::Subnet = fidl_subnet!("192.168.0.1/24");
    const GATEWAY_IP_V6: fidl_fuchsia_net::Subnet = fidl_subnet!("3080::1/64");
    const GATEWAY_MAC: fidl_fuchsia_net::MacAddress = fidl_mac!("02:01:02:03:04:05");
    const HOST_IP_V4: fidl_fuchsia_net::Subnet = fidl_subnet!("192.168.0.2/24");
    const HOST_IP_V6: fidl_fuchsia_net::Subnet = fidl_subnet!("3080::2/64");

    let sandbox = netemul::TestSandbox::new().expect("failed to create sandbox");
    let net = sandbox.create_network("net").await.expect("failed to create network");

    // Configure a host.
    let host = sandbox
        .create_netstack_realm::<Netstack2, _>("resolve_route_host")
        .expect("failed to create client realm");

    let host_stack = host
        .connect_to_protocol::<fidl_fuchsia_net_stack::StackMarker>()
        .expect("failed to connect to netstack");

    let host_ep = host
        .join_network::<netemul::NetworkDevice, _>(
            &net,
            "host",
            &netemul::InterfaceConfig::StaticIp(HOST_IP_V4),
        )
        .await
        .expect("host failed to join network");
    let _host_address_state_provider = interfaces::add_subnet_address_and_route_wait_assigned(
        &host_ep,
        HOST_IP_V6,
        fidl_fuchsia_net_interfaces_admin::AddressParameters::EMPTY,
    )
    .await
    .expect("add subnet address and route");

    // Configure a gateway.
    let gateway = sandbox
        .create_netstack_realm::<Netstack2, _>("resolve_route_gateway")
        .expect("failed to create server realm");

    let gateway_ep = gateway
        .join_network_with(
            &net,
            "gateway",
            netemul::NetworkDevice::make_config(netemul::DEFAULT_MTU, Some(GATEWAY_MAC)),
            &netemul::InterfaceConfig::StaticIp(GATEWAY_IP_V4),
        )
        .await
        .expect("gateway failed to join network");
    let _gateway_address_state_provider = interfaces::add_subnet_address_and_route_wait_assigned(
        &gateway_ep,
        GATEWAY_IP_V6,
        fidl_fuchsia_net_interfaces_admin::AddressParameters::EMPTY,
    )
    .await
    .expect("add subnet address and route");

    let routes = host
        .connect_to_protocol::<fidl_fuchsia_net_routes::StateMarker>()
        .expect("failed to connect to routes/State");
    let routes = &routes;

    let resolve_fails = move |mut remote: fidl_fuchsia_net::IpAddress| async move {
        assert_eq!(
            routes
                .resolve(&mut remote)
                .await
                .expect("resolve FIDL error")
                .map_err(fuchsia_zircon::Status::from_raw),
            Err(fuchsia_zircon::Status::ADDRESS_UNREACHABLE)
        );
        Result::Ok(())
    };

    let interface_id = host_ep.id();
    let host_stack = &host_stack;

    let do_test = |gateway: fidl_fuchsia_net::IpAddress,
                   unreachable_peer: fidl_fuchsia_net::IpAddress,
                   unspecified: fidl_fuchsia_net::IpAddress,
                   public_ip: fidl_fuchsia_net::IpAddress,
                   source_address: fidl_fuchsia_net::IpAddress| async move {
        let gateway_node = || fidl_fuchsia_net_routes::Destination {
            address: Some(gateway),
            mac: Some(GATEWAY_MAC),
            interface_id: Some(interface_id),
            source_address: Some(source_address),
            ..fidl_fuchsia_net_routes::Destination::EMPTY
        };

        // Start asking for a route for something that is directly accessible on the
        // network.
        let resolved = resolve(routes, gateway).await.expect("can't resolve peer");
        assert_eq!(resolved, fidl_fuchsia_net_routes::Resolved::Direct(gateway_node()));
        // Fails if MAC unreachable.
        let () = resolve_fails(unreachable_peer).await.expect("error resolving unreachable peer");
        // Fails if route unreachable.
        let () = resolve_fails(public_ip).await.expect("error resolving without route");

        // Install a default route and try to resolve through the gateway.
        let () = host_stack
            .add_forwarding_entry(&mut fidl_fuchsia_net_stack::ForwardingEntry {
                subnet: fidl_fuchsia_net::Subnet { addr: unspecified, prefix_len: 0 },
                device_id: interface_id,
                next_hop: Some(Box::new(gateway)),
                metric: 100,
            })
            .await
            .expect("call add_route")
            .expect("add route");

        // Resolve a public IP again and check that we get the gateway response.
        let resolved = resolve(routes, public_ip).await.expect("can't resolve through gateway");
        assert_eq!(resolved, fidl_fuchsia_net_routes::Resolved::Gateway(gateway_node()));
        // And that the unspecified address resolves to the gateway node as well.
        let resolved =
            resolve(routes, unspecified).await.expect("can't resolve unspecified address");
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
    .expect("IPv4 route lookup failed");

    let () = do_test(
        GATEWAY_IP_V6.addr,
        fidl_ip!("3080::3"),
        fidl_ip!("::"),
        fidl_ip!("2001:4860:4860::8888"),
        HOST_IP_V6.addr,
    )
    .await
    .expect("IPv6 route lookup failed");
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_resolve_default_route_while_dhcp_is_running() {
    let sandbox = netemul::TestSandbox::new().expect("failed to create sandbox");
    let net = sandbox.create_network("net").await.expect("failed to create network");

    // Configure a host.
    let realm = sandbox
        .create_netstack_realm::<Netstack2, _>("resolve_route_host")
        .expect("failed to create client realm");

    let stack = realm
        .connect_to_protocol::<fidl_fuchsia_net_stack::StackMarker>()
        .expect("failed to connect to netstack");

    let ep = realm
        .join_network::<netemul::NetworkDevice, _>(&net, "host", &netemul::InterfaceConfig::Dhcp)
        .await
        .expect("host failed to join network");

    let routes = realm
        .connect_to_protocol::<fidl_fuchsia_net_routes::StateMarker>()
        .expect("failed to connect to routes/State");

    let resolved = routes
        .resolve(&mut fidl_ip!("0.0.0.0"))
        .await
        .expect("routes/State.Resolve FIDL error")
        .map_err(fuchsia_zircon::Status::from_raw);

    assert_eq!(resolved, Err(fuchsia_zircon::Status::ADDRESS_UNREACHABLE));

    const EP_ADDR: fidl_fuchsia_net::Ipv4Address = fidl_ip_v4!("192.168.0.3");
    const PREFIX_LEN: u8 = 24;
    const GATEWAY_ADDR: fidl_fuchsia_net::IpAddress = fidl_ip!("192.168.0.1");
    const GATEWAY_MAC: fidl_fuchsia_net::MacAddress = fidl_mac!("02:01:02:03:04:05");
    const UNSPECIFIED_IP: fidl_fuchsia_net::IpAddress = fidl_ip!("0.0.0.0");

    // Configure stack statically with an address and a default route while DHCP is still running.
    let _host_address_state_provider = interfaces::add_address_wait_assigned(
        ep.control(),
        fidl_fuchsia_net::InterfaceAddress::Ipv4(fidl_fuchsia_net::Ipv4AddressWithPrefix {
            addr: EP_ADDR,
            prefix_len: PREFIX_LEN,
        }),
        fidl_fuchsia_net_interfaces_admin::AddressParameters::EMPTY,
    )
    .await
    .expect("add address");

    let neigh = realm
        .connect_to_protocol::<fidl_fuchsia_net_neighbor::ControllerMarker>()
        .expect("failed to connect to neighbor API");
    let () = neigh
        .add_entry(ep.id(), &mut GATEWAY_ADDR.clone(), &mut GATEWAY_MAC.clone())
        .await
        .expect("add_entry FIDL error")
        .map_err(fuchsia_zircon::Status::from_raw)
        .expect("add_entry error");

    // Install a default route and try to resolve through the gateway.
    let () = stack
        .add_forwarding_entry(&mut fidl_fuchsia_net_stack::ForwardingEntry {
            subnet: fidl_fuchsia_net::Subnet { addr: UNSPECIFIED_IP, prefix_len: 0 },
            device_id: ep.id(),
            next_hop: Some(Box::new(GATEWAY_ADDR)),
            metric: 100,
        })
        .await
        .expect("call add_route")
        .expect("add route");

    let resolved = routes
        .resolve(&mut UNSPECIFIED_IP.clone())
        .await
        .expect("routes/State.Resolve FIDL error")
        .map_err(fuchsia_zircon::Status::from_raw);

    assert_eq!(
        resolved,
        Ok(fidl_fuchsia_net_routes::Resolved::Gateway(fidl_fuchsia_net_routes::Destination {
            address: Some(GATEWAY_ADDR),
            mac: Some(GATEWAY_MAC),
            interface_id: Some(ep.id()),
            source_address: Some(fidl_fuchsia_net::IpAddress::Ipv4(EP_ADDR)),
            ..fidl_fuchsia_net_routes::Destination::EMPTY
        }))
    );
}
