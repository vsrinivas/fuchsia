// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use fidl_fuchsia_net as net;
use fuchsia_async::{DurationExt as _, TimeoutExt as _};

use futures::StreamExt as _;
use net_declare::std_ip_v4;
use net_types::{
    ip::{self as net_types_ip},
    MulticastAddress as _,
};
use netemul::RealmUdpSocket as _;
use netstack_testing_common::{interfaces, setup_network, ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT};
use netstack_testing_macros::variants_test;
use packet::ParsablePacket as _;
use packet_formats::{
    ethernet::{EtherType, EthernetFrame, EthernetFrameLengthCheck},
    igmp::{messages::IgmpMembershipReportV2, IgmpMessage},
    ip::Ipv4Proto,
    testutil::parse_ip_packet,
};

#[variants_test]
async fn sends_igmp_reports<E: netemul::Endpoint>(name: &str) {
    const INTERFACE_ADDR: std::net::Ipv4Addr = std_ip_v4!("192.168.0.1");
    const MULTICAST_ADDR: std::net::Ipv4Addr = std_ip_v4!("224.1.2.3");

    let sandbox = netemul::TestSandbox::new().expect("error creating sandbox");
    let (_network, realm, _netstack, iface, fake_ep) =
        setup_network::<E>(&sandbox, name).await.expect("error setting up network");

    let addr = net::Ipv4Address { addr: INTERFACE_ADDR.octets() };
    let _address_state_provider = interfaces::add_subnet_address_and_route_wait_assigned(
        &iface,
        net::Subnet { addr: net::IpAddress::Ipv4(addr), prefix_len: 24 },
        fidl_fuchsia_net_interfaces_admin::AddressParameters::EMPTY,
    )
    .await
    .expect("add subnet address and route");

    let sock = fuchsia_async::net::UdpSocket::bind_in_realm(
        &realm,
        std::net::SocketAddrV4::new(std::net::Ipv4Addr::UNSPECIFIED, 0).into(),
    )
    .await
    .expect("error creating socket");

    let () = sock
        .as_ref()
        .join_multicast_v4(&MULTICAST_ADDR, &INTERFACE_ADDR)
        .expect("error joining multicast group");

    let net_types_ip_multicast_addr = net_types_ip::Ipv4Addr::new(MULTICAST_ADDR.octets());

    let stream = fake_ep
        .frame_stream()
        .map(|r| r.expect("error getting OnData event"))
        .filter_map(|(data, dropped)| {
            async move {
                assert_eq!(dropped, 0);
                let mut data = &data[..];

                // Do not check the frame length as the size of IGMP reports may be less
                // than the minimum ethernet frame length and our virtual (netemul) interface
                // does not pad runt ethernet frames before transmission.
                let eth = EthernetFrame::parse(&mut data, EthernetFrameLengthCheck::NoCheck)
                    .expect("error parsing ethernet frame");

                if eth.ethertype() != Some(EtherType::Ipv4) {
                    // Ignore non-IPv4 packets.
                    return None;
                }

                let (mut payload, src_ip, dst_ip, proto, ttl) =
                    parse_ip_packet::<net_types_ip::Ipv4>(&data)
                        .expect("error parsing IPv4 packet");

                if proto != Ipv4Proto::Igmp {
                    // Ignore non-IGMP packets.
                    return None;
                }

                assert_eq!(src_ip, net_types_ip::Ipv4Addr::new(INTERFACE_ADDR.octets()), "IGMP messages must be sent from an address assigned to the NIC");

                assert!(dst_ip.is_multicast(), "all IGMP messages must be sent to a multicast address; dst_ip = {}", dst_ip);

                // As per RFC 2236 section 2,
                //
                //   All IGMP messages described in this document are sent with
                //   IP TTL 1, ...
                assert_eq!(ttl, 1, "IGMP messages must have a TTL of 1");

                let igmp = IgmpMessage::<_, IgmpMembershipReportV2>::parse(&mut payload, ())
                    .expect("error parsing IGMP message");

                let group_addr = igmp.group_addr();
                assert!(group_addr.is_multicast(), "IGMP reports must only be sent for multicast addresses; group_addr = {}", group_addr);

                if group_addr != net_types_ip_multicast_addr {
                    // We are only interested in the report for the multicast group we
                    // joined.
                    return None;
                }

                assert_eq!(dst_ip, group_addr, "the destination of an IGMP report should be the multicast group the report is for");

                Some(())
            }
        });
    futures::pin_mut!(stream);
    let () = stream
        .next()
        .on_timeout(ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT.after_now(), || {
            panic!("timed out waiting for the IGMP report");
        })
        .await
        .expect("error getting our expected IGMP report");
}
