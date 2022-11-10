// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use std::{convert::TryInto as _, mem::size_of};

use fidl_fuchsia_net as net;
use fidl_fuchsia_net_interfaces_admin as finterfaces_admin;
use fidl_fuchsia_net_stack as net_stack;
use fidl_fuchsia_netemul_network as fnetemul_network;
use fidl_fuchsia_posix_socket as fposix_socket;
use fidl_fuchsia_posix_socket_raw as fposix_socket_raw;
use fuchsia_async::{DurationExt as _, TimeoutExt as _};
use fuchsia_zircon as zx;

use anyhow::{self, Context as _};
use futures::{
    future, Future, FutureExt as _, StreamExt as _, TryFutureExt as _, TryStreamExt as _,
};
use net_types::{
    ethernet::Mac,
    ip::{self as net_types_ip, Ip},
    LinkLocalAddress as _, MulticastAddress as _, SpecifiedAddress as _, Witness as _,
};
use netstack_testing_common::{
    constants::{eth as eth_consts, ipv6 as ipv6_consts},
    interfaces,
    realms::{
        constants, KnownServiceProvider, Netstack, Netstack2, NetstackVersion, TestSandboxExt as _,
    },
    send_ra_with_router_lifetime, setup_network, setup_network_with, sleep, write_ndp_message,
    ASYNC_EVENT_CHECK_INTERVAL, ASYNC_EVENT_NEGATIVE_CHECK_TIMEOUT,
    ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT, NDP_MESSAGE_TTL,
};
use netstack_testing_macros::variants_test;
use packet::{InnerPacketBuilder as _, ParsablePacket as _, Serializer as _};
use packet_formats::{
    ethernet::{EtherType, EthernetFrame, EthernetFrameLengthCheck},
    icmp::{
        mld::MldPacket,
        ndp::{
            options::{NdpOption, NdpOptionBuilder, PrefixInformation, RouteInformation},
            NeighborAdvertisement, NeighborSolicitation, RoutePreference, RouterAdvertisement,
            RouterSolicitation,
        },
        IcmpParseArgs, Icmpv6Packet,
    },
    ip::Ipv6Proto,
    testutil::{parse_icmp_packet_in_ip_packet_in_ethernet_frame, parse_ip_packet},
};
use test_case::test_case;

/// The expected number of Router Solicitations sent by the netstack when an
/// interface is brought up as a host.
const EXPECTED_ROUTER_SOLICIATIONS: u8 = 3;

/// The expected interval between sending Router Solicitation messages when
/// soliciting IPv6 routers.
const EXPECTED_ROUTER_SOLICITATION_INTERVAL: zx::Duration = zx::Duration::from_seconds(4);

/// The expected number of Neighbor Solicitations sent by the netstack when
/// performing Duplicate Address Detection.
const EXPECTED_DUP_ADDR_DETECT_TRANSMITS: u8 = 1;

/// The expected interval between sending Neighbor Solicitation messages when
/// performing Duplicate Address Detection.
const EXPECTED_DAD_RETRANSMIT_TIMER: zx::Duration = zx::Duration::from_seconds(1);

/// As per [RFC 7217 section 6] Hosts SHOULD introduce a random delay between 0 and
/// `IDGEN_DELAY` before trying a new tentative address.
///
/// [RFC 7217]: https://tools.ietf.org/html/rfc7217#section-6
const DAD_IDGEN_DELAY: zx::Duration = zx::Duration::from_seconds(1);

async fn install_and_get_ipv6_addrs_for_endpoint<N: Netstack>(
    realm: &netemul::TestRealm<'_>,
    endpoint: &netemul::TestEndpoint<'_>,
    name: &str,
) -> Vec<net::Subnet> {
    let (id, control, _device_control) = endpoint
        .add_to_stack(
            realm,
            netemul::InterfaceConfig { name: Some(name.into()), ..Default::default() },
        )
        .await
        .expect("installing interface");
    let did_enable = control.enable().await.expect("calling enable").expect("enable failed");
    assert!(did_enable);

    let interface_state = realm
        .connect_to_protocol::<fidl_fuchsia_net_interfaces::StateMarker>()
        .expect("failed to connect to fuchsia.net.interfaces/State service");
    let mut state = fidl_fuchsia_net_interfaces_ext::InterfaceState::Unknown(id.into());
    let ipv6_addresses = fidl_fuchsia_net_interfaces_ext::wait_interface_with_id(
        fidl_fuchsia_net_interfaces_ext::event_stream_from_state(&interface_state)
            .expect("creating interface event stream"),
        &mut state,
        |fidl_fuchsia_net_interfaces_ext::Properties {
             id: _,
             name: _,
             device_class: _,
             online: _,
             addresses,
             has_default_ipv4_route: _,
             has_default_ipv6_route: _,
         }| {
            let ipv6_addresses = addresses
                .iter()
                .filter_map(|fidl_fuchsia_net_interfaces_ext::Address { addr, valid_until: _ }| {
                    match addr.addr {
                        net::IpAddress::Ipv6(net::Ipv6Address { .. }) => Some(addr),
                        net::IpAddress::Ipv4(net::Ipv4Address { .. }) => None,
                    }
                })
                .copied()
                .collect::<Vec<_>>();
            if ipv6_addresses.is_empty() {
                None
            } else {
                Some(ipv6_addresses)
            }
        },
    )
    .await
    .expect("failed to observe interface addition");

    ipv6_addresses
}

/// Test that across netstack runs, a device will initially be assigned the same
/// IPv6 addresses.
#[variants_test]
async fn consistent_initial_ipv6_addrs<E: netemul::Endpoint>(name: &str) {
    let sandbox = netemul::TestSandbox::new().expect("failed to create sandbox");
    let realm = sandbox
        .create_realm(
            name,
            &[
                // This test exercises stash persistence. Netstack-debug, which
                // is the default used by test helpers, does not use
                // persistence.
                KnownServiceProvider::Netstack(NetstackVersion::ProdNetstack2),
                KnownServiceProvider::SecureStash,
            ],
        )
        .expect("failed to create realm");
    let endpoint = sandbox.create_endpoint::<E, _>(name).await.expect("failed to create endpoint");
    let () = endpoint.set_link_up(true).await.expect("failed to set link up");

    // Make sure netstack uses the same addresses across runs for a device.
    let first_run_addrs =
        install_and_get_ipv6_addrs_for_endpoint::<Netstack2>(&realm, &endpoint, name).await;

    // Stop the netstack.
    let () = realm
        .stop_child_component(constants::netstack::COMPONENT_NAME)
        .await
        .expect("failed to stop netstack");

    let second_run_addrs =
        install_and_get_ipv6_addrs_for_endpoint::<Netstack2>(&realm, &endpoint, name).await;
    assert_eq!(first_run_addrs, second_run_addrs);
}

