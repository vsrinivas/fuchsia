// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use std::{collections::HashMap, convert::TryFrom as _};

use fuchsia_async::{DurationExt as _, TimeoutExt as _};

use anyhow::anyhow;
use assert_matches::assert_matches;
use futures::{SinkExt as _, StreamExt as _, TryFutureExt as _};
use net_declare::{fidl_subnet, std_socket_addr_v4};
use netstack_testing_common::{
    interfaces, ping as ping_helper,
    realms::{Netstack2, TestSandboxExt as _},
    ASYNC_EVENT_NEGATIVE_CHECK_TIMEOUT,
};
use netstack_testing_macros::variants_test;
use test_case::test_case;

#[derive(Debug, Copy, Clone, Eq, Hash, PartialEq)]
enum Link {
    A,
    B,
}

impl std::fmt::Display for Link {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> Result<(), std::fmt::Error> {
        match self {
            Self::A => write!(f, "A"),
            Self::B => write!(f, "B"),
        }
    }
}

impl Link {
    fn id(&self) -> u8 {
        match self {
            Self::A => 1,
            Self::B => 2,
        }
    }
}

enum Step {
    Bridge(Vec<Link>),
    Reenable(Link),
    FlapLink(Link),
}

// A basic bridging test: creates N+2 netstacks; the 2 netstacks that are
// always present are named `gateway` (not actually a gateway since it
// doesn't route packets to the outside) and `switch` (not actually a switch,
// but just a Netstack on which interfaces are bridged together).  `switch`
// and `gateway` are connected via a network. In the simplest testcase, a
// single other Netstack is created (named `hostA`), connected to `switch`
// via a network. The interfaces on `switch` are then bridged, and the test
// verifies that `host` can communicate with `switch` and `gateway`.
//
// The other test steps include disabling-and-reenabling, and flapping the
// link of an interface attached to a bridge and ensuring that the bridge
// functions correctly afterwards.
#[variants_test]
#[test_case(
    "none",
    &[Step::Bridge(vec![])];
    "none")]
#[test_case(
    "one",
    &[Step::Bridge(vec![Link::A])];
    "one")]
#[test_case(
    "two",
    &[Step::Bridge(vec![Link::A, Link::B])];
    "two")]
#[test_case(
    "add",
    &[Step::Bridge(vec![]), Step::Bridge(vec![Link::A])];
    "add")]
#[test_case(
    "remove",
    &[Step::Bridge(vec![Link::A]), Step::Bridge(vec![])];
    "remove")]
#[test_case(
    "reenable",
    &[Step::Bridge(vec![Link::A]), Step::Reenable(Link::A)];
    "reenable")]
#[test_case(
    "link_flap",
    &[Step::Bridge(vec![Link::A]), Step::FlapLink(Link::A)];
    "link_flap")]
