// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use std::collections::HashMap;
use std::convert::From as _;

use fuchsia_async as fasync;

use anyhow::Context as _;
use futures::{FutureExt as _, StreamExt as _, TryFutureExt as _, TryStreamExt as _};
use net_declare::{fidl_ip, fidl_mac};
use net_types::SpecifiedAddress;
use netemul::{
    Endpoint as _, EnvironmentUdpSocket as _, TestEnvironment, TestInterface, TestNetwork,
    TestSandbox,
};
use netstack_testing_common::environments::*;
use netstack_testing_common::Result;

const ALICE_MAC: fidl_fuchsia_net::MacAddress = fidl_mac!("02:00:01:02:03:04");
const ALICE_IP: fidl_fuchsia_net::IpAddress = fidl_ip!(192.168.0.100);
const BOB_MAC: fidl_fuchsia_net::MacAddress = fidl_mac!("02:0A:0B:0C:0D:0E");
const BOB_IP: fidl_fuchsia_net::IpAddress = fidl_ip!(192.168.0.1);
const SUBNET_PREFIX: u8 = 24;

/// Helper type holding values pertinent to neighbor tests.
struct NeighborEnvironment<'a> {
    env: TestEnvironment<'a>,
    ep: TestInterface<'a>,
    ipv6: fidl_fuchsia_net::IpAddress,
    loopback_id: u64,
}

/// Helper function to create an environment with a static IP address and an
/// endpoint with a set MAC address.
///
/// Returns the created environment, ep, the first observed assigned IPv6
/// address, and the id of the loopback interface.
async fn create_environment<'a>(
    sandbox: &'a TestSandbox,
    network: &'a TestNetwork<'a>,
    test_name: &'static str,
    variant_name: &'static str,
    static_addr: fidl_fuchsia_net::Subnet,
    mac: fidl_fuchsia_net::MacAddress,
) -> Result<NeighborEnvironment<'a>> {
    let env = sandbox
        .create_netstack_environment::<Netstack2, _>(format!("{}_{}", test_name, variant_name))
        .context("failed to create environment")?;
    let ep = env
        .join_network_with(
            &network,
            format!("ep-{}", variant_name),
            netemul::NetworkDevice::make_config(netemul::DEFAULT_MTU, Some(mac)),
            &netemul::InterfaceConfig::StaticIp(static_addr),
        )
        .await
        .context("failed to join network")?;

    // Get IPv6 address.
    let interfaces = env
        .connect_to_service::<fidl_fuchsia_net_interfaces::StateMarker>()
        .context("failed to connect to interfaces.State")?;
    let (ipv6, loopback_id) =
        fidl_fuchsia_net_interfaces_ext::wait_interface(
            fidl_fuchsia_net_interfaces_ext::event_stream_from_state(&interfaces)
                .context("failed to get interfaces stream")?,
            &mut std::collections::HashMap::<u64, fidl_fuchsia_net_interfaces::Properties>::new(),
            |interfaces| {
                let addr = interfaces.get(&ep.id())?.addresses.as_ref()?.iter().find_map(
                    |addr| match addr.addr?.addr {
                        a @ fidl_fuchsia_net::IpAddress::Ipv6(_) => Some(a.clone()),
                        fidl_fuchsia_net::IpAddress::Ipv4(_) => None,
                    },
                )?;
                let loopback_id = interfaces.iter().find_map(|(id, props)| {
                    if let Some(fidl_fuchsia_net_interfaces::DeviceClass::Loopback(
                        fidl_fuchsia_net_interfaces::Empty {},
                    )) = props.device_class
                    {
                        Some(*id)
                    } else {
                        None
                    }
                })?;
                Some((addr, loopback_id))
            },
        )
        .await
        .context("failed to retrieve IPv6 address")?;

    Ok(NeighborEnvironment { env, ep, ipv6, loopback_id })
}

/// Helper function that creates two environments in the same `network` with
/// default test parameters. Returns a tuple of environments `(alice, bob)`.
async fn create_neighbor_environments<'a>(
    sandbox: &'a TestSandbox,
    network: &'a TestNetwork<'a>,
    test_name: &'static str,
) -> Result<(NeighborEnvironment<'a>, NeighborEnvironment<'a>)> {
    let alice = create_environment(
        sandbox,
        network,
        test_name,
        "alice",
        fidl_fuchsia_net::Subnet { addr: ALICE_IP, prefix_len: SUBNET_PREFIX },
        ALICE_MAC,
    )
    .await
    .context("failed to setup alice environment")?;
    let bob = create_environment(
        sandbox,
        network,
        test_name,
        "bob",
        fidl_fuchsia_net::Subnet { addr: BOB_IP, prefix_len: SUBNET_PREFIX },
        BOB_MAC,
    )
    .await
    .context("failed to set up bob environment")?;
    Ok((alice, bob))
}