/// Enables IPv6 forwarding configuration.
async fn enable_ipv6_forwarding(iface: &netemul::TestInterface<'_>) {
    let config_with_ipv6_forwarding_set = |forwarding| finterfaces_admin::Configuration {
        ipv6: Some(finterfaces_admin::Ipv6Configuration {
            forwarding: Some(forwarding),
            ..finterfaces_admin::Ipv6Configuration::EMPTY
        }),
        ..finterfaces_admin::Configuration::EMPTY
    };

    let configuration = iface
        .control()
        .set_configuration(config_with_ipv6_forwarding_set(true))
        .await
        .expect("set_configuration FIDL error")
        .expect("error setting configuration");

    assert_eq!(configuration, config_with_ipv6_forwarding_set(false))
}

/// Tests that `EXPECTED_ROUTER_SOLICIATIONS` Router Solicitation messages are transmitted
/// when the interface is brought up.
#[variants_test]
#[test_case("host", false ; "host")]
#[test_case("router", true ; "router")]
async fn sends_router_solicitations<E: netemul::Endpoint>(
    test_name: &str,
    sub_test_name: &str,
    forwarding: bool,
) {
    let name = format!("{}_{}", test_name, sub_test_name);
    let name = name.as_str();

    let sandbox = netemul::TestSandbox::new().expect("failed to create sandbox");
    let (_network, _realm, _netstack, iface, fake_ep) =
        setup_network::<E>(&sandbox, name, None).await.expect("error setting up network");

    if forwarding {
        enable_ipv6_forwarding(&iface).await;
    }

    // Make sure exactly `EXPECTED_ROUTER_SOLICIATIONS` RS messages are transmitted
    // by the netstack.
    let mut observed_rs = 0;
    loop {
        // When we have already observed the expected number of RS messages, do a
        // negative check to make sure that we don't send anymore.
        let extra_timeout = if observed_rs == EXPECTED_ROUTER_SOLICIATIONS {
            ASYNC_EVENT_NEGATIVE_CHECK_TIMEOUT
        } else {
            ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT
        };

        let ret = fake_ep
            .frame_stream()
            .try_filter_map(|(data, dropped)| {
                assert_eq!(dropped, 0);
                let mut observed_slls = Vec::new();
                future::ok(
                    parse_icmp_packet_in_ip_packet_in_ethernet_frame::<
                        net_types_ip::Ipv6,
                        _,
                        RouterSolicitation,
                        _,
                    >(&data, |p| {
                        for option in p.body().iter() {
                            if let NdpOption::SourceLinkLayerAddress(a) = option {
                                let mut mac_bytes = [0; 6];
                                mac_bytes.copy_from_slice(&a[..size_of::<Mac>()]);
                                observed_slls.push(Mac::new(mac_bytes));
                            } else {
                                // We should only ever have an NDP Source Link-Layer Address
                                // option in a RS.
                                panic!("unexpected option in RS = {:?}", option);
                            }
                        }
                    })
                    .map_or(
                        None,
                        |(_src_mac, dst_mac, src_ip, dst_ip, ttl, _message, _code)| {
                            Some((dst_mac, src_ip, dst_ip, ttl, observed_slls))
                        },
                    ),
                )
            })
            .try_next()
            .map(|r| r.context("error getting OnData event"))
            .on_timeout((EXPECTED_ROUTER_SOLICITATION_INTERVAL + extra_timeout).after_now(), || {
                // If we already observed `EXPECTED_ROUTER_SOLICIATIONS` RS, then we shouldn't
                // have gotten any more; the timeout is expected.
                if observed_rs == EXPECTED_ROUTER_SOLICIATIONS {
                    return Ok(None);
                }

                return Err(anyhow::anyhow!("timed out waiting for the {}-th RS", observed_rs));
            })
            .await
            .unwrap();

        let (dst_mac, src_ip, dst_ip, ttl, observed_slls) = match ret {
            Some((dst_mac, src_ip, dst_ip, ttl, observed_slls)) => {
                (dst_mac, src_ip, dst_ip, ttl, observed_slls)
            }
            None => break,
        };

        assert_eq!(
            dst_mac,
            Mac::from(&net_types_ip::Ipv6::ALL_ROUTERS_LINK_LOCAL_MULTICAST_ADDRESS)
        );

        // DAD should have resolved for the link local IPv6 address that is assigned to
        // the interface when it is first brought up. When a link local address is
        // assigned to the interface, it should be used for transmitted RS messages.
        if observed_rs > 0 {
            assert!(src_ip.is_specified())
        }

        assert_eq!(dst_ip, net_types_ip::Ipv6::ALL_ROUTERS_LINK_LOCAL_MULTICAST_ADDRESS.get());

        assert_eq!(ttl, NDP_MESSAGE_TTL);

        // The Router Solicitation should only ever have at max 1 source
        // link-layer option.
        assert!(observed_slls.len() <= 1);
        let observed_sll = observed_slls.into_iter().nth(0);
        if src_ip.is_specified() {
            if observed_sll.is_none() {
                panic!("expected source-link-layer address option if RS has a specified source IP address");
            }
        } else if observed_sll.is_some() {
            panic!("unexpected source-link-layer address option for RS with unspecified source IP address");
        }

        observed_rs += 1;
    }

    assert_eq!(observed_rs, EXPECTED_ROUTER_SOLICIATIONS);
}

