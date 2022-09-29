// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use std::{borrow::Cow, convert::TryFrom as _};

use fidl_fuchsia_net as fnet;
use fidl_fuchsia_net_debug as fnet_debug;
use fidl_fuchsia_net_ext as fnet_ext;
use fidl_fuchsia_net_filter as fnetfilter;
use fidl_fuchsia_net_interfaces_admin as finterfaces_admin;
use fidl_fuchsia_net_interfaces_ext as fnet_interfaces_ext;
use fidl_fuchsia_net_stack as fnet_stack;
use fidl_fuchsia_net_stack_ext::FidlReturn as _;
use futures::FutureExt as _;
use net_declare::fidl_subnet;
use netemul::{RealmTcpListener as _, RealmTcpStream as _, RealmUdpSocket as _};
use netfilter::FidlReturn as _;
use netstack_testing_common::ping as ping_helper;
use netstack_testing_common::realms::{Netstack2, TestSandboxExt as _};
use netstack_testing_macros::variants_test;
use test_case::test_case;

pub enum NatNic {
    RouterNic1,
    RouterNic2,
}

pub struct NatTestCase {
    pub src_subnet: fnet::Subnet,
    pub dst_subnet: fnet::Subnet,

    pub nat_proto: fnetfilter::SocketProtocol,
    pub nat_src_subnet: fnet::Subnet,
    pub nat_outgoing_nic: NatNic,
    pub cycle_dst_net: bool,
}

pub struct HostNetwork<'a> {
    pub net: netemul::TestNetwork<'a>,
    pub router_ep: netemul::TestInterface<'a>,
    pub router_addr: fnet::Subnet,

    pub host_realm: netemul::TestRealm<'a>,
    pub host_ep: netemul::TestInterface<'a>,
    pub host_addr: fnet::Subnet,
}

pub struct MasqueradeNatNetwork<'a> {
    pub router_realm: netemul::TestRealm<'a>,

    pub net1: HostNetwork<'a>,
    pub net2: HostNetwork<'a>,
}

pub fn subnet_to_addr(fnet::Subnet { addr, prefix_len: _ }: fnet::Subnet) -> std::net::IpAddr {
    let fnet_ext::IpAddress(addr) = fnet_ext::IpAddress::from(addr);
    addr
}