/// Gets a neighbor entry iterator stream with `options` in `env`.
fn get_entry_iterator(
    env: &TestEnvironment<'_>,
    options: fidl_fuchsia_net_neighbor::EntryIteratorOptions,
) -> Result<impl futures::Stream<Item = Result<fidl_fuchsia_net_neighbor::EntryIteratorItem>>> {
    let view = env
        .connect_to_service::<fidl_fuchsia_net_neighbor::ViewMarker>()
        .context("failed to connect to fuchsia.net.neighbor/View")?;
    let (proxy, server_end) =
        fidl::endpoints::create_proxy::<fidl_fuchsia_net_neighbor::EntryIteratorMarker>()
            .context("failed to create EntryIterator proxy")?;
    let () =
        view.open_entry_iterator(server_end, options).context("failed to open EntryIterator")?;
    Ok(futures::stream::try_unfold(proxy, |proxy| {
        proxy
            .get_next()
            .map(|r| r.context("fuchsia.net.neighbor/EntryIterator.GetNext FIDL error"))
            .map_ok(|it| Some((futures::stream::iter(it.into_iter().map(Result::Ok)), proxy)))
    })
    .try_flatten())
}

/// Retrieves all existing neighbor entries in `env`.
///
/// Entries are identified by unique `(interface_id, ip_address)` tuples in the
/// returned map.
async fn list_existing_entries(
    env: &TestEnvironment<'_>,
) -> Result<HashMap<(u64, fidl_fuchsia_net::IpAddress), fidl_fuchsia_net_neighbor::Entry>> {
    use async_utils::fold::*;
    try_fold_while(
        get_entry_iterator(env, fidl_fuchsia_net_neighbor::EntryIteratorOptions::empty())?,
        HashMap::new(),
        |mut map, item| {
            futures::future::ready(match item {
                fidl_fuchsia_net_neighbor::EntryIteratorItem::Existing(e) => {
                    if let fidl_fuchsia_net_neighbor::Entry {
                        interface: Some(interface),
                        neighbor: Some(neighbor),
                        ..
                    } = &e
                    {
                        if let Some(e) = map.insert((*interface, neighbor.clone()), e) {
                            Err(anyhow::anyhow!("duplicate entry detected in map: {:?}", e))
                        } else {
                            Ok(FoldWhile::Continue(map))
                        }
                    } else {
                        Err(anyhow::anyhow!(
                            "missing interface or neighbor in existing entry: {:?}",
                            e
                        ))
                    }
                }
                fidl_fuchsia_net_neighbor::EntryIteratorItem::Idle(
                    fidl_fuchsia_net_neighbor::IdleEvent {},
                ) => Ok(FoldWhile::Done(map)),
                x @ fidl_fuchsia_net_neighbor::EntryIteratorItem::Added(_)
                | x @ fidl_fuchsia_net_neighbor::EntryIteratorItem::Changed(_)
                | x @ fidl_fuchsia_net_neighbor::EntryIteratorItem::Removed(_) => {
                    Err(anyhow::anyhow!("unexpected EntryIteratorItem before Idle: {:?}", x))
                }
            })
        },
    )
    .await
    .and_then(|r| {
        r.short_circuited().map_err(|e| {
            anyhow::anyhow!("entry iterator stream ended unexpectedly with state {:?}", e)
        })
    })
}

/// Helper function to exchange a single UDP datagram.
///
/// `alice` will send a single UDP datagram to `bob`. This function will block
/// until `bob` receives the datagram.
async fn exchange_dgram(
    alice: &TestEnvironment<'_>,
    alice_addr: fidl_fuchsia_net::IpAddress,
    bob: &TestEnvironment<'_>,
    bob_addr: fidl_fuchsia_net::IpAddress,
) -> Result {
    let fidl_fuchsia_net_ext::IpAddress(alice_addr) =
        fidl_fuchsia_net_ext::IpAddress::from(alice_addr);
    let alice_addr = std::net::SocketAddr::new(alice_addr, 1234);

    let fidl_fuchsia_net_ext::IpAddress(bob_addr) = fidl_fuchsia_net_ext::IpAddress::from(bob_addr);
    let bob_addr = std::net::SocketAddr::new(bob_addr, 8080);

    let alice_sock = fuchsia_async::net::UdpSocket::bind_in_env(alice, alice_addr)
        .await
        .context("failed to create client socket")?;

    let bob_sock = fuchsia_async::net::UdpSocket::bind_in_env(bob, bob_addr)
        .await
        .context("failed to create server socket")?;

    const PAYLOAD: &'static str = "Hello Neighbor";
    let mut buf = [0u8; 512];
    let (sent, (rcvd, from)) = futures::future::try_join(
        alice_sock.send_to(PAYLOAD.as_bytes(), bob_addr).map(|r| r.context("UDP send_to failed")),
        bob_sock.recv_from(&mut buf[..]).map(|r| r.context("UDP recv_from failed")),
    )
    .await?;
    assert_eq!(sent, PAYLOAD.as_bytes().len());
    assert_eq!(rcvd, PAYLOAD.as_bytes().len());
    assert_eq!(&buf[..rcvd], PAYLOAD.as_bytes());
    // Check equality on IP and port separately since for IPv6 the scope ID may
    // differ, making a direct equality fail.
    assert_eq!(from.ip(), alice_addr.ip());
    assert_eq!(from.port(), alice_addr.port());
    Ok(())
}