/// Tests that both stable and temporary SLAAC addresses are generated for a SLAAC prefix.
#[variants_test]
#[test_case("host", false ; "host")]
#[test_case("router", true ; "router")]
async fn slaac_with_privacy_extensions<E: netemul::Endpoint>(
    test_name: &str,
    sub_test_name: &str,
    forwarding: bool,
) {
    let name = format!("{}_{}", test_name, sub_test_name);
    let name = name.as_str();
    let sandbox = netemul::TestSandbox::new().expect("failed to create sandbox");
    let (_network, realm, _netstack, iface, fake_ep) =
        setup_network::<E>(&sandbox, name, None).await.expect("error setting up network");

    if forwarding {
        enable_ipv6_forwarding(&iface).await;
    }

    // Wait for a Router Solicitation.
    //
    // The first RS should be sent immediately.
    let () = fake_ep
        .frame_stream()
        .try_filter_map(|(data, dropped)| {
            assert_eq!(dropped, 0);
            future::ok(
                parse_icmp_packet_in_ip_packet_in_ethernet_frame::<
                    net_types_ip::Ipv6,
                    _,
                    RouterSolicitation,
                    _,
                >(&data, |_| {})
                .map_or(None, |_| Some(())),
            )
        })
        .try_next()
        .map(|r| r.context("error getting OnData event"))
        .on_timeout(ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT.after_now(), || {
            Err(anyhow::anyhow!("timed out waiting for RS packet"))
        })
        .await
        .unwrap()
        .expect("failed to get next OnData event");

    // Send a Router Advertisement with information for a SLAAC prefix.
    let options = [NdpOptionBuilder::PrefixInformation(PrefixInformation::new(
        ipv6_consts::PREFIX.prefix(),  /* prefix_length */
        false,                         /* on_link_flag */
        true,                          /* autonomous_address_configuration_flag */
        99999,                         /* valid_lifetime */
        99999,                         /* preferred_lifetime */
        ipv6_consts::PREFIX.network(), /* prefix */
    ))];
    send_ra_with_router_lifetime(&fake_ep, 0, &options)
        .await
        .expect("failed to send router advertisement");

    // Wait for the SLAAC addresses to be generated.
    //
    // We expect two addresses for the SLAAC prefixes to be assigned to the NIC as the
    // netstack should generate both a stable and temporary SLAAC address.
    let interface_state = realm
        .connect_to_protocol::<fidl_fuchsia_net_interfaces::StateMarker>()
        .expect("failed to connect to fuchsia.net.interfaces/State");
    let expected_addrs = 2;
    fidl_fuchsia_net_interfaces_ext::wait_interface_with_id(
        fidl_fuchsia_net_interfaces_ext::event_stream_from_state(&interface_state)
            .expect("error getting interface state event stream"),
        &mut fidl_fuchsia_net_interfaces_ext::InterfaceState::Unknown(iface.id()),
        |fidl_fuchsia_net_interfaces_ext::Properties { addresses, .. }| {
            if addresses
                .iter()
                .filter_map(
                    |&fidl_fuchsia_net_interfaces_ext::Address {
                         addr: fidl_fuchsia_net::Subnet { addr, prefix_len: _ },
                         valid_until: _,
                     }| {
                        match addr {
                            net::IpAddress::Ipv4(net::Ipv4Address { .. }) => None,
                            net::IpAddress::Ipv6(net::Ipv6Address { addr }) => ipv6_consts::PREFIX
                                .contains(&net_types_ip::Ipv6Addr::from_bytes(addr))
                                .then_some(()),
                        }
                    },
                )
                .count()
                == expected_addrs as usize
            {
                Some(())
            } else {
                None
            }
        },
    )
    .map_err(anyhow::Error::from)
    .on_timeout(
        (EXPECTED_DAD_RETRANSMIT_TIMER * EXPECTED_DUP_ADDR_DETECT_TRANSMITS * expected_addrs
            + ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT)
            .after_now(),
        || Err(anyhow::anyhow!("timed out")),
    )
    .await
    .expect("failed to wait for SLAAC addresses to be generated")
}