pub async fn setup_masquerade_nat_network<'a, E: netemul::Endpoint>(
    sandbox: &'a netemul::TestSandbox,
    name: &str,
    test_case: &NatTestCase,
) -> MasqueradeNatNetwork<'a> {
    let NatTestCase {
        src_subnet,
        dst_subnet,
        nat_proto,
        nat_src_subnet,
        nat_outgoing_nic,
        cycle_dst_net,
    } = test_case;

    let router_realm = sandbox
        .create_netstack_realm::<Netstack2, _>(format!("{}_router", name))
        .expect("failed to create router_realm");

    async fn configure_host_network<'a, E: netemul::Endpoint, S: Into<Cow<'a, str>>>(
        sandbox: &'a netemul::TestSandbox,
        name: &str,
        router_realm: &netemul::TestRealm<'a>,
        router_if_name: Option<S>,
        net_num: u8,
        subnet: fnet::Subnet,
        other_subnet: fnet::Subnet,
    ) -> HostNetwork<'a> {
        let router_addr = common::subnet_with_offset(subnet, 1);
        let host_addr = common::subnet_with_offset(subnet, 2);

        let net = sandbox
            .create_network(format!("net{}", net_num))
            .await
            .expect("failed to create network");

        let host_realm = sandbox
            .create_netstack_realm::<Netstack2, _>(format!("{}_host{}", name, net_num))
            .expect("failed to create host realm");

        let router_ep = router_realm
            .join_network_with_if_config::<E, _>(
                &net,
                format!("router_ep{}", net_num),
                netemul::InterfaceConfig {
                    name: router_if_name.map(Into::into),
                    ..Default::default()
                },
            )
            .await
            .expect("router failed to join network");
        router_ep.add_address_and_subnet_route(router_addr).await.expect("configure address");

        let gen_forwarding_config = |forwarding| finterfaces_admin::Configuration {
            ipv4: Some(finterfaces_admin::Ipv4Configuration {
                forwarding: Some(forwarding),
                ..finterfaces_admin::Ipv4Configuration::EMPTY
            }),
            ipv6: Some(finterfaces_admin::Ipv6Configuration {
                forwarding: Some(forwarding),
                ..finterfaces_admin::Ipv6Configuration::EMPTY
            }),
            ..finterfaces_admin::Configuration::EMPTY
        };

        assert_eq!(
            router_ep
                .control()
                .set_configuration(gen_forwarding_config(true))
                .await
                .expect("set_configuration FIDL error")
                .expect("error setting configuration"),
            gen_forwarding_config(false)
        );

        let host_ep = host_realm
            .join_network::<E, _>(&net, format!("host{}_ep", net_num))
            .await
            .expect("host failed to join network");
        host_ep.add_address_and_subnet_route(host_addr).await.expect("configure address");

        let stack = host_realm
            .connect_to_protocol::<fnet_stack::StackMarker>()
            .expect("failed to connect to netstack");
        let fnet::Subnet { addr: next_hop, prefix_len: _ } = router_addr;
        let () = stack
            .add_forwarding_entry(&mut fnet_stack::ForwardingEntry {
                subnet: fnet_ext::apply_subnet_mask(other_subnet),
                device_id: 0,
                next_hop: Some(Box::new(next_hop)),
                metric: 0,
            })
            .map(move |r| {
                r.squash_result().unwrap_or_else(|e| {
                    panic!("failed to add route to other subnet {:?}: {}", other_subnet, e)
                })
            })
            .await;

        HostNetwork { net, router_ep, router_addr, host_realm, host_ep, host_addr }
    }

    let net1 = configure_host_network::<E, _>(
        &sandbox,
        name,
        &router_realm,
        None::<&str>,
        1,
        *src_subnet,
        *dst_subnet,
    )
    .await;
    let HostNetwork {
        net: _,
        router_ep: router_ep1,
        router_addr: _,
        host_realm: _,
        host_ep: _,
        host_addr: _,
    } = &net1;

    let net2_factory = |router_if_name| async {
        configure_host_network::<E, _>(
            &sandbox,
            name,
            &router_realm,
            router_if_name,
            2,
            *dst_subnet,
            *src_subnet,
        )
        .await
    };

    let mut net2 = net2_factory(None).await;
    let HostNetwork {
        net: _,
        router_ep: router_ep2,
        router_addr: _,
        host_realm: _,
        host_ep: _,
        host_addr: _,
    } = &net2;

    let mut updates = vec![fnetfilter::Nat {
        proto: *nat_proto,
        src_subnet: *nat_src_subnet,
        outgoing_nic: u32::try_from(match nat_outgoing_nic {
            NatNic::RouterNic1 => router_ep1.id(),
            NatNic::RouterNic2 => router_ep2.id(),
        })
        .expect("NIC ID should fit in a u32"),
    }];

    let router_filter = router_realm
        .connect_to_protocol::<fnetfilter::FilterMarker>()
        .expect("failed to connect to filter service");

    let () = router_filter
        .enable_interface(router_ep2.id())
        .await
        .transform_result()
        .expect("error enabling filter on router_ep2");
    let (rules, generation) =
        router_filter.get_nat_rules().await.transform_result().expect("failed to get NAT rules");
    assert_eq!(&rules, &[]);
    let () = router_filter
        .update_nat_rules(&mut updates.iter_mut(), generation)
        .await
        .transform_result()
        .expect("failed to update NAT rules");
    let generation = generation + 1;
    {
        let (got_nat_rules, got_generation) = router_filter
            .get_nat_rules()
            .await
            .transform_result()
            .expect("failed to get NAT rules");
        assert_eq!(got_nat_rules, updates);
        assert_eq!(got_generation, generation);
    }

    if *cycle_dst_net {
        let router_ep2_id = router_ep2.id();
        let state_stream =
            router_ep2.get_interface_event_stream().expect("error getting interface event stream");
        futures::pin_mut!(state_stream);

        // Make sure the interfaces watcher stream knows about router_ep2's existence
        // so we can reliably observe its removal later.
        let mut router_ep2_interface_state = fnet_interfaces_ext::existing(
            &mut state_stream,
            fnet_interfaces_ext::InterfaceState::Unknown(router_ep2_id),
        )
        .await
        .expect("error reading existing interface event");

        let router_ep2_name = match &router_ep2_interface_state {
            fnet_interfaces_ext::InterfaceState::Known(fnet_interfaces_ext::Properties {
                id,
                name,
                device_class: _,
                online: _,
                addresses: _,
                has_default_ipv4_route: _,
                has_default_ipv6_route: _,
            }) => {
                assert_eq!(*id, router_ep2_id);
                name.clone()
            }
            fnet_interfaces_ext::InterfaceState::Unknown(id) => {
                panic!("expected known interface state for router_ep2(id={}); got unknown state for ID = {}",
                       router_ep2_id, id)
            }
        };

        let () = std::mem::drop(net2);
        let () = fnet_interfaces_ext::wait_interface_with_id(
            state_stream,
            &mut router_ep2_interface_state,
            |fnet_interfaces_ext::Properties {
                 id,
                 name: _,
                 device_class: _,
                 online: _,
                 addresses: _,
                 has_default_ipv4_route: _,
                 has_default_ipv6_route: _,
             }| {
                assert_eq!(*id, router_ep2_id);
                None
            },
        )
        .await
        .map_or_else(
            |err| match err {
                fnet_interfaces_ext::WatcherOperationError::Update(
                    fnet_interfaces_ext::UpdateError::Removed,
                ) => {}
                err => panic!("unexpected error waiting for interface removal: {:?}", err),
            },
            |_: ()| panic!("expected to get removed event"),
        );

        // The NAT rule for a NIC should be removed when the NIC is removed.
        let (got_nat_rules, got_generation) = router_filter
            .get_nat_rules()
            .await
            .transform_result()
            .expect("failed to get NAT rules");
        assert_eq!(got_nat_rules, []);
        assert_eq!(got_generation, generation + 1);

        net2 = net2_factory(Some(router_ep2_name)).await;
        let HostNetwork {
            net: _,
            router_ep: router_ep2,
            router_addr: _,
            host_realm: _,
            host_ep: _,
            host_addr: _,
        } = &net2;
        assert_ne!(router_ep2_id, router_ep2.id());
    }

    async fn get_mac(realm: &netemul::TestRealm<'_>, id: u64) -> fidl_fuchsia_net::MacAddress {
        let debug = realm
            .connect_to_protocol::<fnet_debug::InterfacesMarker>()
            .expect("failed to connect to debug protocol");
        *debug
            .get_mac(id)
            .await
            .expect("error calling get_mac")
            .expect("error getting bridge's MAC address")
            .expect("expected bridge to have a MAC address")
    }

    for (realm, neighbors) in [
        (
            &router_realm,
            &[
                (
                    net1.router_ep.id(),
                    net1.host_addr.addr,
                    get_mac(&net1.host_realm, net1.host_ep.id()).await,
                ),
                (
                    net2.router_ep.id(),
                    net2.host_addr.addr,
                    get_mac(&net2.host_realm, net2.host_ep.id()).await,
                ),
            ][..],
        ),
        (
            &net1.host_realm,
            &[(
                net1.host_ep.id(),
                net1.router_addr.addr,
                get_mac(&router_realm, net1.router_ep.id()).await,
            )][..],
        ),
        (
            &net2.host_realm,
            &[(
                net2.host_ep.id(),
                net2.router_addr.addr,
                get_mac(&router_realm, net2.router_ep.id()).await,
            )][..],
        ),
    ] {
        for (interface, addr, mac) in neighbors.into_iter().copied() {
            realm
                .add_neighbor_entry(interface, addr, mac)
                .await
                .expect("failed to add neighbor entry");
        }
    }

    MasqueradeNatNetwork { router_realm, net1, net2 }
}