/// Helper function to exchange an IPv4 and IPv6 datagram between `alice` and
/// `bob`.
async fn exchange_dgrams(
    alice: &NeighborEnvironment<'_>,
    bob: &NeighborEnvironment<'_>,
) -> Result<()> {
    let () = exchange_dgram(&alice.env, ALICE_IP, &bob.env, BOB_IP)
        .await
        .context("IPv4 exchange failed")?;

    let () = exchange_dgram(&alice.env, alice.ipv6, &bob.env, bob.ipv6)
        .await
        .context("IPv6 exchange failed")?;

    Ok(())
}

/// Helper function to assert validity of a reachable entry.
fn assert_reachable_entry(
    entry: fidl_fuchsia_net_neighbor::Entry,
    match_interface: u64,
    match_neighbor: fidl_fuchsia_net::IpAddress,
    match_mac: fidl_fuchsia_net::MacAddress,
) {
    matches::assert_matches!(
        entry,
        fidl_fuchsia_net_neighbor::Entry {
            interface: Some(interface),
            neighbor: Some(neighbor),
            state: Some(fidl_fuchsia_net_neighbor::EntryState::Reachable(
                // TODO(fxbug.dev/59372): Capture and assert expiration
                // value
                fidl_fuchsia_net_neighbor::ReachableState { expires_at: None, .. },
            )),
            mac: Some(mac),
            updated_at: Some(updated_at), ..
        } if interface == match_interface && neighbor == match_neighbor && mac == match_mac && updated_at != 0
    );
}

/// Helper function to assert validity of a stale entry.
fn assert_stale_entry(
    entry: fidl_fuchsia_net_neighbor::Entry,
    match_interface: u64,
    match_neighbor: fidl_fuchsia_net::IpAddress,
    match_mac: fidl_fuchsia_net::MacAddress,
) {
    matches::assert_matches!(
        entry,
        fidl_fuchsia_net_neighbor::Entry {
            interface: Some(interface),
            neighbor: Some(neighbor),
            state: Some(fidl_fuchsia_net_neighbor::EntryState::Stale(
                fidl_fuchsia_net_neighbor::StaleState { .. },
            )),
            mac: Some(mac),
            updated_at: Some(updated_at), ..
        } if interface == match_interface && neighbor == match_neighbor && mac == match_mac && updated_at != 0
    );
}

/// Helper function to assert validity of a static entry.
fn assert_static_entry(
    entry: fidl_fuchsia_net_neighbor::Entry,
    match_interface: u64,
    match_neighbor: fidl_fuchsia_net::IpAddress,
    match_mac: fidl_fuchsia_net::MacAddress,
) {
    matches::assert_matches!(
        entry,
        fidl_fuchsia_net_neighbor::Entry {
            interface: Some(interface),
            neighbor: Some(neighbor),
            state: Some(fidl_fuchsia_net_neighbor::EntryState::Static_(
                fidl_fuchsia_net_neighbor::StaticState { .. },
            )),
            mac: Some(mac),
            updated_at: Some(updated_at), ..
        } if interface == match_interface && neighbor == match_neighbor && mac == match_mac && updated_at != 0
    );
}