/// Tests that if the netstack attempts to assign an address to an interface, and a remote node
/// is already assigned the address or attempts to assign the address at the same time, DAD
/// fails on the local interface.
///
/// If no remote node has any interest in an address the netstack is attempting to assign to
/// an interface, DAD should succeed.
#[variants_test]
async fn duplicate_address_detection<E: netemul::Endpoint>(name: &str) {
    /// Transmits a Neighbor Solicitation message and expects `ipv6_consts::LINK_LOCAL_ADDR`
    /// to not be assigned to the interface after the normal resolution time for DAD.
    async fn fail_dad_with_ns(fake_ep: &netemul::TestFakeEndpoint<'_>) {
        let snmc = ipv6_consts::LINK_LOCAL_ADDR.to_solicited_node_address();
        let () = write_ndp_message::<&[u8], _>(
            eth_consts::MAC_ADDR,
            Mac::from(&snmc),
            net_types_ip::Ipv6::UNSPECIFIED_ADDRESS,
            snmc.get(),
            NeighborSolicitation::new(ipv6_consts::LINK_LOCAL_ADDR),
            &[],
            fake_ep,
        )
        .await
        .expect("failed to write NDP message");
    }

    /// Transmits a Neighbor Advertisement message and expects `ipv6_consts::LINK_LOCAL_ADDR`
    /// to not be assigned to the interface after the normal resolution time for DAD.
    async fn fail_dad_with_na(fake_ep: &netemul::TestFakeEndpoint<'_>) {
        let () = write_ndp_message::<&[u8], _>(
            eth_consts::MAC_ADDR,
            Mac::from(&net_types_ip::Ipv6::ALL_NODES_LINK_LOCAL_MULTICAST_ADDRESS),
            ipv6_consts::LINK_LOCAL_ADDR,
            net_types_ip::Ipv6::ALL_NODES_LINK_LOCAL_MULTICAST_ADDRESS.get(),
            NeighborAdvertisement::new(
                false, /* router_flag */
                false, /* solicited_flag */
                false, /* override_flag */
                ipv6_consts::LINK_LOCAL_ADDR,
            ),
            &[NdpOptionBuilder::TargetLinkLayerAddress(&eth_consts::MAC_ADDR.bytes())],
            fake_ep,
        )
        .await
        .expect("failed to write NDP message");
    }

    // Wait for and verify a NS message transmitted by netstack for DAD.
    async fn expect_dad_neighbor_solicitation(fake_ep: &netemul::TestFakeEndpoint<'_>) {
        let ret = fake_ep
            .frame_stream()
            .try_filter_map(|(data, dropped)| {
                assert_eq!(dropped, 0);
                future::ok(
                    parse_icmp_packet_in_ip_packet_in_ethernet_frame::<
                        net_types_ip::Ipv6,
                        _,
                        NeighborSolicitation,
                        _,
                    >(&data, |p| assert_eq!(p.body().iter().count(), 0))
                    .map_or(None, |(_src_mac, dst_mac, src_ip, dst_ip, ttl, message, _code)| {
                        // If the NS is not for the address we just added, this is for some
                        // other address. We ignore it as it is not relevant to our test.
                        if message.target_address() != &ipv6_consts::LINK_LOCAL_ADDR {
                            return None;
                        }

                        Some((dst_mac, src_ip, dst_ip, ttl))
                    }),
                )
            })
            .try_next()
            .map(|r| r.context("error getting OnData event"))
            .on_timeout(ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT.after_now(), || {
                Err(anyhow::anyhow!(
                    "timed out waiting for a neighbor solicitation targetting {}",
                    ipv6_consts::LINK_LOCAL_ADDR
                ))
            })
            .await
            .unwrap()
            .expect("failed to get next OnData event");

        let (dst_mac, src_ip, dst_ip, ttl) = ret;
        let expected_dst = ipv6_consts::LINK_LOCAL_ADDR.to_solicited_node_address();
        assert_eq!(src_ip, net_types_ip::Ipv6::UNSPECIFIED_ADDRESS);
        assert_eq!(dst_ip, expected_dst.get());
        assert_eq!(dst_mac, Mac::from(&expected_dst));
        assert_eq!(ttl, NDP_MESSAGE_TTL);
    }

    type State = Result<
        fidl_fuchsia_net_interfaces_admin::AddressAssignmentState,
        fidl_fuchsia_net_interfaces_ext::admin::AddressStateProviderError,
    >;

    /// Adds `ipv6_consts::LINK_LOCAL_ADDR` to the interface and makes sure a Neighbor Solicitation
    /// message is transmitted by the netstack for DAD.
    ///
    /// Calls `fail_dad_fn` after the DAD message is observed so callers can simulate a remote
    /// node that has some interest in the same address.
    async fn add_address_for_dad<
        'a,
        'b: 'a,
        R: 'b + Future<Output = ()>,
        FN: FnOnce(&'b netemul::TestFakeEndpoint<'a>) -> R,
    >(
        iface: &'b netemul::TestInterface<'a>,
        fake_ep: &'b netemul::TestFakeEndpoint<'a>,
        control: &'b fidl_fuchsia_net_interfaces_admin::ControlProxy,
        interface_up: bool,
        dad_fn: FN,
    ) -> impl futures::stream::Stream<Item = State> {
        let (address_state_provider, server) = fidl::endpoints::create_proxy::<
            fidl_fuchsia_net_interfaces_admin::AddressStateProviderMarker,
        >()
        .expect("create AddressStateProvider proxy");
        // Create the state stream before adding the address to observe all events.
        let state_stream =
            fidl_fuchsia_net_interfaces_ext::admin::assignment_state_stream(address_state_provider);
        let () = control
            .add_address(
                &mut net::Subnet {
                    addr: net::IpAddress::Ipv6(net::Ipv6Address {
                        addr: ipv6_consts::LINK_LOCAL_ADDR.ipv6_bytes(),
                    }),
                    prefix_len: ipv6_consts::LINK_LOCAL_SUBNET_PREFIX,
                },
                fidl_fuchsia_net_interfaces_admin::AddressParameters::EMPTY,
                server,
            )
            .expect("Control.AddAddress FIDL error");
        if interface_up {
            // The first DAD message should be sent immediately.
            expect_dad_neighbor_solicitation(fake_ep).await;

            // Ensure that fuchsia.net.interfaces/Watcher doesn't erroneously report the
            // address as added before DAD completes successfully or otherwise.
            assert_eq!(
                iface.get_addrs().await.expect("failed to get addresses").into_iter().find(
                    |fidl_fuchsia_net_interfaces_ext::Address {
                         addr: fidl_fuchsia_net::Subnet { addr, prefix_len },
                         valid_until: _,
                     }| {
                        *prefix_len == ipv6_consts::LINK_LOCAL_SUBNET_PREFIX
                            && match addr {
                                fidl_fuchsia_net::IpAddress::Ipv4(
                                    fidl_fuchsia_net::Ipv4Address { .. },
                                ) => false,
                                fidl_fuchsia_net::IpAddress::Ipv6(
                                    fidl_fuchsia_net::Ipv6Address { addr },
                                ) => *addr == ipv6_consts::LINK_LOCAL_ADDR.ipv6_bytes(),
                            }
                    }
                ),
                None,
                "added IPv6 LL address already present even though it is tentative"
            );
        }
        dad_fn(fake_ep).await;
        state_stream
    }

    let sandbox = netemul::TestSandbox::new().expect("failed to create sandbox");
    let (_network, realm, _netstack, iface, fake_ep) =
        setup_network::<E>(&sandbox, name, None).await.expect("error setting up network");

    let debug_control = realm
        .connect_to_protocol::<fidl_fuchsia_net_debug::InterfacesMarker>()
        .expect("failed to connect to fuchsia.net.debug/Interfaces");
    let (control, server) =
        fidl::endpoints::create_proxy::<fidl_fuchsia_net_interfaces_admin::ControlMarker>()
            .expect("create proxy");
    let () = debug_control
        .get_admin(iface.id(), server)
        .expect("fuchsia.net.debug/Interfaces.GetAdmin failed");

    async fn dad_state(
        state_stream: &mut (impl futures::stream::Stream<Item = State> + std::marker::Unpin),
    ) -> State {
        // The address state provider doesn't buffer events, so we might see the tentative state,
        // but we might not.
        let state = match state_stream.by_ref().next().await.expect("state stream not ended") {
            Ok(fidl_fuchsia_net_interfaces_admin::AddressAssignmentState::Tentative) => {
                state_stream.by_ref().next().await.expect("state stream not ended")
            }
            state => state,
        };
        // Ensure errors are terminal.
        match state {
            Ok(_) => {}
            Err(_) => {
                assert_matches::assert_matches!(state_stream.by_ref().next().await, None)
            }
        }
        state
    }

    async fn assert_dad_failed(
        mut state_stream: (impl futures::stream::Stream<Item = State> + std::marker::Unpin),
    ) {
        assert_matches::assert_matches!(
            dad_state(&mut state_stream).await,
            Err(fidl_fuchsia_net_interfaces_ext::admin::AddressStateProviderError::AddressRemoved(
                fidl_fuchsia_net_interfaces_admin::AddressRemovalReason::DadFailed
            ))
        );
    }

    // Add an address and expect it to fail DAD because we simulate another node
    // performing DAD at the same time.
    {
        let state_stream =
            add_address_for_dad(&iface, &fake_ep, &control, true, fail_dad_with_ns).await;
        assert_dad_failed(state_stream).await;
    }
    // Add an address and expect it to fail DAD because we simulate another node
    // already owning the address.
    {
        let state_stream =
            add_address_for_dad(&iface, &fake_ep, &control, true, fail_dad_with_na).await;
        assert_dad_failed(state_stream).await;
    }

    async fn assert_dad_success(
        state_stream: &mut (impl futures::stream::Stream<Item = State> + std::marker::Unpin),
    ) {
        assert_matches::assert_matches!(
            dad_state(state_stream).await,
            Ok(fidl_fuchsia_net_interfaces_admin::AddressAssignmentState::Assigned)
        );
    }

    {
        // Add the address, and make sure it gets assigned.
        let mut state_stream =
            add_address_for_dad(&iface, &fake_ep, &control, true, |_| futures::future::ready(()))
                .await;

        assert_dad_success(&mut state_stream).await;

        // Disable the interface, ensure that the address becomes unavailable.
        let did_disable = iface.control().disable().await.expect("send disable").expect("disable");
        assert!(did_disable);

        assert_matches::assert_matches!(
            state_stream.by_ref().next().await,
            Some(Ok(fidl_fuchsia_net_interfaces_admin::AddressAssignmentState::Unavailable))
        );

        // Re-enable the interface, expect DAD to repeat and have it succeed.
        assert!(iface.control().enable().await.expect("send enable").expect("enable"));
        expect_dad_neighbor_solicitation(&fake_ep).await;
        assert_dad_success(&mut state_stream).await;

        let removed = control
            .remove_address(&mut net::Subnet {
                addr: net::IpAddress::Ipv6(net::Ipv6Address {
                    addr: ipv6_consts::LINK_LOCAL_ADDR.ipv6_bytes(),
                }),
                prefix_len: ipv6_consts::LINK_LOCAL_SUBNET_PREFIX,
            })
            .await
            .expect("FIDL error removing address")
            .expect("failed to remove address");
        assert!(removed);
    }

    // Disable the interface, this time add the address while it's down.
    let did_disable = iface.control().disable().await.expect("send disable").expect("disable");
    assert!(did_disable);
    let mut state_stream =
        add_address_for_dad(&iface, &fake_ep, &control, false, |_| futures::future::ready(()))
            .await;

    assert_matches::assert_matches!(
        state_stream.by_ref().next().await,
        Some(Ok(fidl_fuchsia_net_interfaces_admin::AddressAssignmentState::Unavailable))
    );

    // Re-enable the interface, DAD should run.
    let did_enable = iface.control().enable().await.expect("send enable").expect("enable");
    assert!(did_enable);

    expect_dad_neighbor_solicitation(&fake_ep).await;

    assert_dad_success(&mut state_stream).await;

    let addresses = iface.get_addrs().await.expect("addrs");
    assert!(
        addresses.iter().any(
            |&fidl_fuchsia_net_interfaces_ext::Address {
                 addr: fidl_fuchsia_net::Subnet { addr, prefix_len: _ },
                 valid_until: _,
             }| {
                match addr {
                    net::IpAddress::Ipv4(net::Ipv4Address { .. }) => false,
                    net::IpAddress::Ipv6(net::Ipv6Address { addr }) => {
                        addr == ipv6_consts::LINK_LOCAL_ADDR.ipv6_bytes()
                    }
                }
            }
        ),
        "addresses: {:?}",
        addresses
    );
}

