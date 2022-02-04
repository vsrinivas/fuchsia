// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use std::borrow::Cow;
use std::collections::HashMap;

use fidl::endpoints::Proxy as _;
use fidl_fuchsia_hardware_network as fhardware_network;
use fidl_fuchsia_net as fnet;
use fidl_fuchsia_net_interfaces as fnet_interfaces;
use fidl_fuchsia_net_interfaces_ext::{self as fnet_interfaces_ext};
use fidl_fuchsia_net_virtualization as fnet_virtualization;
use futures::StreamExt as _;
use log::info;
use net_declare::fidl_subnet;
use netstack_testing_common::{
    interfaces, ping,
    realms::{
        KnownServiceProvider, ManagementAgent, NetCfgVersion, Netstack2, TestSandboxExt as _,
    },
};
use netstack_testing_macros::variants_test;
use packet::ParseBuffer as _;
use test_case::test_case;

#[derive(Debug, Clone, Copy, Eq, Hash, PartialEq)]
enum Network {
    A,
    B,
}

#[derive(Debug, Clone, Copy, Eq, Hash, PartialEq)]
enum Interface {
    A,
    B,
}

impl Interface {
    fn id(&self) -> u8 {
        match self {
            Self::A => 1,
            Self::B => 2,
        }
    }
}

impl std::fmt::Display for Interface {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> Result<(), std::fmt::Error> {
        match self {
            Self::A => write!(f, "A"),
            Self::B => write!(f, "B"),
        }
    }
}

enum Step {
    AddUpstream,
    RemoveUpstream,
    EnableUpstream,
    DisableUpstream,
    AddNetwork(Network),
    AddInterface(Network, Interface),
    RemoveNetwork(Network),
    RemoveInterface(Network, Interface),
}

impl Step {
    // Returns whether the test step may result in the bridge being torn down
    // and reconstructed.
    fn may_reconstruct_bridge(&self) -> bool {
        match self {
            Self::AddUpstream
            | Self::AddInterface(_, _)
            | Self::RemoveNetwork(_)
            | Self::RemoveInterface(_, _) => true,
            Self::EnableUpstream
            | Self::DisableUpstream
            | Self::RemoveUpstream
            | Self::AddNetwork(_) => false,
        }
    }
}

struct Guest<'a> {
    interface_proxy: fnet_virtualization::InterfaceProxy,
    realm: netemul::TestRealm<'a>,
    guest_if: netemul::TestInterface<'a>,
    _net: netemul::TestNetwork<'a>,
    _ep: netemul::TestEndpoint<'a>,
}

impl<'a> std::fmt::Debug for Guest<'a> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::result::Result<(), std::fmt::Error> {
        let Self { realm, guest_if, _net, _ep, interface_proxy: _ } = self;
        f.debug_struct("Guest")
            .field("realm", realm)
            .field("guest_if", guest_if)
            .field("_net", _net)
            .field("_ep", _ep)
            .finish_non_exhaustive()
    }
}

impl<'a> Guest<'a> {
    async fn new<E: netemul::Endpoint, S: Into<Cow<'a, str>>>(
        sandbox: &'a netemul::TestSandbox,
        network_proxy: &fnet_virtualization::NetworkProxy,
        realm_name: S,
        interface: Interface,
        ipv4_addr: fnet::Subnet,
    ) -> Guest<'a> {
        let realm = sandbox
            .create_netstack_realm::<Netstack2, _>(realm_name)
            .expect("failed to create guest netstack realm");
        let net = sandbox
            .create_network(format!("net{}", interface))
            .await
            .expect("failed to create network between guest and host");
        let guest_if = realm
            .join_network::<E, _>(
                &net,
                format!("guest{}", interface),
                &netemul::InterfaceConfig::StaticIp(ipv4_addr),
            )
            .await
            .expect("failed to join network as guest");

        let ep = net
            .create_endpoint::<netemul::NetworkDevice, _>(format!("host{}", interface))
            .await
            .expect("failed to create endpoint on host realm connected to guest");
        let () = ep.set_link_up(true).await.expect("failed to enable endpoint");

        let (device, mut port_id) = ep.get_netdevice().await.expect("failed to get netdevice");
        let device =
            device.into_proxy().expect("fuchsia.hardware.network/Device into_proxy failed");
        let (port, server_end) =
            fidl::endpoints::create_endpoints::<fhardware_network::PortMarker>()
                .expect("failed to create fuchsia.hardware.network/Port endpoints");
        let () = device.get_port(&mut port_id, server_end).expect("get_port");

        let (interface_proxy, server_end) =
            fidl::endpoints::create_proxy::<fnet_virtualization::InterfaceMarker>()
                .expect("failed to create fuchsia.net.virtualization/Interface proxy");
        let () = network_proxy.add_port(port, server_end).expect("add_port");
        Self { interface_proxy, realm, _net: net, _ep: ep, guest_if }
    }
}

