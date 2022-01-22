// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use fidl_fuchsia_net as fnet;
use futures::{FutureExt as _, SinkExt as _, StreamExt as _, TryStreamExt as _};
use net_declare::{fidl_mac, fidl_subnet, std_socket_addr_v4};
use netstack_testing_common::realms::{Netstack2, TestSandboxExt as _};
use netstack_testing_macros::variants_test;
use packet::ParsablePacket as _;
use packet_formats::icmp::MessageBody as _;
use packet_formats::ipv4::Ipv4Header as _;
use static_assertions::const_assert_eq;
use std::convert::TryFrom as _;
use test_case::test_case;

const SOURCE_SUBNET: fnet::Subnet = fidl_subnet!("192.168.255.1/16");
const TARGET_SUBNET: fnet::Subnet = fidl_subnet!("192.168.254.1/16");
const SOURCE_MAC_ADDRESS: fnet::MacAddress = fidl_mac!("02:00:00:00:00:01");
const TARGET_MAC_ADDRESS: fnet::MacAddress = fidl_mac!("02:00:00:00:00:02");

/// An MTU that will result in an ICMP packet that is entirely used. Since the
/// payload length is rounded down to the nearest 8 bytes, this value minus the
/// IP header and the ethernet header must be divisible by 8.
const FULLY_USABLE_MTU: usize = 1490;

// Verifies that `FULLY_USABLE_MTU` corresponds to an MTU value that can have
// all bytes leveraged when sending a ping.
const_assert_eq!(
    max_icmp_payload_length(FULLY_USABLE_MTU),
    possible_icmp_payload_length(FULLY_USABLE_MTU)
);

/// Sends a single ICMP echo request from `source_realm` to `addr`, and waits
/// for the echo reply.
///
/// The body of the ICMP packet will be filled with `payload_length` 0 bytes.
/// Panics if the ping fails.
async fn expect_successful_ping<Ip: ping::IpExt>(
    source_realm: &netemul::TestRealm<'_>,
    addr: Ip::Addr,
    payload_length: usize,
) {
    let icmp_sock = source_realm.icmp_socket::<Ip>().await.expect("failed to create ICMP socket");

    let payload: Vec<u8> = vec![0; payload_length];
    let (mut sink, mut stream) = ping::new_unicast_sink_and_stream::<Ip, _, { u16::MAX as usize }>(
        &icmp_sock, &addr, &payload,
    );

    const SEQ: u16 = 1;
    sink.send(SEQ).await.expect("failed to send ping");
    let got = stream
        .try_next()
        .await
        .expect("echo reply failure")
        .expect("echo stream ended unexpectedly");
    assert_eq!(got, SEQ);
}

#[derive(Debug, PartialEq)]
struct IcmpPacketMetadata {
    source_address: fnet::IpAddress,
    target_address: fnet::IpAddress,
    payload_length: usize,
}

#[derive(Debug, PartialEq)]
enum IcmpEvent {
    EchoRequest(IcmpPacketMetadata),
    EchoReply(IcmpPacketMetadata),
}

// TODO(https://fxbug.dev/91971): Replace this with a shared solution.
fn to_fidl_address(addr: net_types::ip::Ipv4Addr) -> fnet::IpAddress {
    fnet::IpAddress::Ipv4(fnet::Ipv4Address { addr: addr.ipv4_bytes() })
}