pub const IPV4_SUBNET1: fnet::Subnet = fidl_subnet!("10.0.0.0/24");
pub const IPV4_SUBNET2: fnet::Subnet = fidl_subnet!("192.168.0.0/24");
pub const IPV6_SUBNET1: fnet::Subnet = fidl_subnet!("a::/24");
pub const IPV6_SUBNET2: fnet::Subnet = fidl_subnet!("b::/24");

#[variants_test]
#[test_case(
    "perform_nat44",
    NatTestCase {
        src_subnet:IPV4_SUBNET1,
        dst_subnet: IPV4_SUBNET2,
        nat_proto: fnetfilter::SocketProtocol::Udp,
        nat_src_subnet: IPV4_SUBNET1,
        nat_outgoing_nic: NatNic::RouterNic2,
        cycle_dst_net: false,
    },
    true; "perform_nat44")]
#[test_case(
    "dont_perform_nat44_outgoing_nic_cycled",
    NatTestCase {
        src_subnet:IPV4_SUBNET1,
        dst_subnet: IPV4_SUBNET2,
        nat_proto: fnetfilter::SocketProtocol::Udp,
        nat_src_subnet: IPV4_SUBNET1,
        nat_outgoing_nic: NatNic::RouterNic2,
        cycle_dst_net: true,
    },
    false; "dont_perform_nat44_outgoing_nic_cycled")]