struct NetworkClient<'a> {
    network_proxy: fnet_virtualization::NetworkProxy,
    interface_map: HashMap<Interface, Guest<'a>>,
}

impl<'a> std::fmt::Debug for NetworkClient<'a> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::result::Result<(), std::fmt::Error> {
        let Self { interface_map, network_proxy: _ } = self;
        f.debug_struct("NetworkClient")
            .field("interface_map", interface_map)
            .finish_non_exhaustive()
    }
}

impl<'a> NetworkClient<'a> {
    fn new(network_proxy: fnet_virtualization::NetworkProxy) -> Self {
        Self { network_proxy, interface_map: HashMap::new() }
    }
}

fn create_bridged_network(
    virtualization_control: &fnet_virtualization::ControlProxy,
) -> fnet_virtualization::NetworkProxy {
    let (network_proxy, server_end) =
        fidl::endpoints::create_proxy::<fnet_virtualization::NetworkMarker>()
            .expect("failed to create fuchsia.net.virtualization/Network proxy");
    virtualization_control
        .create_network(
            &mut fnet_virtualization::Config::Bridged(fnet_virtualization::Bridged {
                ..fnet_virtualization::Bridged::EMPTY
            }),
            server_end,
        )
        .expect("create network");
    network_proxy
}

// Tests netcfg's implementation of fuchsia.net.virtualization.
//
// At the beginning of each test case, a netstack named `gateway` and a
// netstack named `host` are created, connected to each other. `host` runs
// netcfg-workstation, whose implementation of the FIDL is under test.
//
// Test steps include:
// - adding and removing an interface eligible for providing upstream
//   connectivity (note that adding multiple upstream-providing interfaces
//   is currently unsupported),
// - adding and removing networks, which are identified by variants of the
//   `Network` enum so that they can be referred to when adding/removing
//   interfaces to/from them, and
// - adding and removing interfaces, which are identified by variants of the
//   `Interface` enum so that they can be removed.
//
// Each netdevice client passed to `fuchsia.net.virtualization/Network.AddDevice`
// is backed by a netemul endpoint connected to a netstack running inside a
// realm. Such realms are named `guest`s as they are effectively the guest VM
// network stacks in real usage. After each test step, ensure that all
// `guest`s and the `gateway` can communicate with each other if there is a
// candidate for upstream present.
#[variants_test]
#[test_case(
    "basic",
    &[
        Step::AddUpstream,
        Step::AddNetwork(Network::A),
        Step::AddInterface(Network::A, Interface::A),
    ];
    "basic")]
#[test_case(
    "remove_network",
    &[
        Step::AddUpstream,
        Step::AddNetwork(Network::A),
        Step::AddInterface(Network::A, Interface::A),
        Step::AddNetwork(Network::B),
        Step::AddInterface(Network::B, Interface::B),
        Step::RemoveNetwork(Network::A),
    ];
    "remove_network")]
#[test_case(
    "remove_interface",
    &[
        Step::AddUpstream,
        Step::AddNetwork(Network::A),
        Step::AddInterface(Network::A, Interface::A),
        Step::AddInterface(Network::A, Interface::B),
        Step::RemoveInterface(Network::A, Interface::A),
    ];
    "remove_interface")]
#[test_case(
    "add_upstream",
    &[
        Step::AddNetwork(Network::A),
        Step::AddInterface(Network::A, Interface::A),
        Step::AddUpstream,
    ];
    "add_upstream")]