async fn test<E: netemul::Endpoint>(name: &str, sub_name: &str, steps: &[Step]) {
    let sandbox = netemul::TestSandbox::new().expect("failed to create sandbox");
    let gateway_realm = sandbox
        .create_netstack_realm::<Netstack2, _>(format!("{}_{}_gateway", name, sub_name))
        .expect("failed to create gateway netstack realm");
    let net_switch_gateway = sandbox
        .create_network("net_switch_gateway")
        .await
        .expect("failed to create network between switch and gateway");
    let gateway_if = gateway_realm
        .join_network::<E, _>(
            &net_switch_gateway,
            "gateway_ep",
            &netemul::InterfaceConfig::StaticIp(fidl_subnet!("192.168.255.1/16")),
        )
        .await
        .expect("failed to join network in gateway realm");

    let switch_realm = sandbox
        .create_netstack_realm::<Netstack2, _>(format!("{}_{}_switch", name, sub_name))
        .expect("failed to create switch netstack realm");
    let switch_if = switch_realm
        .join_network::<E, _>(
            &net_switch_gateway,
            "switch_ep",
            &netemul::InterfaceConfig::StaticIp(fidl_subnet!("192.168.254.1/16")),
        )
        .await
        .expect("failed to join network in switch realm");

    let gateway_node =
        ping_helper::Node::new_with_v4_and_v6_link_local(&gateway_realm, &gateway_if)
            .await
            .expect("failed to construct gateway node");
    {
        let switch_node =
            ping_helper::Node::new_with_v4_and_v6_link_local(&switch_realm, &switch_if)
                .await
                .expect("failed to construct switch node");
        let () = gateway_node
            .ping_pairwise(std::slice::from_ref(&switch_node))
            .await
            .expect("failed to ping between gateway and switch");
    }

    let mut ports = HashMap::new();
    struct Host<'a> {
        realm: netemul::TestRealm<'a>,
        _net: netemul::TestNetwork<'a>,
        switch_if: netemul::TestInterface<'a>,
        host_if: netemul::TestInterface<'a>,
    }
    let switch_netstack = switch_realm
        .connect_to_protocol::<fidl_fuchsia_netstack::NetstackMarker>()
        .expect("failed to connect to netstack in switch realm");
    let switch_stack = switch_realm
        .connect_to_protocol::<fidl_fuchsia_net_stack::StackMarker>()
        .expect("failed to connect to stack in switch realm");
    let switch_debug = switch_realm
        .connect_to_protocol::<fidl_fuchsia_net_debug::InterfacesMarker>()
        .expect("failed to connect to fuchsia.net.debug/Interfaces in switch realm");
    let switch_interfaces_state = switch_realm
        .connect_to_protocol::<fidl_fuchsia_net_interfaces::StateMarker>()
        .expect("failed to connect to fuchsia.net.interfaces/State in switch realm");
    let mut bridge = None;

    for step in steps {
        match step {
            Step::Bridge(links) => {
                match bridge.take() {
                    Some(bridge_id) => {
                        let () = switch_stack
                            .del_ethernet_interface(u64::from(bridge_id))
                            .await
                            .expect("FIDL error deleting bridge")
                            .expect("failed to delete bridge");

                        let switch_node = ping_helper::Node::new_with_v4_and_v6_link_local(
                            &switch_realm,
                            &switch_if,
                        )
                        .await
                        .expect("failed to construct switch node");
                        let () = gateway_node
                            .ping_pairwise(std::slice::from_ref(&switch_node))
                            .await
                            .expect("failed to ping between gateway and switch");
                    }
                    None => {}
                }
                ports.retain(|link, _| links.contains(link));
                for &link in links {
                    match ports.entry(link) {
                        std::collections::hash_map::Entry::Occupied(_) => {}
                        std::collections::hash_map::Entry::Vacant(vacant) => {
                            // Create a new netstack and a new network between it and the host.
                            let realm = sandbox
                                .create_netstack_realm::<Netstack2, _>(format!(
                                    "{}_{}_host{}",
                                    name, sub_name, link
                                ))
                                .expect("failed to create host netstack realm");
                            let net = sandbox
                                .create_network(format!("net{}", link))
                                .await
                                .expect("failed to create network between host and switch");
                            let host_if = realm
                                .join_network::<E, _>(
                                    &net,
                                    format!("host{}", link),
                                    &netemul::InterfaceConfig::StaticIp(fidl_fuchsia_net::Subnet {
                                        addr: fidl_fuchsia_net::IpAddress::Ipv4(
                                            fidl_fuchsia_net::Ipv4Address {
                                                addr: [192, 168, link.id(), 1],
                                            },
                                        ),
                                        prefix_len: 16,
                                    }),
                                )
                                .await
                                .expect("failed to join network in host realm");
                            let switch_if = switch_realm
                                .join_network::<E, _>(
                                    &net,
                                    format!("switch_ep{}", link),
                                    &netemul::InterfaceConfig::None,
                                )
                                .await
                                .expect("failed to join network in switch realm");

                            let _: &mut Host<'_> =
                                vacant.insert(Host { realm, _net: net, host_if, switch_if });
                        }
                    }
                }

                let bridge_interface_ids = std::iter::once(switch_if.id())
                    .chain(
                        ports
                            .values()
                            .map(|Host { switch_if, realm: _, _net, host_if: _ }| switch_if.id()),
                    )
                    .map(|id| u32::try_from(id).expect("interface ID does not fit into u32"))
                    .collect::<Vec<_>>();

                // Create the bridge.
                let bridge_id = switch_netstack
                    .bridge_interfaces(bridge_interface_ids.as_ref())
                    .await
                    .map_err(anyhow::Error::new)
                    .and_then(|result| match result {
                        fidl_fuchsia_netstack::Result_::Message(message) => {
                            Err(anyhow::Error::msg(message))
                        }
                        fidl_fuchsia_netstack::Result_::Nicid(id) => Ok(id),
                    })
                    .expect("failed to create bridge");
                bridge = Some(bridge_id);
                let (switch_interface_control, server_end) = fidl::endpoints::create_proxy::<
                    fidl_fuchsia_net_interfaces_admin::ControlMarker,
                >()
                .expect("failed to create fuchsia.net.interfaces.admin/Control proxy");
                let () = switch_debug
                    .get_admin(u64::from(bridge_id), server_end)
                    .expect("FIDL error initializing bridge interface control");
                let switch_interface_control =
                    fidl_fuchsia_net_interfaces_ext::admin::Control::new(switch_interface_control);
                let did_enable =
                    switch_interface_control.enable().await.expect("send enable").expect("enable");
                assert!(did_enable);
                let addr = fidl_fuchsia_net::Ipv4Address {
                    addr: [
                        192,
                        168,
                        254,
                        u8::try_from(bridge_id).expect("bridge interface ID does not fit into u8"),
                    ],
                };
                let prefix_len = 16;
                let address_state_provider = interfaces::add_address_wait_assigned(
                    &switch_interface_control,
                    fidl_fuchsia_net::InterfaceAddress::Ipv4(
                        fidl_fuchsia_net::Ipv4AddressWithPrefix { addr, prefix_len },
                    ),
                    fidl_fuchsia_net_interfaces_admin::AddressParameters::EMPTY,
                )
                .await
                .expect("add IPv4 address to bridge failed");
                let () = address_state_provider
                    .detach()
                    .expect("failed to detach from bridge interface address state provider");

                let () = switch_stack
                    .add_forwarding_entry(&mut fidl_fuchsia_net_stack::ForwardingEntry {
                        subnet: fidl_fuchsia_net_ext::apply_subnet_mask(fidl_fuchsia_net::Subnet {
                            addr: fidl_fuchsia_net::IpAddress::Ipv4(addr),
                            prefix_len,
                        }),
                        device_id: bridge_id.into(),
                        next_hop: None,
                        metric: 0,
                    })
                    .await
                    .expect("FIDL error adding subnet route to bridge")
                    .expect("error adding subnet route to bridge");
            }
            Step::Reenable(id) => {
                let Host { switch_if, realm: _, _net, host_if: _ } =
                    ports.get(&id).expect("port to reenable doesn't exist");
                let did_disable =
                    switch_if.control().disable().await.expect("send disable").expect("disable");
                assert!(did_disable);
                let did_enable =
                    switch_if.control().enable().await.expect("send enable").expect("enable");
                assert!(did_enable);
            }
            Step::FlapLink(id) => {
                let Host { switch_if, realm: _, _net, host_if: _ } =
                    ports.get(&id).expect("port to flap link doesn't exist");
                let () = switch_if
                    .set_link_up(false)
                    .await
                    .expect("failed to set link to down on bridged port");
                let () = switch_if
                    .set_link_up(true)
                    .await
                    .expect("failed to set link to up on bridged port");
            }
        }
        let bridge_id = bridge.expect("bridge ID not present");
        let nodes = futures::stream::once({
            async {
                // NB: Waiting for addresses on the bridge cannot use the
                // methods on `TestInterface` because the bridge interface
                // was created manually.
                let (v4, v6) = interfaces::wait_for_v4_and_v6_ll(
                    &switch_interfaces_state,
                    u64::from(bridge_id),
                )
                .await
                .expect("failed to wait for IPv4 and IPv6 link-local addresses");
                ping_helper::Node::new(&switch_realm, u64::from(bridge_id), vec![v4], vec![v6])
            }
        })
        .chain(futures::stream::iter(ports.values()).then(
            |Host { realm, host_if, switch_if: _, _net }| async move {
                ping_helper::Node::new_with_v4_and_v6_link_local(&realm, &host_if)
                    .await
                    .expect("failed to construct node on host")
            },
        ))
        .collect::<Vec<_>>()
        .await;

        // Verify that the bridge is working
        let () = gateway_node.ping_pairwise(nodes.as_slice()).await.expect("failed to ping hosts");
    }
}