/// Tests to make sure default router discovery, prefix discovery and more-specific
/// route discovery works.
#[variants_test]
#[test_case("host", false ; "host")]
#[test_case("router", true ; "router")]
async fn on_and_off_link_route_discovery<E: netemul::Endpoint>(
    test_name: &str,
    sub_test_name: &str,
    forwarding: bool,
) {
    pub const SUBNET_WITH_MORE_SPECIFIC_ROUTE: net_types_ip::Subnet<net_types_ip::Ipv6Addr> = unsafe {
        net_types_ip::Subnet::new_unchecked(
            net_types_ip::Ipv6Addr::new([0xa001, 0xf1f0, 0x4060, 0x0001, 0, 0, 0, 0]),
            64,
        )
    };

    async fn check_route_table(
        stack: &net_stack::StackProxy,
        want_routes: &[net_stack::ForwardingEntry],
    ) {
        let check_attempts = ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT.into_seconds()
            / ASYNC_EVENT_CHECK_INTERVAL.into_seconds();
        for attempt in 0..check_attempts {
            let () = sleep(ASYNC_EVENT_CHECK_INTERVAL.into_seconds()).await;
            let route_table =
                stack.get_forwarding_table().await.expect("failed to get route table");

            if want_routes.iter().all(|route| route_table.contains(route)) {
                return;
            }
            println!("route table at attempt={}:\n{:?}", attempt, route_table);
        }

        panic!(
            "timed out on waiting for a route table entry after {} seconds",
            ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT.into_seconds(),
        )
    }

    let name = format!("{}_{}", test_name, sub_test_name);
    let name = name.as_str();

    let sandbox = netemul::TestSandbox::new().expect("failed to create sandbox");
    const METRIC: u32 = 200;
    let (_network, realm, _netstack, iface, fake_ep) =
        setup_network::<E>(&sandbox, name, Some(METRIC)).await.expect("failed to setup network");

    let stack =
        realm.connect_to_protocol::<net_stack::StackMarker>().expect("failed to get stack proxy");

    if forwarding {
        enable_ipv6_forwarding(&iface).await;
    }

    let options = [
        NdpOptionBuilder::PrefixInformation(PrefixInformation::new(
            ipv6_consts::PREFIX.prefix(),  /* prefix_length */
            true,                          /* on_link_flag */
            false,                         /* autonomous_address_configuration_flag */
            6234,                          /* valid_lifetime */
            0,                             /* preferred_lifetime */
            ipv6_consts::PREFIX.network(), /* prefix */
        )),
        NdpOptionBuilder::RouteInformation(RouteInformation::new(
            SUBNET_WITH_MORE_SPECIFIC_ROUTE,
            1337, /* route_lifetime_seconds */
            RoutePreference::default(),
        )),
    ];
    let () = send_ra_with_router_lifetime(&fake_ep, 1234, &options)
        .await
        .expect("failed to send router advertisement");

    let nicid = iface.id();
    check_route_table(
        &stack,
        &[
            // Test that a default route through the router is installed.
            net_stack::ForwardingEntry {
                subnet: net::Subnet {
                    addr: net::IpAddress::Ipv6(net::Ipv6Address {
                        addr: net_types_ip::Ipv6::UNSPECIFIED_ADDRESS.ipv6_bytes(),
                    }),
                    prefix_len: 0,
                },
                device_id: nicid,
                next_hop: Some(Box::new(net::IpAddress::Ipv6(net::Ipv6Address {
                    addr: ipv6_consts::LINK_LOCAL_ADDR.ipv6_bytes(),
                }))),
                metric: METRIC,
            },
            // Test that a route to `SUBNET_WITH_MORE_SPECIFIC_ROUTE` exists through the router.
            net_stack::ForwardingEntry {
                subnet: net::Subnet {
                    addr: net::IpAddress::Ipv6(net::Ipv6Address {
                        addr: SUBNET_WITH_MORE_SPECIFIC_ROUTE.network().ipv6_bytes(),
                    }),
                    prefix_len: SUBNET_WITH_MORE_SPECIFIC_ROUTE.prefix(),
                },
                device_id: nicid,
                next_hop: Some(Box::new(net::IpAddress::Ipv6(net::Ipv6Address {
                    addr: ipv6_consts::LINK_LOCAL_ADDR.ipv6_bytes(),
                }))),
                metric: METRIC,
            },
            // Test that the prefix should be discovered after it is advertised.
            net_stack::ForwardingEntry {
                subnet: net::Subnet {
                    addr: net::IpAddress::Ipv6(net::Ipv6Address {
                        addr: ipv6_consts::PREFIX.network().ipv6_bytes(),
                    }),
                    prefix_len: ipv6_consts::PREFIX.prefix(),
                },
                device_id: nicid,
                next_hop: None,
                metric: METRIC,
            },
        ][..],
    )
    .await
}