#[fasync::run_singlethreaded(test)]
async fn neigh_list_entries() -> Result {
    // TODO(fxbug.dev/59425): Extend this test with hanging get.
    let sandbox = TestSandbox::new().context("failed to create sandbox")?;
    let network = sandbox.create_network("net").await.context("failed to create network")?;

    let (alice, bob) = create_neighbor_environments(&sandbox, &network, "neigh_list_entries")
        .await
        .context("failed to setup environments")?;

    // No Neighbors should exist initially.
    let alice_entries =
        list_existing_entries(&alice.env).await.context("failed to get entries for alice")?;
    assert!(alice_entries.is_empty(), "expected empty set of entries: {:?}", alice_entries);
    let bob_entries =
        list_existing_entries(&bob.env).await.context("failed to get entries for bob")?;
    assert!(bob_entries.is_empty(), "expected empty set of entries: {:?}", bob_entries);

    // Send a single UDP datagram between alice and bob.
    let () = exchange_dgram(&alice.env, ALICE_IP, &bob.env, BOB_IP)
        .await
        .context("IPv4 exchange failed")?;
    let () = exchange_dgram(&alice.env, alice.ipv6, &bob.env, bob.ipv6)
        .await
        .context("IPv6 exchange failed")?;

    // Check that bob is listed as a neighbor for alice.
    let mut alice_entries =
        list_existing_entries(&alice.env).await.context("failed to get entries for alice")?;
    // IPv4 entry.
    let () = assert_reachable_entry(
        alice_entries.remove(&(alice.ep.id(), BOB_IP)).context("missing IPv4 neighbor entry")?,
        alice.ep.id(),
        BOB_IP,
        BOB_MAC,
    );
    // IPv6 entry.
    let () = assert_reachable_entry(
        alice_entries.remove(&(alice.ep.id(), bob.ipv6)).context("missing IPv6 neighbor entry")?,
        alice.ep.id(),
        bob.ipv6,
        BOB_MAC,
    );
    assert!(
        alice_entries.is_empty(),
        "unexpected neighbors remaining in list: {:?}",
        alice_entries
    );

    // Check that alice is listed as a neighbor for bob. Bob should have alice
    // listed as STALE entries due to having received solicitations as part of
    // the UDP exchange.
    let mut bob_entries =
        list_existing_entries(&bob.env).await.context("failed to get entries for bob")?;

    // IPv4 entry.
    let () = assert_stale_entry(
        bob_entries.remove(&(bob.ep.id(), ALICE_IP)).context("missing IPv4 neighbor entry")?,
        bob.ep.id(),
        ALICE_IP,
        ALICE_MAC,
    );
    // IPv6 entry.
    let () = assert_stale_entry(
        bob_entries.remove(&(bob.ep.id(), alice.ipv6)).context("missing IPv6 neighbor entry")?,
        bob.ep.id(),
        alice.ipv6,
        ALICE_MAC,
    );
    assert!(bob_entries.is_empty(), "unexpected neighbors remaining in list: {:?}", bob_entries);

    Ok(())
}

/// Frame metadata of interest to neighbor tests.
#[derive(Debug, Eq, PartialEq)]
enum FrameMetadata {
    /// An ARP request or NDP Neighbor Solicitation target address.
    NeighborSolicitation(fidl_fuchsia_net::IpAddress),
    /// A UDP datagram destined to the address.
    Udp(fidl_fuchsia_net::IpAddress),
    /// Any other succesfully parsed frame.
    Other,
}

/// Helper function to extract specific frame metadata from a raw Ethernet
/// frame.
///
/// Returns `Err` if the frame can't be parsed or `Ok(FrameMetadata)` with any
/// interesting metadata of interest to neighbor tests.
fn extract_frame_metadata(data: Vec<u8>) -> Result<FrameMetadata> {
    use packet::ParsablePacket;
    use packet_formats::{
        arp::{ArpOp, ArpPacket},
        ethernet::{EtherType, EthernetFrame, EthernetFrameLengthCheck},
        icmp::{ndp::NdpPacket, IcmpParseArgs, Icmpv6Packet},
        ip::IpProto,
        ipv4::{Ipv4Header, Ipv4Packet},
        ipv6::Ipv6Packet,
    };

    let mut bv = &data[..];
    let ethernet = EthernetFrame::parse(&mut bv, EthernetFrameLengthCheck::NoCheck)
        .context("failed to parse Ethernet frame")?;
    match ethernet
        .ethertype()
        .ok_or_else(|| anyhow::anyhow!("missing ethertype in Ethernet frame"))?
    {
        EtherType::Ipv4 => {
            let ipv4 = Ipv4Packet::parse(&mut bv, ()).context("failed to parse IPv4 packet")?;
            if ipv4.proto() != IpProto::Udp {
                return Ok(FrameMetadata::Other);
            }
            Ok(FrameMetadata::Udp(fidl_fuchsia_net::IpAddress::Ipv4(
                fidl_fuchsia_net::Ipv4Address { addr: ipv4.dst_ip().ipv4_bytes() },
            )))
        }
        EtherType::Arp => {
            let arp = ArpPacket::<_, net_types::ethernet::Mac, net_types::ip::Ipv4Addr>::parse(
                &mut bv,
                (),
            )
            .context("failed to parse ARP packet")?;
            match arp.operation() {
                ArpOp::Request => Ok(FrameMetadata::NeighborSolicitation(
                    fidl_fuchsia_net::IpAddress::Ipv4(fidl_fuchsia_net::Ipv4Address {
                        addr: arp.target_protocol_address().ipv4_bytes(),
                    }),
                )),
                ArpOp::Response => Ok(FrameMetadata::Other),
                ArpOp::Other(other) => Err(anyhow::anyhow!("unrecognized ARP operation {}", other)),
            }
        }
        EtherType::Ipv6 => {
            let ipv6 = Ipv6Packet::parse(&mut bv, ()).context("failed to parse IPv6 packet")?;
            match ipv6.proto() {
                IpProto::Icmpv6 => {
                    // NB: filtering out packets with an unspecified source address will
                    // filter out DAD-related solicitations.
                    if !ipv6.src_ip().is_specified() {
                        return Ok(FrameMetadata::Other);
                    }
                    let parse_args = IcmpParseArgs::new(ipv6.src_ip(), ipv6.dst_ip());
                    match Icmpv6Packet::parse(&mut bv, parse_args)
                        .context("failed to parse ICMP packet")?
                    {
                        Icmpv6Packet::Ndp(NdpPacket::NeighborSolicitation(solicit)) => {
                            Ok(FrameMetadata::NeighborSolicitation(
                                fidl_fuchsia_net::IpAddress::Ipv6(fidl_fuchsia_net::Ipv6Address {
                                    addr: solicit.message().target_address().ipv6_bytes(),
                                }),
                            ))
                        }
                        _ => Ok(FrameMetadata::Other),
                    }
                }
                IpProto::Udp => Ok(FrameMetadata::Udp(fidl_fuchsia_net::IpAddress::Ipv6(
                    fidl_fuchsia_net::Ipv6Address { addr: ipv6.dst_ip().ipv6_bytes() },
                ))),
                _ => Ok(FrameMetadata::Other),
            }
        }
        EtherType::Other(other) => {
            Err(anyhow::anyhow!("unrecognized ethertype in Ethernet frame {}", other))
        }
    }
}