#[test_case(
    "dont_perform_nat44_different_protocol",
    NatTestCase {
        src_subnet: IPV4_SUBNET1,
        dst_subnet: IPV4_SUBNET2,
        nat_proto: fnetfilter::SocketProtocol::Tcp,
        nat_src_subnet: IPV4_SUBNET1,
        nat_outgoing_nic: NatNic::RouterNic2,
        cycle_dst_net: false,
    },
    false; "dont_perform_nat44_different_protocol")]
#[test_case(
    "dont_perform_nat44_different_src_subnet",
    NatTestCase {
        src_subnet:IPV4_SUBNET1,
        dst_subnet: IPV4_SUBNET2,
        nat_proto: fnetfilter::SocketProtocol::Udp,
        nat_src_subnet: IPV4_SUBNET2,
        nat_outgoing_nic: NatNic::RouterNic2,
        cycle_dst_net: false,
    },
    false; "dont_perform_nat44_different_src_subnet")]
#[test_case(
    "dont_perform_nat44_different_nic",
    NatTestCase {
        src_subnet:IPV4_SUBNET1,
        dst_subnet: IPV4_SUBNET2,
        nat_proto: fnetfilter::SocketProtocol::Udp,
        nat_src_subnet: IPV4_SUBNET1,
        nat_outgoing_nic: NatNic::RouterNic1,
        cycle_dst_net: false,
    },
    true; "dont_perform_nat44_different_nic")]
#[test_case(
    "perform_nat66",
    NatTestCase {
        src_subnet:IPV6_SUBNET1,
        dst_subnet: IPV6_SUBNET2,
        nat_proto: fnetfilter::SocketProtocol::Udp,
        nat_src_subnet: IPV6_SUBNET1,
        nat_outgoing_nic: NatNic::RouterNic2,
        cycle_dst_net: false,
    },
    true; "perform_nat66")]
#[test_case(
    "dont_perform_nat66_outgoing_nic_cycled",
    NatTestCase {
        src_subnet:IPV6_SUBNET1,
        dst_subnet: IPV6_SUBNET2,
        nat_proto: fnetfilter::SocketProtocol::Udp,
        nat_src_subnet: IPV6_SUBNET1,
        nat_outgoing_nic: NatNic::RouterNic2,
        cycle_dst_net: true,
    },
    false; "dont_perform_nat66_outgoing_nic_cycled")]
#[test_case(
    "dont_perform_nat66_different_protocol",
    NatTestCase {
        src_subnet: IPV6_SUBNET1,
        dst_subnet: IPV6_SUBNET2,
        nat_proto: fnetfilter::SocketProtocol::Tcp,
        nat_src_subnet: IPV6_SUBNET1,
        nat_outgoing_nic: NatNic::RouterNic2,
        cycle_dst_net: false,
    },
    false; "dont_perform_nat66_different_protocol")]
#[test_case(
    "dont_perform_nat66_different_src_subnet",
    NatTestCase {
        src_subnet:IPV6_SUBNET1,
        dst_subnet: IPV6_SUBNET2,
        nat_proto: fnetfilter::SocketProtocol::Udp,
        nat_src_subnet: IPV6_SUBNET2,
        nat_outgoing_nic: NatNic::RouterNic2,
        cycle_dst_net: false,
    },
    false; "dont_perform_nat66_different_src_subnet")]
#[test_case(
    "dont_perform_nat66_different_nic",
    NatTestCase {
        src_subnet:IPV6_SUBNET1,
        dst_subnet: IPV6_SUBNET2,
        nat_proto: fnetfilter::SocketProtocol::Udp,
        nat_src_subnet: IPV6_SUBNET1,
        nat_outgoing_nic: NatNic::RouterNic1,
        cycle_dst_net: false,
    },
    true; "dont_perform_nat66_different_nic")]