/// Extracts an Ipv4 `IcmpEvent` from the provided `data`.
fn extract_icmp_event(ipv4_packet: &packet_formats::ipv4::Ipv4Packet<&[u8]>) -> Option<IcmpEvent> {
    let fidl_src_ip = to_fidl_address(ipv4_packet.src_ip());
    let fidl_dst_ip = to_fidl_address(ipv4_packet.dst_ip());

    if ipv4_packet.proto() != packet_formats::ip::Ipv4Proto::Icmp {
        // Ignore non-ICMP packets.
        return None;
    }

    let mut payload = ipv4_packet.body();
    let icmp_packet = packet_formats::icmp::Icmpv4Packet::parse(
        &mut payload,
        packet_formats::icmp::IcmpParseArgs::new(ipv4_packet.src_ip(), ipv4_packet.dst_ip()),
    )
    .expect("failed to parse ICMP packet");

    match icmp_packet {
        packet_formats::icmp::Icmpv4Packet::EchoRequest(echo) => {
            Some(IcmpEvent::EchoRequest(IcmpPacketMetadata {
                source_address: fidl_src_ip,
                target_address: fidl_dst_ip,
                payload_length: echo.body().len(),
            }))
        }
        packet_formats::icmp::Icmpv4Packet::EchoReply(echo) => {
            Some(IcmpEvent::EchoReply(IcmpPacketMetadata {
                source_address: fidl_src_ip,
                target_address: fidl_dst_ip,
                payload_length: echo.body().len(),
            }))
        }
        packet_formats::icmp::Icmpv4Packet::DestUnreachable(_)
        | packet_formats::icmp::Icmpv4Packet::ParameterProblem(_)
        | packet_formats::icmp::Icmpv4Packet::Redirect(_)
        | packet_formats::icmp::Icmpv4Packet::TimeExceeded(_)
        | packet_formats::icmp::Icmpv4Packet::TimestampReply(_)
        | packet_formats::icmp::Icmpv4Packet::TimestampRequest(_) => None,
    }
}

/// Returns the maximum payload length of a packet given the `mtu`.
const fn max_icmp_payload_length(mtu: usize) -> usize {
    // Based on the following logic:
    // https://osscs.corp.google.com/fuchsia/fuchsia/+/main:third_party/golibs/vendor/gvisor.dev/gvisor/pkg/tcpip/network/ipv4/ipv4.go;l=402;drc=42abed01bbe5e1fb34f17f64f63f6e7ba27a74c7
    // The max payload length is rounded down to the nearest number that is
    // divisible by 8 bytes.
    ((mtu
        - packet_formats::ethernet::testutil::ETHERNET_HDR_LEN_NO_TAG
        - packet_formats::ipv4::testutil::IPV4_MIN_HDR_LEN)
        & !7)
        - ping::ICMP_HEADER_LEN
}

/// Returns the maximum number of bytes that an ICMP packet body could
/// potentially fill given an `mtu`.
///
/// The returned value excludes the relevant headers.
const fn possible_icmp_payload_length(mtu: usize) -> usize {
    mtu - packet_formats::ipv4::testutil::IPV4_MIN_HDR_LEN
        - packet_formats::ethernet::testutil::ETHERNET_HDR_LEN_NO_TAG
        - ping::ICMP_HEADER_LEN
}

/// Returns a stream of `IcmpEvent`s from the provided `fake_endpoint`.
fn icmp_event_stream<'a>(
    fake_endpoint: &'a netemul::TestFakeEndpoint<'_>,
) -> impl futures::Stream<Item = IcmpEvent> + 'a {
    fake_endpoint.frame_stream().map(|r| r.expect("error getting OnData event")).filter_map(
        |(data, _dropped)| async move {
            let mut data = &data[..];

            let eth = packet_formats::ethernet::EthernetFrame::parse(
                &mut data,
                packet_formats::ethernet::EthernetFrameLengthCheck::NoCheck,
            )
            .expect("error parsing ethernet frame");

            if eth.ethertype() != Some(packet_formats::ethernet::EtherType::Ipv4) {
                // Ignore non-IPv4 packets.
                return None;
            }

            let mut frame_body = eth.body();
            let ipv4_packet = packet_formats::ipv4::Ipv4Packet::parse(&mut frame_body, ())
                .expect("failed to parse IPv4 packet");

            match ipv4_packet.fragment_type() {
                packet_formats::ipv4::Ipv4FragmentType::InitialFragment => {
                    extract_icmp_event(&ipv4_packet)
                }
                // Only consider initial fragments as we are simply verifying that the
                // payload length is of an expected size for a given MTU. Note that
                // non-initial fragments do not contain an ICMP header and therefore
                // would not parse as an ICMP packet
                packet_formats::ipv4::Ipv4FragmentType::NonInitialFragment => None,
            }
        },
    )
}