#[variants_test]
async fn slaac_regeneration_after_dad_failure<E: netemul::Endpoint>(name: &str) {
    // Expects an NS message for DAD within timeout and returns the target address of the message.
    async fn expect_ns_message_in(
        fake_ep: &netemul::TestFakeEndpoint<'_>,
        timeout: zx::Duration,
    ) -> net_types_ip::Ipv6Addr {
        fake_ep
            .frame_stream()
            .try_filter_map(|(data, dropped)| {
                assert_eq!(dropped, 0);
                future::ok(
                    parse_icmp_packet_in_ip_packet_in_ethernet_frame::<
                        net_types_ip::Ipv6,
                        _,
                        NeighborSolicitation,
                        _,
                    >(&data, |p| assert_eq!(p.body().iter().count(), 0))
                        .map_or(None, |(_src_mac, _dst_mac, _src_ip, _dst_ip, _ttl, message, _code)| {
                            // If the NS target_address does not have the prefix we have advertised,
                            // this is for some other address. We ignore it as it is not relevant to
                            // our test.
                            if !ipv6_consts::PREFIX.contains(message.target_address()) {
                                return None;
                            }

                            Some(*message.target_address())
                        }),
                )
            })
            .try_next()
            .map(|r| r.context("error getting OnData event"))
            .on_timeout(timeout.after_now(), || {
                Err(anyhow::anyhow!(
                    "timed out waiting for a neighbor solicitation targetting address of prefix: {}",
                    ipv6_consts::PREFIX,
                ))
            })
            .await.unwrap().expect("failed to get next OnData event")
    }

    let sandbox = netemul::TestSandbox::new().expect("failed to create sandbox");
    let (_network, realm, _netstack, iface, fake_ep) =
        setup_network_with::<E, _>(&sandbox, name, None, &[KnownServiceProvider::SecureStash])
            .await
            .expect("error setting up network");

    // Send a Router Advertisement with information for a SLAAC prefix.
    let options = [NdpOptionBuilder::PrefixInformation(PrefixInformation::new(
        ipv6_consts::PREFIX.prefix(),  /* prefix_length */
        false,                         /* on_link_flag */
        true,                          /* autonomous_address_configuration_flag */
        99999,                         /* valid_lifetime */
        99999,                         /* preferred_lifetime */
        ipv6_consts::PREFIX.network(), /* prefix */
    ))];
    send_ra_with_router_lifetime(&fake_ep, 0, &options)
        .await
        .expect("failed to send router advertisement");

    let tried_address = expect_ns_message_in(&fake_ep, ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT).await;

    // We pretend there is a duplicate address situation.
    let snmc = tried_address.to_solicited_node_address();
    let () = write_ndp_message::<&[u8], _>(
        eth_consts::MAC_ADDR,
        Mac::from(&snmc),
        net_types_ip::Ipv6::UNSPECIFIED_ADDRESS,
        snmc.get(),
        NeighborSolicitation::new(tried_address),
        &[],
        &fake_ep,
    )
    .await
    .expect("failed to write DAD message");

    let target_address =
        expect_ns_message_in(&fake_ep, DAD_IDGEN_DELAY + ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT).await;

    // We expect two addresses for the SLAAC prefixes to be assigned to the NIC as the
    // netstack should generate both a stable and temporary SLAAC address.
    let expected_addrs = 2;
    let interface_state = realm
        .connect_to_protocol::<fidl_fuchsia_net_interfaces::StateMarker>()
        .expect("failed to connect to fuchsia.net.interfaces/State");
    let () = fidl_fuchsia_net_interfaces_ext::wait_interface_with_id(
        fidl_fuchsia_net_interfaces_ext::event_stream_from_state(&interface_state)
            .expect("error getting interfaces state event stream"),
        &mut fidl_fuchsia_net_interfaces_ext::InterfaceState::Unknown(iface.id()),
        |fidl_fuchsia_net_interfaces_ext::Properties { addresses, .. }| {
            // We have to make sure 2 things:
            // 1. We have `expected_addrs` addrs which have the advertised prefix for the
            // interface.
            // 2. The last tried address should be among the addresses for the interface.
            let (slaac_addrs, has_target_addr) = addresses.iter().fold(
                (0, false),
                |(mut slaac_addrs, mut has_target_addr),
                 &fidl_fuchsia_net_interfaces_ext::Address {
                     addr: fidl_fuchsia_net::Subnet { addr, prefix_len: _ },
                     valid_until: _,
                 }| {
                    match addr {
                        net::IpAddress::Ipv4(net::Ipv4Address { .. }) => {}
                        net::IpAddress::Ipv6(net::Ipv6Address { addr }) => {
                            let configured_addr = net_types_ip::Ipv6Addr::from_bytes(addr);
                            assert_ne!(
                                configured_addr, tried_address,
                                "address which previously failed DAD was assigned"
                            );
                            if ipv6_consts::PREFIX.contains(&configured_addr) {
                                slaac_addrs += 1;
                            }
                            if configured_addr == target_address {
                                has_target_addr = true;
                            }
                        }
                    }
                    (slaac_addrs, has_target_addr)
                },
            );

            assert!(
                slaac_addrs <= expected_addrs,
                "more addresses found than expected, found {}, expected {}",
                slaac_addrs,
                expected_addrs
            );
            if slaac_addrs == expected_addrs && has_target_addr {
                Some(())
            } else {
                None
            }
        },
    )
    .map_err(anyhow::Error::from)
    .on_timeout(
        (EXPECTED_DAD_RETRANSMIT_TIMER * EXPECTED_DUP_ADDR_DETECT_TRANSMITS * expected_addrs
            + ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT)
            .after_now(),
        || Err(anyhow::anyhow!("timed out")),
    )
    .await
    .expect("failed to wait for SLAAC addresses");
}