async fn masquerade_nat_udp<E: netemul::Endpoint>(
    test_name: &str,
    sub_test_name: &str,
    test_case: NatTestCase,
    expect_nat: bool,
) {
    let name = format!("{}_{}", test_name, sub_test_name);
    let name = name.as_str();

    let sandbox = netemul::TestSandbox::new().expect("failed to create sandbox");

    let MasqueradeNatNetwork {
        router_realm: _router_realm,
        net1:
            HostNetwork {
                net: _net1,
                router_ep: _router_ep1,
                router_addr: _,
                host_realm: host1_realm,
                host_ep: _host1_ep,
                host_addr: host1_addr,
            },
        net2:
            HostNetwork {
                net: _net2,
                router_ep: _router_ep2,
                router_addr: router_ep2_addr,
                host_realm: host2_realm,
                host_ep: _host2_ep,
                host_addr: host2_addr,
            },
    } = setup_masquerade_nat_network::<E>(&sandbox, name, &test_case).await;

    let get_sock = |realm, subnet| async move {
        let addr = subnet_to_addr(subnet);
        let sock =
            fuchsia_async::net::UdpSocket::bind_in_realm(realm, std::net::SocketAddr::new(addr, 0))
                .await
                .expect("failed to create socket");
        let addr = sock.local_addr().expect("failed to get socket's local addr");

        (sock, addr)
    };

    let (host1_sock, host1_sockaddr) = get_sock(&host1_realm, host1_addr).await;
    let (host2_sock, host2_sockaddr) = get_sock(&host2_realm, host2_addr).await;

    // Send a packet from host1 to host2.
    const SEND_SIZE: usize = 4;
    const SEND_BUF: [u8; SEND_SIZE] = [1, 2, 4, 5];
    assert_eq!(
        host1_sock
            .send_to(&SEND_BUF, host2_sockaddr)
            .await
            .expect("failed to send from host1 to host2"),
        SEND_BUF.len()
    );

    // Host2 should loop the packet back to host1.
    {
        let mut recv_buf = [0; SEND_SIZE + 1];
        let (got_byte_count, sender) =
            host2_sock.recv_from(&mut recv_buf).await.expect("failed to recv from host2_sock");
        assert_eq!(got_byte_count, SEND_BUF.len());
        let recv_buf = &recv_buf[..got_byte_count];
        assert_eq!(recv_buf, &SEND_BUF);
        let NatTestCase {
            src_subnet: _,
            dst_subnet: _,
            nat_proto: _,
            nat_src_subnet: _,
            nat_outgoing_nic: _,
            cycle_dst_net: _,
        } = test_case;

        let expected_sender = if expect_nat {
            std::net::SocketAddr::new(subnet_to_addr(router_ep2_addr), host1_sockaddr.port())
        } else {
            host1_sockaddr
        };
        assert_eq!(sender, expected_sender);
        assert_eq!(
            host2_sock.send_to(recv_buf, sender).await.expect("failed to send from host1 to host2"),
            SEND_BUF.len()
        );
    };

    // Make sure the packet was looped back to host1 by host2.
    {
        let mut recv_buf = [0; SEND_SIZE + 1];
        let (got_byte_count, sender) =
            host1_sock.recv_from(&mut recv_buf).await.expect("failed to recv from host2_sock");
        assert_eq!(got_byte_count, SEND_BUF.len());
        assert_eq!(&recv_buf[..SEND_BUF.len()], &SEND_BUF);
        assert_eq!(sender, host2_sockaddr);
    }
}

#[variants_test]
#[test_case(
    "perform_nat44",
    NatTestCase {
        src_subnet:IPV4_SUBNET1,
        dst_subnet: IPV4_SUBNET2,
        nat_proto: fnetfilter::SocketProtocol::Tcp,
        nat_src_subnet: IPV4_SUBNET1,
        nat_outgoing_nic: NatNic::RouterNic2,
        cycle_dst_net: false,
    },
    true; "perform_nat44")]