// Tests that an admin-disabled interface attached to a bridge is still
// disabled when the bridge is removed.
#[variants_test]
async fn test_remove_bridge_interface_disabled<E: netemul::Endpoint>(name: &str) {
    let sandbox = netemul::TestSandbox::new().expect("failed to create sandbox");
    let gateway_realm = sandbox
        .create_netstack_realm::<Netstack2, _>(format!("{}_gateway", name))
        .expect("failed to create gateway netstack realm");
    let net_switch_gateway = sandbox
        .create_network("net_switch_gateway")
        .await
        .expect("failed to create network between switch and gateway");
    let gateway_if = gateway_realm
        .join_network::<E, _>(
            &net_switch_gateway,
            "gateway_ep",
            &netemul::InterfaceConfig::StaticIp(fidl_subnet!("192.168.255.1/16")),
        )
        .await
        .expect("failed to join network in gateway realm");

    let switch_realm = sandbox
        .create_netstack_realm::<Netstack2, _>(format!("{}_switch", name))
        .expect("failed to create switch netstack realm");
    let switch_if = switch_realm
        .join_network::<E, _>(
            &net_switch_gateway,
            "switch_ep",
            &netemul::InterfaceConfig::StaticIp(fidl_subnet!("192.168.254.1/16")),
        )
        .await
        .expect("failed to join network to gateway in switch realm");

    let gateway_node =
        ping_helper::Node::new_with_v4_and_v6_link_local(&gateway_realm, &gateway_if)
            .await
            .expect("failed to construct gateway node");
    let switch_node = ping_helper::Node::new_with_v4_and_v6_link_local(&switch_realm, &switch_if)
        .await
        .expect("failed to construct switch node");
    let mut seq = 0;
    let mut gen_seq = move || {
        seq += 1;
        seq
    };
    let () = gateway_node
        .ping_pairwise(std::slice::from_ref(&switch_node))
        .await
        .expect("failed to ping between switch and gateway");

    // Create the bridge.
    let switch_netstack = switch_realm
        .connect_to_protocol::<fidl_fuchsia_netstack::NetstackMarker>()
        .expect("failed to connect to netstack in switch realm");
    let switch_stack = switch_realm
        .connect_to_protocol::<fidl_fuchsia_net_stack::StackMarker>()
        .expect("failed to connect to stack in switch realm");
    let bridge_id = switch_netstack
        .bridge_interfaces(&[
            u32::try_from(switch_if.id()).expect("switch interface ID doesn't fit into u32")
        ])
        .await
        .map_err(anyhow::Error::new)
        .and_then(|result| match result {
            fidl_fuchsia_netstack::Result_::Message(message) => Err(anyhow::Error::msg(message)),
            fidl_fuchsia_netstack::Result_::Nicid(id) => Ok(id),
        })
        .expect("failed to create bridge");

    // Disable the attached interface.
    let did_disable = switch_if.control().disable().await.expect("send disable").expect("disable");
    assert!(did_disable);

    // Destroy the bridge.
    let () = switch_stack
        .del_ethernet_interface(u64::from(bridge_id))
        .await
        .expect("FIDL error deleting bridge")
        .expect("failed to delete bridge");

    // Ensure that attempting to ping the switch results in a non-response.
    let () = gateway_realm
        .ping_once::<ping::Ipv4>(std_socket_addr_v4!("192.168.254.1:0"), gen_seq())
        .and_then(|()| futures::future::err(anyhow!("ping succeeded unexpectedly")))
        .on_timeout(ASYNC_EVENT_NEGATIVE_CHECK_TIMEOUT.after_now(), || Ok(()))
        .await
        .expect("error while asserting that gateway cannot ping switch");

    // Ensure that the interface that was detached from the bridge is still
    // disabled by asserting that it cannot be used to ping the gateway.
    let icmp_sock = switch_realm
        .icmp_socket::<ping::Ipv4>()
        .await
        .expect("failed to create ICMP socket in switch realm");
    let mut sink = ping::PingSink::<ping::Ipv4, _>::new(&icmp_sock);
    assert_matches!(
        sink.send(ping::PingData {
            addr: std_socket_addr_v4!("192.168.255.1:0"),
            sequence: gen_seq(),
            body: "hello, world".as_bytes().to_vec(),
        })
        .await
        .expect_err("send from socket succeeded unexpectedly"),
        // TODO(https://github.com/rust-lang/rust/issues/86442): Assert that
        // the `std::io::Error` contained within is of kind
        // `std::io::ErrorKind::HostUnreachable` once stable.
        ping::PingError::Send(_)
    );

    // Enable the detached interface.
    let did_enable = switch_if.control().enable().await.expect("send enable").expect("enable");
    assert!(did_enable);

    // Pings should work now.
    let switch_node = ping_helper::Node::new_with_v4_and_v6_link_local(&switch_realm, &switch_if)
        .await
        .expect("failed to construct switch node");
    let () = gateway_node
        .ping_pairwise(std::slice::from_ref(&switch_node))
        .await
        .expect("failed to ping between switch and gateway");
}