#[variants_test]
async fn sends_mld_reports<E: netemul::Endpoint>(name: &str) {
    let sandbox = netemul::TestSandbox::new().expect("error creating sandbox");
    let (_network, _realm, _netstack, iface, fake_ep) =
        setup_network::<E>(&sandbox, name, None).await.expect("error setting up networking");

    // Add an address so we join the address's solicited node multicast group.
    let _address_state_provider = interfaces::add_subnet_address_and_route_wait_assigned(
        &iface,
        net::Subnet {
            addr: net::IpAddress::Ipv6(net::Ipv6Address {
                addr: ipv6_consts::LINK_LOCAL_ADDR.ipv6_bytes(),
            }),
            prefix_len: 64,
        },
        fidl_fuchsia_net_interfaces_admin::AddressParameters::EMPTY,
    )
    .await
    .expect("add subnet address and route");
    let snmc = ipv6_consts::LINK_LOCAL_ADDR.to_solicited_node_address();

    let stream = fake_ep
        .frame_stream()
        .map(|r| r.context("error getting OnData event"))
        .try_filter_map(|(data, dropped)| {
            async move {
                assert_eq!(dropped, 0);
                let mut data = &data[..];

                let eth = EthernetFrame::parse(&mut data, EthernetFrameLengthCheck::Check)
                    .expect("error parsing ethernet frame");

                if eth.ethertype() != Some(EtherType::Ipv6) {
                    // Ignore non-IPv6 packets.
                    return Ok(None);
                }

                let (mut payload, src_ip, dst_ip, proto, ttl) =
                    parse_ip_packet::<net_types_ip::Ipv6>(&data)
                        .expect("error parsing IPv6 packet");

                if proto != Ipv6Proto::Icmpv6 {
                    // Ignore non-ICMPv6 packets.
                    return Ok(None);
                }

                let icmp = Icmpv6Packet::parse(&mut payload, IcmpParseArgs::new(src_ip, dst_ip))
                    .expect("error parsing ICMPv6 packet");

                let mld = if let Icmpv6Packet::Mld(mld) = icmp {
                    mld
                } else {
                    // Ignore non-MLD packets.
                    return Ok(None);
                };

                // As per RFC 3590 section 4,
                //
                //   MLD Report and Done messages are sent with a link-local address as
                //   the IPv6 source address, if a valid address is available on the
                //   interface. If a valid link-local address is not available (e.g., one
                //   has not been configured), the message is sent with the unspecified
                //   address (::) as the IPv6 source address.
                assert!(!src_ip.is_specified() || src_ip.is_link_local(), "MLD messages must be sent from the unspecified or link local address; src_ip = {}", src_ip);

                assert!(dst_ip.is_multicast(), "all MLD messages must be sent to a multicast address; dst_ip = {}", dst_ip);

                // As per RFC 2710 section 3,
                //
                //   All MLD messages described in this document are sent with a
                //   link-local IPv6 Source Address, an IPv6 Hop Limit of 1, ...
                assert_eq!(ttl, 1, "MLD messages must have a hop limit of 1");

                let report = if let MldPacket::MulticastListenerReport(report) = mld {
                    report
                } else {
                    // Ignore non-report messages.
                    return Ok(None);
                };

                let group_addr = report.body().group_addr;
                assert!(group_addr.is_multicast(), "MLD reports must only be sent for multicast addresses; group_addr = {}", group_addr);

                if group_addr != *snmc {
                    // We are only interested in the report for the solicited node
                    // multicast group we joined.
                    return Ok(None);
                }

                assert_eq!(dst_ip, group_addr, "the destination of an MLD report should be the multicast group the report is for");

                Ok(Some(()))
            }
        });
    futures::pin_mut!(stream);
    let () = stream
        .try_next()
        .on_timeout(ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT.after_now(), || {
            return Err(anyhow::anyhow!("timed out waiting for the MLD report"));
        })
        .await
        .unwrap()
        .expect("error getting our expected MLD report");
}