#[test_case(
    "dont_perform_nat44_outgoing_nic_cycled",
    NatTestCase {
        src_subnet:IPV4_SUBNET1,
        dst_subnet: IPV4_SUBNET2,
        nat_proto: fnetfilter::SocketProtocol::Tcp,
        nat_src_subnet: IPV4_SUBNET1,
        nat_outgoing_nic: NatNic::RouterNic2,
        cycle_dst_net: true,
    },
    false; "dont_perform_nat44_outgoing_nic_cycled")]
#[test_case(
    "dont_perform_nat44_different_protocol",
    NatTestCase {
        src_subnet: IPV4_SUBNET1,
        dst_subnet: IPV4_SUBNET2,
        nat_proto: fnetfilter::SocketProtocol::Udp,
        nat_src_subnet: IPV4_SUBNET1,
        nat_outgoing_nic: NatNic::RouterNic2,
        cycle_dst_net: false,
    },
    false; "dont_perform_nat44_different_protocol")]
#[test_case(
    "dont_perform_nat44_different_src_subnet",
    NatTestCase {
        src_subnet:IPV4_SUBNET1,
        dst_subnet: IPV4_SUBNET2,
        nat_proto: fnetfilter::SocketProtocol::Tcp,
        nat_src_subnet: IPV4_SUBNET2,
        nat_outgoing_nic: NatNic::RouterNic2,
        cycle_dst_net: false,
    },
    false; "dont_perform_nat44_different_src_subnet")]
#[test_case(
    "dont_perform_nat44_different_nic",
    NatTestCase {
        src_subnet:IPV4_SUBNET1,
        dst_subnet: IPV4_SUBNET2,
        nat_proto: fnetfilter::SocketProtocol::Tcp,
        nat_src_subnet: IPV4_SUBNET1,
        nat_outgoing_nic: NatNic::RouterNic1,
        cycle_dst_net: false,
    },
    true; "dont_perform_nat44_different_nic")]
#[test_case(
    "perform_nat66",
    NatTestCase {
        src_subnet:IPV6_SUBNET1,
        dst_subnet: IPV6_SUBNET2,
        nat_proto: fnetfilter::SocketProtocol::Tcp,
        nat_src_subnet: IPV6_SUBNET1,
        nat_outgoing_nic: NatNic::RouterNic2,
        cycle_dst_net: false,
    },
    true; "perform_nat66")]
#[test_case(
    "dont_perform_nat66_outgoing_nic_cycled",
    NatTestCase {
        src_subnet:IPV6_SUBNET1,
        dst_subnet: IPV6_SUBNET2,
        nat_proto: fnetfilter::SocketProtocol::Tcp,
        nat_src_subnet: IPV6_SUBNET1,
        nat_outgoing_nic: NatNic::RouterNic2,
        cycle_dst_net: true,
    },
    false; "dont_perform_nat66_outgoing_nic_cycled")]
#[test_case(
    "dont_perform_nat66_different_protocol",
    NatTestCase {
        src_subnet: IPV6_SUBNET1,
        dst_subnet: IPV6_SUBNET2,
        nat_proto: fnetfilter::SocketProtocol::Udp,
        nat_src_subnet: IPV6_SUBNET1,
        nat_outgoing_nic: NatNic::RouterNic2,
        cycle_dst_net: false,
    },
    false; "dont_perform_nat66_different_protocol")]
#[test_case(
    "dont_perform_nat66_different_src_subnet",
    NatTestCase {
        src_subnet:IPV6_SUBNET1,
        dst_subnet: IPV6_SUBNET2,
        nat_proto: fnetfilter::SocketProtocol::Tcp,
        nat_src_subnet: IPV6_SUBNET2,
        nat_outgoing_nic: NatNic::RouterNic2,
        cycle_dst_net: false,
    },
    false; "dont_perform_nat66_different_src_subnet")]
#[test_case(
    "dont_perform_nat66_different_nic",
    NatTestCase {
        src_subnet:IPV6_SUBNET1,
        dst_subnet: IPV6_SUBNET2,
        nat_proto: fnetfilter::SocketProtocol::Tcp,
        nat_src_subnet: IPV6_SUBNET1,
        nat_outgoing_nic: NatNic::RouterNic1,
        cycle_dst_net: false,
    },
    true; "dont_perform_nat66_different_nic")]