#[variants_test]
#[test_case(
    "fragmented",
    netemul::DEFAULT_MTU.into(),
    // Set the `payload_length` to a value larger than the `mtu`. This will
    // force the ICMP packets to be fragmented.
    (netemul::DEFAULT_MTU + 100).into(),
    max_icmp_payload_length(netemul::DEFAULT_MTU.into());
    "fragmented")]
#[test_case(
    "fully_used_mtu",
    FULLY_USABLE_MTU,
    possible_icmp_payload_length(FULLY_USABLE_MTU),
    possible_icmp_payload_length(FULLY_USABLE_MTU);
    "fully used mtu"
)]
async fn ping_succeeds_with_expected_payload<E: netemul::Endpoint>(
    name: &str,
    sub_name: &str,
    mtu: usize,
    payload_length: usize,
    // Corresponds to the ICMP packet body length. This length excludes the
    // relevant headers (e.g. ICMP, ethernet, and IP). When fragmented, only the
    // first packet is considered.
    expected_payload_length: usize,
) {
    let sandbox = netemul::TestSandbox::new().expect("failed to create sandbox");
    let network = sandbox
        .create_network(format!("network_{}_{}", name, sub_name))
        .await
        .expect("failed to create network");
    let source_realm = sandbox
        .create_netstack_realm::<Netstack2, _>(format!("source_{}_{}", name, sub_name))
        .expect("failed to create source realm");
    let target_realm = sandbox
        .create_netstack_realm::<Netstack2, _>(format!("target_{}_{}", name, sub_name))
        .expect("failed to reate target realm");
    let fake_ep = network.create_fake_endpoint().expect("failed to create fake endpoint");

    let mtu = u16::try_from(mtu).expect("failed to convert mtu to u16");
    let source_if = source_realm
        .join_network_with(
            &network,
            "source_ep",
            E::make_config(mtu, Some(SOURCE_MAC_ADDRESS)),
            &netemul::InterfaceConfig::StaticIp(SOURCE_SUBNET),
        )
        .await
        .expect("failed to join network with source");
    let target_if = target_realm
        .join_network_with(
            &network,
            "target_ep",
            E::make_config(mtu, Some(TARGET_MAC_ADDRESS)),
            &netemul::InterfaceConfig::StaticIp(TARGET_SUBNET),
        )
        .await
        .expect("failed to join network with target");

    // Add static ARP entries as we've observed flakes in CQ due to ARP timeouts
    // and ARP resolution is immaterial to this test.
    futures::stream::iter([
        (&source_realm, &source_if, TARGET_SUBNET.addr, TARGET_MAC_ADDRESS),
        (&target_realm, &target_if, SOURCE_SUBNET.addr, SOURCE_MAC_ADDRESS),
    ])
    .for_each_concurrent(None, |(realm, ep, addr, mac)| {
        realm.add_neighbor_entry(ep.id(), addr, mac).map(|r| r.expect("add_neighbor_entry failed"))
    })
    .await;

    expect_successful_ping::<ping::Ipv4>(
        &source_realm,
        // TODO(https://github.com/rust-lang/rust/issues/67390): Make the
        // address const.
        std_socket_addr_v4!("192.168.254.1:0"),
        payload_length,
    )
    .await;

    let icmp_event_stream = icmp_event_stream(&fake_ep);
    futures::pin_mut!(icmp_event_stream);

    assert_eq!(
        icmp_event_stream.next().await,
        Some(IcmpEvent::EchoRequest(IcmpPacketMetadata {
            source_address: SOURCE_SUBNET.addr,
            target_address: TARGET_SUBNET.addr,
            payload_length: expected_payload_length,
        })),
    );

    assert_eq!(
        icmp_event_stream.next().await,
        Some(IcmpEvent::EchoReply(IcmpPacketMetadata {
            source_address: TARGET_SUBNET.addr,
            target_address: SOURCE_SUBNET.addr,
            payload_length: expected_payload_length,
        })),
    );
}