#[test_case(
    "remove_upstream",
    &[
        Step::AddUpstream,
        Step::AddNetwork(Network::A),
        Step::AddInterface(Network::A, Interface::A),
        Step::RemoveUpstream,
        Step::AddUpstream,
    ];
    "remove_upstream")]
#[test_case(
    "disable_upstream",
    &[
        Step::AddUpstream,
        Step::AddNetwork(Network::A),
        Step::AddInterface(Network::A, Interface::A),
        Step::DisableUpstream,
        Step::EnableUpstream,
    ];
    "disable_upstream")]
async fn virtualization<E: netemul::Endpoint>(name: &str, sub_name: &str, steps: &[Step]) {
    let () = fuchsia_syslog::init().expect("cannot init logger");
    let sandbox = netemul::TestSandbox::new().expect("failed to create sandbox");
    let gateway_realm = sandbox
        .create_netstack_realm::<Netstack2, _>(format!("{}_{}_gateway", name, sub_name))
        .expect("failed to create gateway netstack realm");
    let net_host_gateway = sandbox
        .create_network("net_host_gateway")
        .await
        .expect("failed to create network between host and gateway");
    let gateway_if = gateway_realm
        .join_network::<E, _>(
            &net_host_gateway,
            "ep_gateway",
            &netemul::InterfaceConfig::StaticIp(fidl_subnet!("192.168.255.1/16")),
        )
        .await
        .expect("failed to join network in gateway realm");

    let host_realm = sandbox
        .create_netstack_realm_with::<Netstack2, _, _>(
            format!("{}_{}_host", name, sub_name),
            &[
                KnownServiceProvider::Manager {
                    agent: ManagementAgent::NetCfg(NetCfgVersion::Advanced),
                    use_dhcp_server: false,
                    enable_dhcpv6: false,
                },
                KnownServiceProvider::DnsResolver,
                KnownServiceProvider::FakeClock,
            ],
        )
        .expect("failed to create host netstack realm");

    let host_interfaces_state = host_realm
        .connect_to_protocol::<fnet_interfaces::StateMarker>()
        .expect("connect to protocol");
    let virtualization_control = host_realm
        .connect_to_protocol::<fnet_virtualization::ControlMarker>()
        .expect("failed to connect to fuchsia.net.virtualization/Control in host realm");

    let mut networks = HashMap::new();
    let mut upstream_if = None;
    let mut bridge = None;

    for step in steps {
        match *step {
            Step::AddUpstream => {
                info!("adding upstream");
                match upstream_if.replace((
                    host_realm
                        .join_network::<E, _>(
                            &net_host_gateway,
                            "ep_host",
                            &netemul::InterfaceConfig::None,
                        )
                        .await
                        .expect("failed to join network in host realm"),
                    true,
                )) {
                    Some(upstream) => panic!(
                        "upstream interface already present when adding upstream: {:?}",
                        upstream
                    ),
                    None => {}
                }
            }
            Step::RemoveUpstream => {
                info!("removing upstream");
                let _: (netemul::TestInterface<'_>, bool) =
                    upstream_if.take().expect("upstream to be removed doesn't exist");
            }
            Step::DisableUpstream => {
                let (interface, _): (_, bool) =
                    upstream_if.take().expect("upstream to disable not present");
                let did_disable =
                    interface.control().disable().await.expect("send disable").expect("disable");
                assert!(did_disable);
                upstream_if = Some((interface, false));
            }
            Step::EnableUpstream => {
                let (interface, _): (_, bool) =
                    upstream_if.take().expect("upstream to enable not present");
                let did_enable =
                    interface.control().enable().await.expect("send enable").expect("enable");
                assert!(did_enable);
                upstream_if = Some((interface, true));
            }
            Step::AddNetwork(network) => {
                info!("adding network {:?}", network);
                match networks.entry(network) {
                    std::collections::hash_map::Entry::Occupied(occupied) => {
                        panic!("test step to add network but it already exists: {:?}", occupied);
                    }
                    std::collections::hash_map::Entry::Vacant(vacant) => {
                        let _: &mut NetworkClient<'_> = vacant.insert(NetworkClient::new(
                            create_bridged_network(&virtualization_control),
                        ));
                    }
                }
            }
            Step::AddInterface(network, interface) => {
                info!("adding interface {:?} to network {:?}", interface, network);
                let NetworkClient { network_proxy, interface_map } =
                    networks.get_mut(&network).unwrap_or_else(|| {
                        panic!("network {:?} to add interface to doesn't exist", network)
                    });
                match interface_map.entry(interface) {
                    std::collections::hash_map::Entry::Occupied(occupied) => {
                        panic!(
                            "test step to add interface to network {:?} but it already exists: {:?}",
                            network, occupied
                        );
                    }
                    std::collections::hash_map::Entry::Vacant(vacant) => {
                        // Create a new netstack and a new network between it and the host.
                        let _: &mut Guest<'_> = vacant.insert(
                            Guest::new::<E, _>(
                                &sandbox,
                                &network_proxy,
                                format!("{}_{}_guest{}", name, sub_name, interface),
                                interface,
                                fnet::Subnet {
                                    addr: fnet::IpAddress::Ipv4(fnet::Ipv4Address {
                                        addr: [192, 168, interface.id(), 1],
                                    }),
                                    prefix_len: 16,
                                },
                            )
                            .await,
                        );
                    }
                }
            }
            Step::RemoveNetwork(network) => {
                info!("removing network {:?}", network);
                let NetworkClient { network_proxy, interface_map } =
                    networks.remove(&network).unwrap_or_else(|| {
                        panic!("network {:?} to be removed does not exist", network)
                    });
                std::mem::drop(network_proxy);
                for (_, Guest { interface_proxy, realm: _, guest_if: _, _ep, _net }) in
                    interface_map.into_iter()
                {
                    match interface_proxy.on_closed().await {
                        Ok(signals) => {
                            if !signals.contains(fidl::handle::Signals::CHANNEL_PEER_CLOSED) {
                                panic!("signal did not contain PEER_CLOSED: {:?}", signals);
                            }
                        }
                        Err(status) => {
                            panic!("Interface proxy on_closed error: {}", status);
                        }
                    }
                }
            }
            Step::RemoveInterface(network, interface) => {
                info!("removing interface {:?} from network {:?}", interface, network);
                let NetworkClient { network_proxy: _, interface_map } =
                    networks.get_mut(&network).unwrap_or_else(|| {
                        panic!("network {:?} to remove interface from doesn't exist", network)
                    });
                let _: Guest<'_> = interface_map.remove(&interface).unwrap_or_else(|| {
                    panic!("interface {} to be removed does not exist", interface)
                });
            }
        }

        if let Some::<(netemul::TestInterface<'_>, _)>((_, upstream_online)) = upstream_if {
            if networks
                .values()
                .any(|NetworkClient { interface_map, network_proxy: _ }| !interface_map.is_empty())
            {
                if step.may_reconstruct_bridge() {
                    let mut interfaces_map = HashMap::new();
                    let bridge_id = fnet_interfaces_ext::wait_interface(
                        fnet_interfaces_ext::event_stream_from_state(&host_interfaces_state)
                            .expect("initialize interface event stream"),
                        &mut interfaces_map,
                        |interfaces_map| {
                            interfaces_map.values().find_map(
                                |&fnet_interfaces_ext::Properties {
                                     id,
                                     device_class,
                                     addresses: _,
                                     name: _,
                                     online: _,
                                     has_default_ipv4_route: _,
                                     has_default_ipv6_route: _,
                                 }| {
                                    match device_class {
                                        fnet_interfaces::DeviceClass::Device(
                                            fhardware_network::DeviceClass::Bridge,
                                        ) if Some(id) != bridge.as_ref().map(ping::Node::id) => {
                                            Some(id)
                                        }
                                        _ => None,
                                    }
                                },
                            )
                        },
                    )
                    .await
                    .expect("failed to wait for bridge interface");
                    let v6_ll = interfaces::wait_for_v6_ll(&host_interfaces_state, bridge_id)
                        .await
                        .expect("failed to wait for IPv6 link-local address on bridge");

                    bridge = Some(ping::Node::new(&host_realm, bridge_id, Vec::new(), vec![v6_ll]));
                }

                let nodes = futures::stream::iter(
                    networks
                        .values()
                        .map(|NetworkClient { interface_map, network_proxy: _ }| {
                            interface_map.values()
                        })
                        .flatten(),
                )
                .then(|Guest { realm, guest_if, _ep, interface_proxy: _, _net }| async move {
                    ping::Node::new_with_v4_and_v6_link_local(&realm, &guest_if)
                        .await
                        .expect("failed to construct guest node")
                })
                .chain(if upstream_online {
                    futures::stream::once(async {
                        ping::Node::new_with_v4_and_v6_link_local(&gateway_realm, &gateway_if)
                            .await
                            .expect("failed to construct gateway node")
                    })
                    .left_stream()
                } else {
                    futures::stream::empty().right_stream()
                })
                .collect::<Vec<_>>()
                .await;

                // Verify that the bridge is working
                let () = bridge
                    .as_ref()
                    .expect("bridge must be present")
                    .ping_pairwise(nodes.as_slice())
                    .await
                    .expect("failed to ping hosts");
            }
        }
    }
}