#[fuchsia_async::run_singlethreaded(test)]
async fn sending_ra_with_autoconf_flag_triggers_slaac() {
    let name = "sending_ra_with_onlink_triggers_autoconf";
    let sandbox = netemul::TestSandbox::new().expect("error creating sandbox");
    let (_network, realm, _netstack, iface, _fake_ep) =
        setup_network::<netemul::NetworkDevice>(&sandbox, name, None)
            .await
            .expect("error setting up networking");

    let interfaces_state = &realm
        .connect_to_protocol::<fidl_fuchsia_net_interfaces::StateMarker>()
        .expect("connect to protocol");

    let src_ip = netstack_testing_common::interfaces::wait_for_v6_ll(&interfaces_state, iface.id())
        .await
        .expect("waiting for link local address");
    let dst_ip = net_types_ip::Ipv6::ALL_NODES_LINK_LOCAL_MULTICAST_ADDRESS;

    let sock = realm
        .raw_socket(
            fposix_socket::Domain::Ipv6,
            fposix_socket_raw::ProtocolAssociation::Associated(
                packet_formats::ip::Ipv6Proto::Icmpv6.into(),
            ),
        )
        .await
        .expect("create raw socket");

    let options = [NdpOptionBuilder::PrefixInformation(PrefixInformation::new(
        ipv6_consts::PREFIX.prefix(),  /* prefix_length */
        true,                          /* on_link_flag */
        true,                          /* autonomous_address_configuration_flag */
        6234,                          /* valid_lifetime */
        0,                             /* preferred_lifetime */
        ipv6_consts::PREFIX.network(), /* prefix */
    ))];
    let ra = RouterAdvertisement::new(
        0,     /* current_hop_limit */
        false, /* managed_flag */
        false, /* other_config_flag */
        1234,  /* router_lifetime */
        0,     /* reachable_time */
        0,     /* retransmit_timer */
    );

    let msg = packet_formats::icmp::ndp::OptionSequenceBuilder::<_>::new(options.iter())
        .into_serializer()
        .encapsulate(packet_formats::icmp::IcmpPacketBuilder::<_, &[u8], _>::new(
            src_ip,
            dst_ip,
            packet_formats::icmp::IcmpUnusedCode,
            ra,
        ))
        .serialize_vec_outer()
        .expect("failed to serialize NDP packet")
        .unwrap_b();

    // NDP requires that this be the hop limit.
    let () = sock.set_multicast_hops_v6(255).expect("set multicast hops");

    let written = sock
        .send_to(
            msg.as_ref(),
            &std::net::SocketAddrV6::new((*dst_ip).into(), 0, 0, iface.id().try_into().unwrap())
                .into(),
        )
        .expect("failed to write to socket");
    assert_eq!(written, msg.as_ref().len());

    fidl_fuchsia_net_interfaces_ext::wait_interface_with_id(
        fidl_fuchsia_net_interfaces_ext::event_stream_from_state(&interfaces_state)
            .expect("creating interface event stream"),
        &mut fidl_fuchsia_net_interfaces_ext::InterfaceState::Unknown(iface.id()),
        |fidl_fuchsia_net_interfaces_ext::Properties {
             id: _,
             name: _,
             device_class: _,
             online: _,
             addresses,
             has_default_ipv4_route: _,
             has_default_ipv6_route: _,
         }| {
            addresses.into_iter().find_map(
                |fidl_fuchsia_net_interfaces_ext::Address {
                     addr: fidl_fuchsia_net::Subnet { addr, prefix_len: _ },
                     valid_until: _,
                 }| {
                    let addr = match addr {
                        fidl_fuchsia_net::IpAddress::Ipv4(fidl_fuchsia_net::Ipv4Address {
                            ..
                        }) => {
                            return None;
                        }
                        fidl_fuchsia_net::IpAddress::Ipv6(fidl_fuchsia_net::Ipv6Address {
                            addr,
                        }) => net_types_ip::Ipv6Addr::from_bytes(*addr),
                    };
                    ipv6_consts::PREFIX.contains(&addr).then(|| ())
                },
            )
        },
    )
    .await
    .expect("error waiting for address assignment");
}

#[variants_test]
async fn add_device_adds_link_local_subnet_route<N: Netstack, E: netemul::Endpoint>(name: &str) {
    let test_is_ns3_eth = N::VERSION == NetstackVersion::Netstack3
        && E::NETEMUL_BACKING == fnetemul_network::EndpointBacking::Ethertap;

    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let realm = sandbox.create_netstack_realm::<N, _>(name).expect("create realm");
    let endpoint = sandbox.create_endpoint::<E, _>(name).await.expect("create endpoint");
    endpoint.set_link_up(true).await.expect("set link up");
    let iface = endpoint.into_interface_in_realm(&realm).await.expect("install interface");
    let did_enable = iface.control().enable().await.expect("calling enable").expect("enable");
    if test_is_ns3_eth {
        // Ethernet devices start enabled in NS3.
        assert!(!did_enable);
    } else {
        assert!(did_enable);
    }

    let id = iface.id();

    // TODO(https://fxbug.dev/101842): Replace this with a proper routes API
    // that we do not have to poll over. For now, the only flake-safe way of
    // going about this is to poll the API.
    let stack = realm
        .connect_to_protocol::<fidl_fuchsia_net_stack::StackMarker>()
        .expect("connect to protocol)");
    let forwarding_table_stream = futures::stream::unfold(stack, |stack| async move {
        let table = stack.get_forwarding_table().await.expect("get forwarding table");
        Some((table, stack))
    });
    futures::pin_mut!(forwarding_table_stream);

    forwarding_table_stream
        .by_ref()
        .filter_map(|forwarding_table| {
            futures::future::ready(
                forwarding_table
                    .into_iter()
                    .any(
                        |fidl_fuchsia_net_stack::ForwardingEntry {
                             subnet,
                             device_id,
                             next_hop,
                             metric: _,
                         }| {
                            device_id == id
                                && next_hop.is_none()
                                && subnet == net_declare::fidl_subnet!("fe80::/64")
                        },
                    )
                    .then(|| ()),
            )
        })
        .next()
        .await
        .expect("stream ended");

    // NS3 does not remove Ethernet interfaces on device destruction; skip the rest of the test.
    if test_is_ns3_eth {
        return;
    }

    // Removing the device should also remove the subnet route.
    drop(iface);

    forwarding_table_stream
        .by_ref()
        .filter_map(|forwarding_table| {
            futures::future::ready(
                forwarding_table
                    .into_iter()
                    .all(
                        |fidl_fuchsia_net_stack::ForwardingEntry {
                             subnet: _,
                             device_id,
                             next_hop: _,
                             metric: _,
                         }| { device_id != id },
                    )
                    .then(|| ()),
            )
        })
        .next()
        .await
        .expect("stream ended");
}