async fn masquerade_nat_tcp<E: netemul::Endpoint>(
    test_name: &str,
    sub_test_name: &str,
    test_case: NatTestCase,
    expect_nat: bool,
) {
    let name = format!("{}_{}", test_name, sub_test_name);
    let name = name.as_str();

    let sandbox = netemul::TestSandbox::new().expect("failed to create sandbox");

    let MasqueradeNatNetwork {
        router_realm: _router_realm,
        net1:
            HostNetwork {
                net: _net1,
                router_ep: _router_ep1,
                router_addr: _,
                host_realm: host1_realm,
                host_ep: _host1_ep,
                host_addr: _,
            },
        net2:
            HostNetwork {
                net: _net2,
                router_ep: _router_ep2,
                router_addr: router_ep2_addr,
                host_realm: host2_realm,
                host_ep: _host2_ep,
                host_addr: host2_addr,
            },
    } = setup_masquerade_nat_network::<E>(&sandbox, name, &test_case).await;

    let host2_listener = fuchsia_async::net::TcpListener::listen_in_realm(
        &host2_realm,
        std::net::SocketAddr::new(subnet_to_addr(host2_addr), 0),
    )
    .await
    .expect("failed to create TCP listener");
    let host2_listener_addr =
        host2_listener.local_addr().expect("failed to get host2_listener's local addr");

    let host1_client =
        fuchsia_async::net::TcpStream::connect_in_realm(&host1_realm, host2_listener_addr)
            .await
            .expect("failed to connect to host2 from host1");
    let (_host2_listener, _accepted_sock, client_addr) =
        host2_listener.accept().await.expect("failed to accept connection");

    let host1_client_addr =
        host1_client.std().local_addr().expect("failed to get host1_client's local addr");
    let NatTestCase {
        src_subnet: _,
        dst_subnet: _,
        nat_proto: _,
        nat_src_subnet: _,
        nat_outgoing_nic: _,
        cycle_dst_net: _,
    } = test_case;

    let expected_client_addr = if expect_nat {
        std::net::SocketAddr::new(subnet_to_addr(router_ep2_addr), host1_client_addr.port())
    } else {
        host1_client_addr
    };
    assert_eq!(client_addr, expected_client_addr);
}

#[variants_test]
#[test_case(
    "perform_nat44",
    NatTestCase {
        src_subnet:IPV4_SUBNET1,
        dst_subnet: IPV4_SUBNET2,
        nat_proto: fnetfilter::SocketProtocol::Icmp,
        nat_src_subnet: IPV4_SUBNET1,
        nat_outgoing_nic: NatNic::RouterNic2,
        cycle_dst_net: false,
    }; "perform_nat44")]
#[test_case(
    "dont_perform_nat44_outgoing_nic_cycled",
    NatTestCase {
        src_subnet:IPV4_SUBNET1,
        dst_subnet: IPV4_SUBNET2,
        nat_proto: fnetfilter::SocketProtocol::Icmp,
        nat_src_subnet: IPV4_SUBNET1,
        nat_outgoing_nic: NatNic::RouterNic2,
        cycle_dst_net: true,
    }; "dont_perform_nat44_outgoing_nic_cycled")]
#[test_case(
    "dont_perform_nat44_different_protocol",
    NatTestCase {
        src_subnet: IPV4_SUBNET1,
        dst_subnet: IPV4_SUBNET2,
        nat_proto: fnetfilter::SocketProtocol::Tcp,
        nat_src_subnet: IPV4_SUBNET1,
        nat_outgoing_nic: NatNic::RouterNic2,
        cycle_dst_net: false,
    }; "dont_perform_nat44_different_protocol")]
#[test_case(
    "dont_perform_nat44_different_src_subnet",
    NatTestCase {
        src_subnet:IPV4_SUBNET1,
        dst_subnet: IPV4_SUBNET2,
        nat_proto: fnetfilter::SocketProtocol::Icmp,
        nat_src_subnet: IPV4_SUBNET2,
        nat_outgoing_nic: NatNic::RouterNic2,
        cycle_dst_net: false,
    }; "dont_perform_nat44_different_src_subnet")]
#[test_case(
    "dont_perform_nat44_different_nic",
    NatTestCase {
        src_subnet:IPV4_SUBNET1,
        dst_subnet: IPV4_SUBNET2,
        nat_proto: fnetfilter::SocketProtocol::Icmp,
        nat_src_subnet: IPV4_SUBNET1,
        nat_outgoing_nic: NatNic::RouterNic1,
        cycle_dst_net: false,
    }; "dont_perform_nat44_different_nic")]