#[variants_test]
async fn dhcpv4_client_started<E: netemul::Endpoint>(name: &str) {
    let sandbox = netemul::TestSandbox::new().expect("failed to create sandbox");
    let host_realm = sandbox
        .create_netstack_realm_with::<Netstack2, _, _>(
            format!("{}_host", name),
            &[
                KnownServiceProvider::Manager {
                    agent: ManagementAgent::NetCfg(NetCfgVersion::Advanced),
                    use_dhcp_server: false,
                    enable_dhcpv6: false,
                },
                KnownServiceProvider::DnsResolver,
                KnownServiceProvider::FakeClock,
            ],
        )
        .expect("failed to create host netstack realm");
    let net = sandbox.create_network("net").await.expect("failed to create network");
    let fake_ep = net.create_fake_endpoint().expect("failed to create fake endpoint on net");
    let _upstream_if = host_realm
        .join_network::<E, _>(&net, "ep_host", &netemul::InterfaceConfig::None)
        .await
        .expect("failed to join network in host realm");

    // Create a virtualized network and attach a device.
    let virtualization_control = host_realm
        .connect_to_protocol::<fnet_virtualization::ControlMarker>()
        .expect("failed to connect to fuchsia.net.virtualization/Control in host realm");
    let network_proxy = create_bridged_network(&virtualization_control);
    let _guest = Guest::new::<E, _>(
        &sandbox,
        &network_proxy,
        "guest",
        Interface::A,
        fidl_subnet!("192.168.1.1/16"),
    )
    .await;

    // Expect a DHCPv4 packet.
    fake_ep
        .frame_stream()
        .filter_map(|r| {
            let (buf, dropped) = r.expect("fake_ep frame stream error");
            assert_eq!(dropped, 0);
            futures::future::ready(
                match packet_formats::testutil::parse_ip_packet_in_ethernet_frame::<
                    net_types::ip::Ipv4,
                >(&buf)
                {
                    Ok((mut body, _src_mac, _dst_mac, src_ip, dst_ip, _proto, _ttl)) => {
                        match (&mut body).parse_with::<_, packet_formats::udp::UdpPacket<_>>(
                            packet_formats::udp::UdpParseArgs::new(src_ip, dst_ip),
                        ) {
                            Ok(udp) => {
                                const DHCPV4_SERVER_PORT: std::num::NonZeroU16 =
                                    nonzero_ext::nonzero!(67u16);
                                (udp.dst_port() == DHCPV4_SERVER_PORT).then(|| ())
                            }
                            Err(packet_formats::error::ParseError::NotExpected) => None,
                            Err(e) => panic!("failed to parse UDP packet: {}", e),
                        }
                    }
                    Err(packet_formats::error::IpParseError::Parse {
                        error: packet_formats::error::ParseError::NotExpected,
                    }) => None,
                    Err(e) => {
                        panic!("failed to parse IPv4 packet: {}", e);
                    }
                },
            )
        })
        .next()
        .await
        .expect("fake endpoint frame stream ended unexpectedly");
}