/// Creates a fake endpoint that extracts [`FrameMetadata`] from exchanged
/// frames in `network`.
fn create_metadata_stream<'a>(
    ep: &'a netemul::TestFakeEndpoint<'a>,
) -> impl futures::Stream<Item = Result<FrameMetadata>> + 'a {
    ep.frame_stream().map(|r| {
        let (data, dropped) = r.context("fake_ep FIDL error")?;
        if dropped != 0 {
            Err(anyhow::anyhow!("dropped {} frames on fake endpoint", dropped))
        } else {
            extract_frame_metadata(data)
        }
    })
}

// Helper function to retrieve the next item from a fake endpoint metadata
// stream.
async fn read_metadata_stream<T>(
    stream: &mut (impl futures::Stream<Item = Result<T>> + std::marker::Unpin),
) -> Result<T> {
    stream
        .try_next()
        .await
        .context("failed to read from fake endpoint")?
        .ok_or_else(|| anyhow::anyhow!("fake endpoint stream ended unexpectedly"))
}

#[fasync::run_singlethreaded(test)]
async fn neigh_clear_entries_errors() -> Result {
    let sandbox = TestSandbox::new().context("failed to create sandbox")?;
    let network = sandbox.create_network("net").await.context("failed to create network")?;

    let alice = create_environment(
        &sandbox,
        &network,
        "neigh_clear_entries_errors",
        "alice",
        fidl_fuchsia_net::Subnet { addr: ALICE_IP, prefix_len: SUBNET_PREFIX },
        ALICE_MAC,
    )
    .await
    .context("failed to setup alice environment")?;

    // Connect to the service and check some error cases.
    let controller = alice
        .env
        .connect_to_service::<fidl_fuchsia_net_neighbor::ControllerMarker>()
        .context("failed to connect to Controller")?;
    // Clearing neighbors on loopback interface is not supported.
    assert_eq!(
        controller.clear_entries(alice.loopback_id).await.context("clear_entries FIDL error")?,
        Err(fuchsia_zircon::Status::NOT_SUPPORTED.into_raw())
    );
    // Clearing neighbors on non-existing interface returns the proper error.
    assert_eq!(
        controller.clear_entries(alice.ep.id() + 100).await.context("clear_entries FIDL error")?,
        Err(fuchsia_zircon::Status::NOT_FOUND.into_raw())
    );
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn neigh_clear_entries() -> Result {
    let sandbox = TestSandbox::new().context("failed to create sandbox")?;
    let network = sandbox.create_network("net").await.context("failed to create network")?;

    // Attach a fake endpoint that will capture all the ARP and NDP neighbor
    // solicitations.
    let fake_ep = network.create_fake_endpoint().context("failed to create fake endpoint")?;
    let mut solicit_stream = create_metadata_stream(&fake_ep).try_filter_map(|m| {
        futures::future::ok(match m {
            FrameMetadata::NeighborSolicitation(ip) => Some(ip),
            FrameMetadata::Udp(_) | FrameMetadata::Other => None,
        })
    });

    let (alice, bob) = create_neighbor_environments(&sandbox, &network, "neigh_clear_entries")
        .await
        .context("failed to setup environments")?;

    let controller = alice
        .env
        .connect_to_service::<fidl_fuchsia_net_neighbor::ControllerMarker>()
        .context("failed to connect to Controller")?;

    let entries =
        list_existing_entries(&alice.env).await.context("failed to get existing entries")?;
    assert!(entries.is_empty(), "entries should be empty on startup, got: {:?}", entries);

    // Exchange some datagrams to add some entries to the list and check that we
    // observe the neighbor solicitations over the network.
    let () = exchange_dgrams(&alice, &bob)
        .await
        .context("failed to exchange datagrams before clearing cache")?;
    assert_eq!(
        read_metadata_stream(&mut solicit_stream)
            .await
            .context("failed to observe initial IPv4 solicitation")?,
        BOB_IP
    );
    assert_eq!(
        read_metadata_stream(&mut solicit_stream)
            .await
            .context("failed to observe initial IPv6 solicitation")?,
        bob.ipv6
    );

    let entries =
        list_existing_entries(&alice.env).await.context("failed to get existing entries")?;
    assert_eq!(entries.len(), 2, "should have two entries after exchange, got: {:?}", entries);

    // Clear entries and verify they go away.
    let () = controller
        .clear_entries(alice.ep.id())
        .await
        .context("clear_entries FIDL error")?
        .map_err(fuchsia_zircon::Status::from_raw)
        .context("clear_entries failed")?;
    let entries =
        list_existing_entries(&alice.env).await.context("failed to get existing entries")?;
    assert!(entries.is_empty(), "entries should have been emptied, got: {:?}", entries);

    // Exchange datagrams again and assert that new solicitation requests were
    // sent.
    let () = exchange_dgrams(&alice, &bob)
        .await
        .context("failed to exchange datagrams after clearing cache")?;
    assert_eq!(
        read_metadata_stream(&mut solicit_stream)
            .await
            .context("failed to observe new IPv4 solicitation")?,
        BOB_IP
    );
    assert_eq!(
        read_metadata_stream(&mut solicit_stream)
            .await
            .context("failed to observe new IPv6 solicitation")?,
        bob.ipv6
    );

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn neigh_add_remove_entry() -> Result {
    let sandbox = TestSandbox::new().context("failed to create sandbox")?;
    let network = sandbox.create_network("net").await.context("failed to create network")?;

    // Attach a fake endpoint that will observe neighbor solicitations and UDP
    // frames.
    let fake_ep = network.create_fake_endpoint().context("failed to create fake endpoint")?;
    let mut meta_stream = create_metadata_stream(&fake_ep).try_filter_map(|m| {
        futures::future::ok(match m {
            m @ FrameMetadata::NeighborSolicitation(_) | m @ FrameMetadata::Udp(_) => Some(m),
            FrameMetadata::Other => None,
        })
    });

    let (alice, bob) = create_neighbor_environments(&sandbox, &network, "neigh_add_remove_entry")
        .await
        .context("failed to setup environments")?;

    let controller = alice
        .env
        .connect_to_service::<fidl_fuchsia_net_neighbor::ControllerMarker>()
        .context("failed to connect to Controller")?;

    // Entries start out empty.
    let entries =
        list_existing_entries(&alice.env).await.context("failed to get existing entries")?;
    assert!(entries.is_empty(), "entries should be empty on startup, got: {:?}", entries);

    // Check error conditions.
    // Add and remove entry not supported on loopback.
    assert_eq!(
        controller
            .add_entry(alice.loopback_id, &mut BOB_IP.clone(), &mut BOB_MAC.clone())
            .await
            .context("add_entry FIDL error")?
            .map_err(fuchsia_zircon::Status::from_raw),
        Err(fuchsia_zircon::Status::NOT_SUPPORTED)
    );
    assert_eq!(
        controller
            .remove_entry(alice.loopback_id, &mut BOB_IP.clone())
            .await
            .context("add_entry FIDL error")?
            .map_err(fuchsia_zircon::Status::from_raw),
        Err(fuchsia_zircon::Status::NOT_SUPPORTED)
    );
    // Add entry and remove entry return not found on non-existing interface.
    assert_eq!(
        controller
            .add_entry(alice.ep.id() + 100, &mut BOB_IP.clone(), &mut BOB_MAC.clone())
            .await
            .context("add_entry FIDL error")?
            .map_err(fuchsia_zircon::Status::from_raw),
        Err(fuchsia_zircon::Status::NOT_FOUND)
    );
    assert_eq!(
        controller
            .remove_entry(alice.ep.id() + 100, &mut BOB_IP.clone())
            .await
            .context("add_entry FIDL error")?
            .map_err(fuchsia_zircon::Status::from_raw),
        Err(fuchsia_zircon::Status::NOT_FOUND)
    );
    // Remove entry returns not found for non-existing entry.
    assert_eq!(
        controller
            .remove_entry(alice.ep.id(), &mut BOB_IP.clone())
            .await
            .context("add_entry FIDL error")?
            .map_err(fuchsia_zircon::Status::from_raw),
        Err(fuchsia_zircon::Status::NOT_FOUND)
    );

    // Add static entries and verify that they're listable.
    let () = controller
        .add_entry(alice.ep.id(), &mut BOB_IP.clone(), &mut BOB_MAC.clone())
        .await
        .context("add_entry FIDL error")?
        .map_err(fuchsia_zircon::Status::from_raw)
        .context("add_entry failed")?;
    let () = controller
        .add_entry(alice.ep.id(), &mut bob.ipv6.clone(), &mut BOB_MAC.clone())
        .await
        .context("add_entry FIDL error")?
        .map_err(fuchsia_zircon::Status::from_raw)
        .context("add_entry failed")?;

    let mut entries =
        list_existing_entries(&alice.env).await.context("failed to get existing entries")?;
    let () = assert_static_entry(
        entries.remove(&(alice.ep.id(), BOB_IP)).expect("static IPv4 entry missing"),
        alice.ep.id(),
        BOB_IP,
        BOB_MAC,
    );
    let () = assert_static_entry(
        entries.remove(&(alice.ep.id(), bob.ipv6)).expect("static IPv6 entry missing"),
        alice.ep.id(),
        bob.ipv6,
        BOB_MAC,
    );
    assert!(entries.is_empty(), "unexpected neighbors remaining in list: {:?}", entries);

    let () = exchange_dgrams(&alice, &bob).await?;
    assert_eq!(read_metadata_stream(&mut meta_stream).await?, FrameMetadata::Udp(BOB_IP));
    assert_eq!(read_metadata_stream(&mut meta_stream).await?, FrameMetadata::Udp(bob.ipv6));

    // Remove both entries and check that the list is empty afterwards.
    let () = controller
        .remove_entry(alice.ep.id(), &mut BOB_IP.clone())
        .await
        .context("remove_entry FIDL error")?
        .map_err(fuchsia_zircon::Status::from_raw)
        .context("remove_entry failed")?;
    let () = controller
        .remove_entry(alice.ep.id(), &mut bob.ipv6.clone())
        .await
        .context("remove_entry FIDL error")?
        .map_err(fuchsia_zircon::Status::from_raw)
        .context("remove_entry failed")?;

    let entries =
        list_existing_entries(&alice.env).await.context("failed to get existing entries")?;
    assert!(entries.is_empty(), "entries should've been emptied, got: {:?}", entries);

    // Exchange datagrams again and assert that new solicitation requests were
    // sent (ignoring any UDP metadata this time).
    let mut meta_stream = meta_stream.try_filter(|meta| {
        futures::future::ready(match meta {
            FrameMetadata::Udp(_) | FrameMetadata::Other => false,
            FrameMetadata::NeighborSolicitation(_) => true,
        })
    });

    let () = exchange_dgrams(&alice, &bob).await.context("failed to exchange datagrams")?;
    assert_eq!(
        read_metadata_stream(&mut meta_stream)
            .await
            .context("failed to observe IPv4 solicitation")?,
        FrameMetadata::NeighborSolicitation(BOB_IP)
    );
    assert_eq!(
        read_metadata_stream(&mut meta_stream)
            .await
            .context("failed to observe IPv6 solicitation")?,
        FrameMetadata::NeighborSolicitation(bob.ipv6)
    );

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn neigh_unreachability_config_errors() -> Result {
    let sandbox = TestSandbox::new().context("failed to create sandbox")?;
    let network = sandbox.create_network("net").await.context("failed to create network")?;

    let alice = create_environment(
        &sandbox,
        &network,
        "neigh_update_unreachability_config_errors",
        "alice",
        fidl_fuchsia_net::Subnet { addr: ALICE_IP, prefix_len: SUBNET_PREFIX },
        ALICE_MAC,
    )
    .await
    .context("failed to setup environment")?;

    let view = alice
        .env
        .connect_to_service::<fidl_fuchsia_net_neighbor::ViewMarker>()
        .context("failed to connect to View")?;
    assert_eq!(
        view.get_unreachability_config(alice.ep.id() + 100)
            .await
            .context("get_unreachability_config FIDL error")?
            .map_err(fuchsia_zircon::Status::from_raw),
        Err(fuchsia_zircon::Status::NOT_FOUND)
    );

    let controller = alice
        .env
        .connect_to_service::<fidl_fuchsia_net_neighbor::ControllerMarker>()
        .context("failed to connect to Controller")?;
    assert_eq!(
        controller
            .update_unreachability_config(
                alice.ep.id() + 100,
                fidl_fuchsia_net_neighbor::UnreachabilityConfig::empty(),
            )
            .await
            .context("update_unreachability_config FIDL error")?
            .map_err(fuchsia_zircon::Status::from_raw),
        Err(fuchsia_zircon::Status::NOT_FOUND)
    );
    assert_eq!(
        controller
            .update_unreachability_config(
                alice.loopback_id,
                fidl_fuchsia_net_neighbor::UnreachabilityConfig::empty(),
            )
            .await
            .context("update_unreachability_config FIDL error")?
            .map_err(fuchsia_zircon::Status::from_raw),
        Err(fuchsia_zircon::Status::NOT_SUPPORTED)
    );
    let mut invalid_config = fidl_fuchsia_net_neighbor::UnreachabilityConfig::empty();
    invalid_config.base_reachable_time = Some(-1);
    assert_eq!(
        controller
            .update_unreachability_config(alice.ep.id(), invalid_config)
            .await
            .context("update_unreachability_config FIDL error")?
            .map_err(fuchsia_zircon::Status::from_raw),
        Err(fuchsia_zircon::Status::INVALID_ARGS)
    );
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn neigh_unreachability_config() -> Result {
    let sandbox = TestSandbox::new().context("failed to create sandbox")?;
    let network = sandbox.create_network("net").await.context("failed to create network")?;

    let alice = create_environment(
        &sandbox,
        &network,
        "neigh_update_unreachability_config",
        "alice",
        fidl_fuchsia_net::Subnet { addr: ALICE_IP, prefix_len: SUBNET_PREFIX },
        ALICE_MAC,
    )
    .await
    .context("failed to setup environment")?;

    let view = alice
        .env
        .connect_to_service::<fidl_fuchsia_net_neighbor::ViewMarker>()
        .context("failed to connect to View")?;
    let controller = alice
        .env
        .connect_to_service::<fidl_fuchsia_net_neighbor::ControllerMarker>()
        .context("failed to connect to Controller")?;

    {
        // Get the current UnreachabilityConfig for comparison
        let original_config = view
            .get_unreachability_config(alice.ep.id())
            .await
            .context("get_unreachability_config FIDL error")?
            .map_err(fuchsia_zircon::Status::from_raw)
            .context("get_unreachability_config failed")?;
        let expected = fidl_fuchsia_net_neighbor::UnreachabilityConfig { ..original_config };

        // Verify that updating with the current config doesn't change anything
        let () = controller
            .update_unreachability_config(alice.ep.id(), original_config)
            .await
            .context("update_unreachability_config FIDL error")?
            .map_err(fuchsia_zircon::Status::from_raw)
            .context("update_unreachability_config failed")?;
        assert_eq!(
            view.get_unreachability_config(alice.ep.id())
                .await
                .context("get_unreachability_config FIDL error")?
                .map_err(fuchsia_zircon::Status::from_raw),
            Ok(expected),
        );
    }

    {
        let original_config = view
            .get_unreachability_config(alice.ep.id())
            .await
            .context("get_unreachability_config FIDL error")?
            .map_err(fuchsia_zircon::Status::from_raw)
            .context("get_unreachability_config failed")?;

        // Update config with some non-defaults
        let mut updates = fidl_fuchsia_net_neighbor::UnreachabilityConfig::empty();
        let updated_base_reachable_time =
            Some(fidl_fuchsia_net_neighbor::DEFAULT_BASE_REACHABLE_TIME * 2);
        let updated_retransmit_timer =
            Some(fidl_fuchsia_net_neighbor::DEFAULT_RETRANSMIT_TIMER / 2);
        updates.base_reachable_time = updated_base_reachable_time;
        updates.retransmit_timer = updated_retransmit_timer;
        let () = controller
            .update_unreachability_config(alice.ep.id(), updates)
            .await
            .context("update_unreachability_config FIDL error")?
            .map_err(fuchsia_zircon::Status::from_raw)
            .context("update_unreachability_config failed")?;

        // Verify that set fields are changed and unset fields remain the same
        let updated_config = view
            .get_unreachability_config(alice.ep.id())
            .await
            .context("get_unreachability_config FIDL error")?
            .map_err(fuchsia_zircon::Status::from_raw);
        assert_eq!(
            updated_config,
            Ok(fidl_fuchsia_net_neighbor::UnreachabilityConfig {
                base_reachable_time: updated_base_reachable_time,
                retransmit_timer: updated_retransmit_timer,
                ..original_config
            })
        );
    }

    Ok(())
}