#[test_case(
    "perform_nat66",
    NatTestCase {
        src_subnet:IPV6_SUBNET1,
        dst_subnet: IPV6_SUBNET2,
        nat_proto: fnetfilter::SocketProtocol::Icmpv6,
        nat_src_subnet: IPV6_SUBNET1,
        nat_outgoing_nic: NatNic::RouterNic2,
        cycle_dst_net: false,
    }; "perform_nat66")]
#[test_case(
    "dont_perform_nat66_outgoing_nic_cycled",
    NatTestCase {
        src_subnet:IPV6_SUBNET1,
        dst_subnet: IPV6_SUBNET2,
        nat_proto: fnetfilter::SocketProtocol::Icmpv6,
        nat_src_subnet: IPV6_SUBNET1,
        nat_outgoing_nic: NatNic::RouterNic2,
        cycle_dst_net: true,
    }; "dont_perform_nat66_outgoing_nic_cycled")]
#[test_case(
    "dont_perform_nat66_different_protocol",
    NatTestCase {
        src_subnet: IPV6_SUBNET1,
        dst_subnet: IPV6_SUBNET2,
        nat_proto: fnetfilter::SocketProtocol::Tcp,
        nat_src_subnet: IPV6_SUBNET1,
        nat_outgoing_nic: NatNic::RouterNic2,
        cycle_dst_net: false,
    }; "dont_perform_nat66_different_protocol")]
#[test_case(
    "dont_perform_nat66_different_src_subnet",
    NatTestCase {
        src_subnet:IPV6_SUBNET1,
        dst_subnet: IPV6_SUBNET2,
        nat_proto: fnetfilter::SocketProtocol::Icmpv6,
        nat_src_subnet: IPV6_SUBNET2,
        nat_outgoing_nic: NatNic::RouterNic2,
        cycle_dst_net: false,
    }; "dont_perform_nat66_different_src_subnet")]
#[test_case(
    "dont_perform_nat66_different_nic",
    NatTestCase {
        src_subnet:IPV6_SUBNET1,
        dst_subnet: IPV6_SUBNET2,
        nat_proto: fnetfilter::SocketProtocol::Icmpv6,
        nat_src_subnet: IPV6_SUBNET1,
        nat_outgoing_nic: NatNic::RouterNic1,
        cycle_dst_net: false,
    }; "dont_perform_nat66_different_nic")]
async fn masquerade_nat_ping<E: netemul::Endpoint>(
    test_name: &str,
    sub_test_name: &str,
    test_case: NatTestCase,
) {
    let name = format!("{}_{}", test_name, sub_test_name);
    let name = name.as_str();

    let sandbox = netemul::TestSandbox::new().expect("failed to create sandbox");

    let MasqueradeNatNetwork {
        router_realm: _router_realm,
        net1:
            HostNetwork {
                net: _net1,
                router_ep: _router_ep1,
                router_addr: _,
                host_realm: host1_realm,
                host_ep: host1_ep,
                host_addr: host1_addr,
            },
        net2:
            HostNetwork {
                net: _net2,
                router_ep: _router_ep2,
                router_addr: _router_ep2_addr,
                host_realm: host2_realm,
                host_ep: host2_ep,
                host_addr: host2_addr,
            },
    } = setup_masquerade_nat_network::<E>(&sandbox, name, &test_case).await;

    let ping_node = |realm, id, fnet::Subnet { addr, prefix_len: _ }| {
        let (v4_addrs, v6_addrs) = match addr {
            fnet::IpAddress::Ipv4(fnet::Ipv4Address { addr }) => (vec![addr.into()], vec![]),
            fnet::IpAddress::Ipv6(fnet::Ipv6Address { addr }) => (vec![], vec![addr.into()]),
        };
        ping_helper::Node::new(realm, id, v4_addrs, v6_addrs)
    };

    let host1_node = ping_node(&host1_realm, host1_ep.id(), host1_addr);
    let host2_node = ping_node(&host2_realm, host2_ep.id(), host2_addr);
    let () = host1_node
        .ping_pairwise(&[host2_node])
        .await
        .expect("expected to successfully ping between host1 and host2");
}
