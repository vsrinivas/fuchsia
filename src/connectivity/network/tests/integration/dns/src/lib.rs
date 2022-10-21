// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use std::{
    convert::{TryFrom as _, TryInto as _},
    num::NonZeroU16,
    str::FromStr as _,
};

use fidl_fuchsia_net as fnet;
use fidl_fuchsia_net_dhcp as net_dhcp;
use fidl_fuchsia_net_dhcpv6 as net_dhcpv6;
use fidl_fuchsia_net_ext as fnet_ext;
use fidl_fuchsia_net_interfaces as net_interfaces;
use fidl_fuchsia_net_name as net_name;
use fidl_fuchsia_testing as ftesting;
use fuchsia_async::{DurationExt as _, TimeoutExt as _};
use fuchsia_zircon as zx;

use futures::{
    future::{self, FusedFuture, Future, FutureExt as _},
    stream::{self, StreamExt as _},
    AsyncReadExt as _, AsyncWriteExt as _,
};
use itertools::Itertools as _;
use net_declare::{fidl_ip, fidl_ip_v4, fidl_ip_v6, fidl_subnet, std_ip_v6, std_socket_addr};
use net_types::{ethernet::Mac, ip as net_types_ip, Witness as _};
use netemul::{RealmTcpListener as _, RealmUdpSocket as _};
use netstack_testing_common::{
    constants::{eth as eth_consts, ipv6 as ipv6_consts},
    interfaces, pause_fake_clock,
    realms::{KnownServiceProvider, Manager, ManagerConfig, Netstack2, TestSandboxExt as _},
    wait_for_component_stopped, write_ndp_message, Result, ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT,
};
use netstack_testing_macros::variants_test;
use packet::{
    serialize::{InnerPacketBuilder as _, Serializer as _},
    ParsablePacket as _,
};
use packet_formats::{
    ethernet::{
        EtherType, EthernetFrame, EthernetFrameBuilder, EthernetFrameLengthCheck,
        EthernetIpExt as _,
    },
    icmp::ndp::{
        options::{NdpOptionBuilder, RecursiveDnsServer},
        RouterAdvertisement,
    },
    ip::{IpPacket as _, IpProto, Ipv6Proto},
    ipv6::{Ipv6Packet, Ipv6PacketBuilder},
    udp::{UdpPacket, UdpPacketBuilder, UdpParseArgs},
};
use packet_formats_dhcp::v6;

#[fuchsia_async::run_singlethreaded(test)]
async fn no_ip_literal() {
    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let realm = sandbox
        .create_netstack_realm_with::<Netstack2, _, _>(
            "realm",
            &[KnownServiceProvider::DnsResolver, KnownServiceProvider::FakeClock],
        )
        .expect("create realm");

    let name_lookup =
        realm.connect_to_protocol::<net_name::LookupMarker>().expect("connect to protocol");

    let range = || [true, false].into_iter();

    let name_lookup = &name_lookup;
    futures::stream::iter(range().cartesian_product(range()))
        .for_each_concurrent(None, move |(ipv4_lookup, ipv6_lookup)| async move {
            assert_eq!(
                name_lookup
                    .lookup_ip(
                        "240.0.0.2",
                        net_name::LookupIpOptions {
                            ipv4_lookup: Some(ipv4_lookup),
                            ipv6_lookup: Some(ipv6_lookup),
                            ..net_name::LookupIpOptions::EMPTY
                        },
                    )
                    .await
                    .expect("call lookup IP"),
                Err(net_name::LookupError::InvalidArgs),
                "ipv4_lookup={},ipv6_lookup={}",
                ipv4_lookup,
                ipv6_lookup,
            );

            assert_eq!(
                name_lookup
                    .lookup_ip(
                        "abcd::2",
                        net_name::LookupIpOptions {
                            ipv4_lookup: Some(ipv4_lookup),
                            ipv6_lookup: Some(ipv6_lookup),
                            ..net_name::LookupIpOptions::EMPTY
                        },
                    )
                    .await
                    .expect("call lookup IP"),
                Err(net_name::LookupError::InvalidArgs),
                "ipv4_lookup={},ipv6_lookup={}",
                ipv4_lookup,
                ipv6_lookup,
            );
        })
        .await
}

/// Keep polling the lookup admin's DNS servers until it returns `expect`.
async fn poll_lookup_admin<
    F: Unpin + FusedFuture + Future<Output = Result<component_events::events::Stopped>>,
>(
    lookup_admin: &net_name::LookupAdminProxy,
    expect: &[fnet::SocketAddress],
    mut wait_for_netmgr_fut: &mut F,
    poll_wait: zx::Duration,
    retry_count: u64,
) {
    for i in 0..retry_count {
        let () = futures::select! {
            () = fuchsia_async::Timer::new(poll_wait.after_now()).fuse() => (),
            stopped_event = wait_for_netmgr_fut => {
                panic!(
                    "the network manager unexpectedly exited with event: {:?}",
                    stopped_event,
                )
            }
        };

        let servers = lookup_admin.get_dns_servers().await.expect("failed to get DNS servers");
        println!("attempt {} got DNS servers {:?}", i, servers);

        if servers == expect {
            return;
        }
    }

    // Too many retries.
    panic!(
        "timed out waiting for DNS server configurations; retry_count={}, poll_wait={:?}",
        retry_count, poll_wait,
    )
}

/// Tests that Netstack exposes DNS servers discovered dynamically and NetworkManager
/// configures the Lookup service.
#[variants_test]
async fn discovered_dns<E: netemul::Endpoint, M: Manager>(name: &str) {
    const SERVER_ADDR: fnet::Subnet = fidl_subnet!("192.168.0.1/24");
    /// DNS server served by DHCP.
    const DHCP_DNS_SERVER: fnet::Ipv4Address = fidl_ip_v4!("123.12.34.56");
    /// DNS server served by NDP.
    const NDP_DNS_SERVER: fnet::Ipv6Address = fidl_ip_v6!("20a::1234:5678");

    /// Maximum number of times we'll poll `LookupAdmin` to check DNS configuration
    /// succeeded.
    const RETRY_COUNT: u64 = 60;
    /// Duration to sleep between polls.
    const POLL_WAIT: zx::Duration = zx::Duration::from_seconds(1);

    const DEFAULT_DNS_PORT: u16 = 53;

    let sandbox = netemul::TestSandbox::new().expect("failed to create sandbox");

    let network = sandbox.create_network("net").await.expect("failed to create network");
    let server_realm = sandbox
        .create_netstack_realm_with::<Netstack2, _, _>(
            format!("{}_server", name),
            &[
                KnownServiceProvider::DnsResolver,
                KnownServiceProvider::DhcpServer { persistent: false },
                KnownServiceProvider::FakeClock,
                KnownServiceProvider::SecureStash,
            ],
        )
        .expect("failed to create server realm");

    let client_realm = sandbox
        .create_netstack_realm_with::<Netstack2, _, _>(
            format!("{}_client", name),
            &[
                // Start the network manager on the client.
                //
                // The network manager should listen for DNS server events from the netstack and
                // configure the DNS resolver accordingly.
                KnownServiceProvider::Manager {
                    agent: M::MANAGEMENT_AGENT,
                    use_dhcp_server: false,
                    config: ManagerConfig::Empty,
                },
                KnownServiceProvider::DnsResolver,
                KnownServiceProvider::FakeClock,
            ],
        )
        .expect("failed to create client realm");

    let server_iface = server_realm
        .join_network::<E, _>(&network, "server-ep")
        .await
        .expect("failed to configure server networking");
    server_iface.add_address_and_subnet_route(SERVER_ADDR).await.expect("configure address");

    let dhcp_server = server_realm
        .connect_to_protocol::<net_dhcp::Server_Marker>()
        .expect("failed to connect to DHCP server");

    let dhcp_server_ref = &dhcp_server;

    let server_addr_v4 =
        assert_matches::assert_matches!(SERVER_ADDR.addr, fnet::IpAddress::Ipv4(v4) => v4);
    let (range_start, range_stop) = {
        let [a, b, c, d] = server_addr_v4.addr;
        // A small pool of addresses derived from the server's.
        (fnet::Ipv4Address { addr: [a, b, c, d + 1] }, fnet::Ipv4Address { addr: [a, b, c, d + 4] })
    };
    let () = stream::iter(
        [
            fidl_fuchsia_net_dhcp::Parameter::IpAddrs(vec![server_addr_v4]),
            fidl_fuchsia_net_dhcp::Parameter::AddressPool(fidl_fuchsia_net_dhcp::AddressPool {
                prefix_length: Some(25),
                range_start: Some(range_start),
                range_stop: Some(range_stop),
                ..fidl_fuchsia_net_dhcp::AddressPool::EMPTY
            }),
            fidl_fuchsia_net_dhcp::Parameter::BoundDeviceNames(vec!["eth2".to_string()]),
        ]
        .iter_mut(),
    )
    .for_each_concurrent(None, |parameter| async move {
        dhcp_server_ref
            .set_parameter(parameter)
            .await
            .expect("failed to call dhcp/Server.SetParameter")
            .map_err(zx::Status::from_raw)
            .unwrap_or_else(|e| {
                panic!("dhcp/Server.SetParameter({:?}) returned error: {:?}", parameter, e)
            })
    })
    .await;

    let () = dhcp_server
        .set_option(&mut net_dhcp::Option_::DomainNameServer(vec![DHCP_DNS_SERVER]))
        .await
        .expect("Failed to set DNS option")
        .map_err(zx::Status::from_raw)
        .expect("dhcp/Server.SetOption returned error");

    let () = dhcp_server
        .start_serving()
        .await
        .expect("failed to call dhcp/Server.StartServing")
        .map_err(zx::Status::from_raw)
        .expect("dhcp/Server.StartServing returned error");

    // Start networking on client realm.
    let client_iface = client_realm
        .join_network::<E, _>(&network, "client-ep")
        .await
        .expect("failed to configure client networking");
    client_iface.start_dhcp().await.expect("failed to start DHCP");

    // Send a Router Advertisement with DNS server configurations.
    let fake_ep = network.create_fake_endpoint().expect("failed to create fake endpoint");
    let ra = RouterAdvertisement::new(
        0,     /* current_hop_limit */
        false, /* managed_flag */
        false, /* other_config_flag */
        0,     /* router_lifetime */
        0,     /* reachable_time */
        0,     /* retransmit_timer */
    );
    let addresses = [NDP_DNS_SERVER.addr.into()];
    let rdnss = RecursiveDnsServer::new(9999, &addresses);
    let options = [NdpOptionBuilder::RecursiveDnsServer(rdnss)];
    let () = write_ndp_message::<&[u8], _>(
        eth_consts::MAC_ADDR,
        Mac::from(&net_types_ip::Ipv6::ALL_NODES_LINK_LOCAL_MULTICAST_ADDRESS),
        ipv6_consts::LINK_LOCAL_ADDR,
        net_types_ip::Ipv6::ALL_NODES_LINK_LOCAL_MULTICAST_ADDRESS.get(),
        ra,
        &options,
        &fake_ep,
    )
    .await
    .expect("failed to write NDP message");

    // The list of servers we expect to retrieve from `fuchsia.net.name/LookupAdmin`.
    let expect = [
        fnet::SocketAddress::Ipv4(fnet::Ipv4SocketAddress {
            address: DHCP_DNS_SERVER,
            port: DEFAULT_DNS_PORT,
        }),
        fnet::SocketAddress::Ipv6(fnet::Ipv6SocketAddress {
            address: NDP_DNS_SERVER,
            port: DEFAULT_DNS_PORT,
            zone_index: 0,
        }),
    ];

    // Poll LookupAdmin until we get the servers we want or after too many tries.
    let lookup_admin = client_realm
        .connect_to_protocol::<net_name::LookupAdminMarker>()
        .expect("failed to connect to LookupAdmin");
    let wait_for_netmgr =
        wait_for_component_stopped(&client_realm, M::MANAGEMENT_AGENT.get_component_name(), None)
            .fuse();
    futures::pin_mut!(wait_for_netmgr);
    poll_lookup_admin(&lookup_admin, &expect, &mut wait_for_netmgr, POLL_WAIT, RETRY_COUNT).await
}

/// Tests that DHCPv6 exposes DNS servers discovered dynamically and the network manager
/// configures the Lookup service.
#[variants_test]
async fn discovered_dhcpv6_dns<E: netemul::Endpoint, M: Manager>(name: &str) {
    /// DHCPv6 server IP.
    const DHCPV6_SERVER: net_types_ip::Ipv6Addr =
        net_types_ip::Ipv6Addr::from_bytes(std_ip_v6!("fe80::1").octets());
    /// DNS server served by DHCPv6.
    const DHCPV6_DNS_SERVER: fnet::Ipv6Address = fidl_ip_v6!("20a::1234:5678");

    /// Maximum number of times we'll poll `LookupAdmin` to check DNS configuration
    /// succeeded.
    const RETRY_COUNT: u64 = 60;
    /// Duration to sleep between polls.
    const POLL_WAIT: zx::Duration = zx::Duration::from_seconds(1);

    const DEFAULT_DNS_PORT: u16 = 53;

    let sandbox = netemul::TestSandbox::new().expect("failed to create sandbox");
    let network = sandbox.create_network("net").await.expect("failed to create network");

    let realm = sandbox
        .create_netstack_realm_with::<Netstack2, _, _>(
            format!("{}_client", name),
            &[
                // Start the network manager on the client.
                //
                // The network manager should listen for DNS server events from the DHCPv6 client
                // and configure the DNS resolver accordingly.
                KnownServiceProvider::Manager {
                    agent: M::MANAGEMENT_AGENT,
                    use_dhcp_server: false,
                    config: ManagerConfig::Dhcpv6,
                },
                KnownServiceProvider::Dhcpv6Client,
                KnownServiceProvider::DnsResolver,
                KnownServiceProvider::FakeClock,
            ],
        )
        .expect("failed to create realm");

    // Start networking on client realm.
    let endpoint = network.create_endpoint::<E, _>(name).await.expect("create endpoint");
    let () = endpoint.set_link_up(true).await.expect("set link up");
    let endpoint_mount_path = E::dev_path("ep");
    let endpoint_mount_path = endpoint_mount_path.as_path();
    let () = realm.add_virtual_device(&endpoint, endpoint_mount_path).await.unwrap_or_else(|e| {
        panic!("add virtual device {}: {:?}", endpoint_mount_path.display(), e)
    });

    // Make sure the Netstack got the new device added.
    let interface_state = realm
        .connect_to_protocol::<net_interfaces::StateMarker>()
        .expect("connect to fuchsia.net.interfaces/State service");
    let wait_for_netmgr =
        wait_for_component_stopped(&realm, M::MANAGEMENT_AGENT.get_component_name(), None).fuse();
    futures::pin_mut!(wait_for_netmgr);
    let _: (u64, String) = interfaces::wait_for_non_loopback_interface_up(
        &interface_state,
        &mut wait_for_netmgr,
        None,
        ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT,
    )
    .await
    .expect("wait for a non loopback interface to come up");

    // Wait for the DHCPv6 information request.
    let fake_ep = network.create_fake_endpoint().expect("failed to create fake endpoint");
    let (src_mac, dst_mac, src_ip, dst_ip, src_port, tx_id) = fake_ep
        .frame_stream()
        .map(|r| r.expect("error getting OnData event"))
        .filter_map(|(data, dropped)| {
            assert_eq!(dropped, 0);

            let mut data = data.as_slice();

            future::ready((|| {
                let ethernet = EthernetFrame::parse(&mut data, EthernetFrameLengthCheck::Check)
                    .expect("parse ethernet");

                // DHCPv6 messages are held in IPv6 packets.
                if ethernet.ethertype() != Some(net_types_ip::Ipv6::ETHER_TYPE) {
                    return None;
                }
                let ipv6 = Ipv6Packet::parse(&mut data, ()).expect("parse ipv6");

                let src_ip = ipv6.src_ip();
                let dst_ip = ipv6.dst_ip();

                // DHCPv6 messages are held in UDP packets.
                if ipv6.proto() != Ipv6Proto::Proto(IpProto::Udp) {
                    return None;
                }
                let udp = UdpPacket::parse(&mut data, UdpParseArgs::new(src_ip, dst_ip))
                    .expect("parse udp");

                // We only care about UDP packets directed at a DHCPv6 server.
                if udp.dst_port().get() != net_dhcpv6::RELAY_AGENT_AND_SERVER_PORT {
                    return None;
                }

                // We only care about DHCPv6 messages.
                let msg = v6::Message::parse(&mut data, ()).expect("parse dhcpv6");

                // We only care about DHCPv6 information requests.
                if msg.msg_type() != v6::MessageType::InformationRequest {
                    return None;
                }

                // We only care about DHCPv6 information requests for DNS servers.
                for opt in msg.options() {
                    if let v6::ParsedDhcpOption::Oro(codes) = opt {
                        if !codes.contains(&v6::OptionCode::DnsServers) {
                            return None;
                        }
                    }
                }

                Some((
                    ethernet.src_mac(),
                    ethernet.dst_mac(),
                    src_ip,
                    dst_ip,
                    udp.src_port(),
                    msg.transaction_id().clone(),
                ))
            })())
        })
        .next()
        .on_timeout(ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT.after_now(), || {
            panic!("timed out waiting for the DHCPv6 Information request");
        })
        .await
        .expect("ran out of incoming frames");
    assert!(src_ip.is_unicast_link_local(), "src ip {} should be a unicast link-local", src_ip);
    assert_eq!(
        Ok(std::net::Ipv6Addr::from(dst_ip.ipv6_bytes())),
        std::net::Ipv6Addr::from_str(
            net_dhcpv6::RELAY_AGENT_AND_SERVER_LINK_LOCAL_MULTICAST_ADDRESS
        ),
        "dst ip should be the DHCPv6 servers multicast IP"
    );
    assert_eq!(
        src_port.map(|x| x.get()),
        Some(net_dhcpv6::DEFAULT_CLIENT_PORT),
        "should use RFC defined src port"
    );

    // Send the DHCPv6 reply.
    let dns_servers = [net_types_ip::Ipv6Addr::from(DHCPV6_DNS_SERVER.addr)];
    let options = [v6::DhcpOption::ServerId(&[]), v6::DhcpOption::DnsServers(&dns_servers)];
    let builder = v6::MessageBuilder::new(v6::MessageType::Reply, tx_id, &options);
    let ser = builder
        .into_serializer()
        .encapsulate(UdpPacketBuilder::new(
            DHCPV6_SERVER,
            src_ip,
            NonZeroU16::new(net_dhcpv6::RELAY_AGENT_AND_SERVER_PORT),
            NonZeroU16::new(net_dhcpv6::DEFAULT_CLIENT_PORT)
                .expect("default DHCPv6 client port is non-zero"),
        ))
        .encapsulate(Ipv6PacketBuilder::new(
            DHCPV6_SERVER,
            src_ip,
            ipv6_consts::DEFAULT_HOP_LIMIT,
            IpProto::Udp.into(),
        ))
        .encapsulate(EthernetFrameBuilder::new(dst_mac, src_mac, EtherType::Ipv6))
        .serialize_vec_outer()
        .unwrap_or_else(|(err, _serializer)| panic!("failed to serialize DHCPv6 packet: {:?}", err))
        .unwrap_b();
    let () = fake_ep.write(ser.as_ref()).await.expect("failed to write to fake endpoint");

    // The list of servers we expect to retrieve from `fuchsia.net.name/LookupAdmin`.
    let expect = [fnet::SocketAddress::Ipv6(fnet::Ipv6SocketAddress {
        address: DHCPV6_DNS_SERVER,
        port: DEFAULT_DNS_PORT,
        zone_index: 0,
    })];

    // Poll LookupAdmin until we get the servers we want or after too many tries.
    let lookup_admin = realm
        .connect_to_protocol::<net_name::LookupAdminMarker>()
        .expect("failed to connect to LookupAdmin");
    poll_lookup_admin(&lookup_admin, &expect, &mut wait_for_netmgr, POLL_WAIT, RETRY_COUNT).await
}

async fn mock_udp_name_server(
    socket: &fuchsia_async::net::UdpSocket,
    handle_query: impl Fn(&trust_dns_proto::op::Message) -> trust_dns_proto::op::Message,
) {
    use trust_dns_proto::op::{Message, MessageType, OpCode};

    let mut buf = [0; MAX_DNS_UDP_MESSAGE_LEN];
    loop {
        let (read, src_addr) = socket.recv_from(&mut buf).await.expect("receive DNS query");
        let query = Message::from_vec(&buf[..read]).expect("deserialize DNS query");
        let mut response = handle_query(&query);
        let _: &mut Message = response
            .set_message_type(MessageType::Response)
            .set_op_code(OpCode::Update)
            .set_id(query.id())
            .add_queries(query.queries().to_vec());
        let response = response.to_vec().expect("serialize DNS response");
        let written = socket.send_to(&response, src_addr).await.expect("send DNS response");
        assert_eq!(written, response.len());
    }
}

fn answer_for_hostname(
    hostname: &str,
    resolved_addr: fnet::IpAddress,
) -> trust_dns_proto::rr::Record {
    use trust_dns_proto::rr::{DNSClass, Name, RData, Record, RecordType};

    let mut answer = Record::new();
    let fnet_ext::IpAddress(addr) = resolved_addr.into();
    let _: &mut Record = match addr {
        std::net::IpAddr::V4(addr) => {
            answer.set_rr_type(RecordType::A).set_data(Some(RData::A(addr)))
        }
        std::net::IpAddr::V6(addr) => {
            answer.set_rr_type(RecordType::AAAA).set_data(Some(RData::AAAA(addr)))
        }
    }
    .set_dns_class(DNSClass::IN)
    .set_name(Name::from_str(hostname).expect("parse hostname"));
    answer
}

const MAX_DNS_UDP_MESSAGE_LEN: usize = 512;
const EXAMPLE_HOSTNAME: &str = "www.example.com.";
const EXAMPLE_IPV4_ADDR: fnet::IpAddress = fidl_ip!("93.184.216.34");
const EXAMPLE_IPV6_ADDR: fnet::IpAddress = fidl_ip!("2606:2800:220:1:248:1893:25c8:1946");

#[fuchsia_async::run_singlethreaded(test)]
async fn successfully_retrieves_ipv6_record_despite_ipv4_timeout() {
    use trust_dns_proto::{
        op::{Message, ResponseCode},
        rr::RecordType,
    };

    let sandbox = netemul::TestSandbox::new().expect("failed to create sandbox");
    let realm = sandbox
        .create_netstack_realm_with::<Netstack2, _, _>(
            "realm",
            &[KnownServiceProvider::DnsResolver, KnownServiceProvider::FakeClock],
        )
        .expect("failed to create realm");

    let fake_clock: ftesting::FakeClockControlProxy = realm
        .connect_to_protocol::<ftesting::FakeClockControlMarker>()
        .expect("failed to connect to FakeClockControl");
    fake_clock.pause().await.expect("failed to pause fake clock");

    let dns_server: std::net::SocketAddr = std_socket_addr!("127.0.0.1:1234");
    let (socket, fuchsia_async::net::TcpListener { .. }) =
        setup_dns_server(&realm, dns_server).await;

    let name_lookup =
        realm.connect_to_protocol::<net_name::LookupMarker>().expect("failed to connect to Lookup");

    let lookup_fut = async {
        let ips = name_lookup
            .lookup_ip(
                EXAMPLE_HOSTNAME,
                net_name::LookupIpOptions {
                    ipv4_lookup: Some(true),
                    ipv6_lookup: Some(true),
                    sort_addresses: Some(true),
                    ..net_name::LookupIpOptions::EMPTY
                },
            )
            .await
            .expect("FIDL error")
            .expect("lookup_ip error");
        let want = net_name::LookupResult {
            addresses: Some(vec![EXAMPLE_IPV6_ADDR]),
            ..net_name::LookupResult::EMPTY
        };
        assert_eq!(ips, want);
    }
    .fuse();

    let response_fut = async {
        use trust_dns_proto::op::{MessageType, OpCode};

        match async_utils::fold::fold_while(
            {
                let socket = &socket;
                let buf = [0; MAX_DNS_UDP_MESSAGE_LEN];
                futures::stream::unfold((socket, buf), |(socket, mut buf)| async move {
                    let (read, src_addr) =
                        socket.recv_from(&mut buf).await.expect("receive DNS query");
                    let query = Message::from_vec(&buf[..read]).expect("deserialize DNS query");
                    Some(((query, src_addr), (socket, buf)))
                })
            },
            (false, false),
            |(seen_v4, seen_v6), (query, src_addr)| {
                let socket = &socket;
                async move {
                    let id = query.id();
                    let queries = query.queries().to_vec();
                    let query = match query.queries() {
                        [single_query] => single_query,
                        slice => panic!(
                            "Unexpectedly received message with {}: {:?}",
                            if slice.len() == 0 { "no queries" } else { "more than one query" },
                            query
                        ),
                    };
                    let requested_record_type = query.query_type();
                    let state @ (seen_v4, seen_v6) = match &requested_record_type {
                        RecordType::A => (true, seen_v6),
                        RecordType::AAAA => (seen_v4, true),
                        _ => (seen_v4, seen_v6),
                    };
                    let fold_step = if seen_v4 && seen_v6 {
                        async_utils::fold::FoldWhile::Done(())
                    } else {
                        async_utils::fold::FoldWhile::Continue(state)
                    };
                    let answer_addr = match requested_record_type {
                        RecordType::A => return fold_step,
                        RecordType::AAAA => EXAMPLE_IPV6_ADDR,
                        _ => {
                            panic!(
                                "DNS resolver should only request A or AAAA records, got {}",
                                query
                            )
                        }
                    };
                    let answer = answer_for_hostname(EXAMPLE_HOSTNAME, answer_addr);
                    let mut response = Message::new();
                    let _: &mut Message = response
                        .set_response_code(ResponseCode::NoError)
                        .add_answer(answer)
                        .set_message_type(MessageType::Response)
                        .set_op_code(OpCode::Update)
                        .set_id(id)
                        .add_queries(queries);
                    let response = response.to_vec().expect("serialize DNS response");
                    let written =
                        socket.send_to(&response, src_addr).await.expect("send DNS response");
                    assert_eq!(written, response.len());
                    fold_step
                }
            },
        )
        .await
        {
            async_utils::fold::FoldResult::ShortCircuited(()) => {}
            async_utils::fold::FoldResult::StreamEnded((seen_v4, seen_v6)) => panic!(
                "Stream unexpectedly ended before seeing requests for both v4 and v6 records. \
                    seen_v4: {}, seen_v6: {}",
                seen_v4, seen_v6
            ),
        }
    }
    .fuse();

    futures::pin_mut!(lookup_fut, response_fut);

    let () = futures::select! {
        () = lookup_fut => panic!("lookup_fut not expected to have completed"),
        () = response_fut => (),
    };

    futures::stream::once(futures::future::ready(()))
        .chain(fuchsia_async::Interval::new(
            // Wait an arbitrary short duration to avoid busy-looping.
            zx::Duration::from_millis(10),
        ))
        .take_until(lookup_fut)
        .for_each(|()| async {
            let () = fake_clock
                .advance(&mut ftesting::Increment::Determined(
                    // Advance the fake clock by a larger amount than the real time we are waiting
                    // in order to speed up the test run-time.
                    zx::Duration::from_seconds(1).into_nanos(),
                ))
                .await
                .expect("fake clock FIDL error")
                .expect("failed to increment fake clock");
        })
        .await;
}

#[fuchsia_async::run_singlethreaded(test)]
async fn fallback_on_error_response_code() {
    use itertools::Itertools as _;
    use trust_dns_proto::{
        op::{Message, ResponseCode},
        rr::RecordType,
    };

    #[derive(Debug)]
    struct FallbackTestCase {
        ipv4_lookup: bool,
        ipv6_lookup: bool,
        error_response_code: trust_dns_proto::op::ResponseCode,
    }

    let range = || IntoIterator::into_iter([true, false]);

    for test_case in range()
        .cartesian_product(range())
        .cartesian_product([
            ResponseCode::Refused,
            ResponseCode::NXDomain,
            ResponseCode::FormErr,
            ResponseCode::ServFail,
            ResponseCode::NotAuth,
            ResponseCode::BADMODE,
        ])
        .filter_map(|((ipv4_lookup, ipv6_lookup), error_response_code)| {
            (ipv4_lookup || ipv6_lookup).then(|| FallbackTestCase {
                ipv4_lookup,
                ipv6_lookup,
                error_response_code,
            })
        })
    {
        eprintln!("Entering test case: {:?}", test_case);
        let FallbackTestCase { ipv4_lookup, ipv6_lookup, error_response_code } = test_case;

        let sandbox = netemul::TestSandbox::new().expect("failed to create sandbox");
        let realm = sandbox
            .create_netstack_realm_with::<Netstack2, _, _>(
                "realm",
                &[KnownServiceProvider::DnsResolver, KnownServiceProvider::FakeClock],
            )
            .expect("failed to create realm");
        // The DNS resolver contains a timeout after which any in-flight queries are dropped, which
        // can cause test flakes.
        pause_fake_clock(&realm).await.expect("failed to pause fake clock");

        // Mock name servers in priority order.
        let erroring_dns_server: std::net::SocketAddr = std_socket_addr!("127.0.0.1:1234");
        let fallback_dns_server: std::net::SocketAddr = std_socket_addr!("127.0.0.2:5678");
        let mut dns_servers = [
            fnet_ext::SocketAddress(erroring_dns_server).into(),
            fnet_ext::SocketAddress(fallback_dns_server).into(),
        ];
        let lookup_admin = realm
            .connect_to_protocol::<net_name::LookupAdminMarker>()
            .expect("failed to connect to LookupAdmin");
        let () = lookup_admin
            .set_dns_servers(&mut dns_servers.iter_mut())
            .await
            .expect("FIDL error")
            .expect("failed to set DNS servers");
        let servers = lookup_admin.get_dns_servers().await.expect("failed to get DNS servers");
        assert_eq!(servers, dns_servers);

        let erroring_sock =
            fuchsia_async::net::UdpSocket::bind_in_realm(&realm, erroring_dns_server)
                .await
                .expect("failed to create socket");
        let fallback_sock =
            fuchsia_async::net::UdpSocket::bind_in_realm(&realm, fallback_dns_server)
                .await
                .expect("failed to create socket");

        let name_lookup = realm
            .connect_to_protocol::<net_name::LookupMarker>()
            .expect("failed to connect to Lookup");
        let lookup_fut = async {
            let ips = {
                let mut ips = name_lookup
                    .lookup_ip(
                        EXAMPLE_HOSTNAME,
                        net_name::LookupIpOptions {
                            ipv4_lookup: Some(ipv4_lookup),
                            ipv6_lookup: Some(ipv6_lookup),
                            sort_addresses: Some(true),
                            ..net_name::LookupIpOptions::EMPTY
                        },
                    )
                    .await
                    .expect("FIDL error")
                    .expect("lookup_ip error");
                if let Some(ref mut addresses) = ips.addresses {
                    // Sort the returned addresses because we don't care about their order when
                    // asserting on the contents of the response.
                    addresses.sort();
                }
                ips
            };
            let want_addresses = std::iter::empty()
                .chain(ipv4_lookup.then(|| EXAMPLE_IPV4_ADDR))
                .chain(ipv6_lookup.then(|| EXAMPLE_IPV6_ADDR))
                // Sort the expected addresses because we don't care about their order when
                // asserting on the contents of the response.
                .sorted()
                .collect::<Vec<_>>();
            let want = net_name::LookupResult {
                addresses: Some(want_addresses),
                ..net_name::LookupResult::EMPTY
            };
            assert_eq!(ips, want, "test case: {:?}", test_case);
        }
        .fuse();

        // The erroring name server expects initial queries from dns-resolver, and always replies
        // with an error response code.
        let error_server_fut = mock_udp_name_server(&erroring_sock, |_: &Message| {
            let mut response = Message::new();
            let _: &mut Message = response.set_response_code(error_response_code);
            response
        })
        .fuse();
        let expected_record_types = std::iter::empty()
            .chain(ipv4_lookup.then(|| RecordType::A))
            .chain(ipv6_lookup.then(|| RecordType::AAAA))
            .collect::<std::collections::HashSet<_>>();
        // The fallback name server expects fallback queries from dns-resolver, and replies with a
        // non-error response, unless it gets a query for a different record type than it expects.
        let fallback_fut = {
            mock_udp_name_server(&fallback_sock, move |message| {
                let query = {
                    let mut queries = message.queries().into_iter();
                    let query = queries.next().unwrap_or_else(|| {
                        panic!(
                            "message {:?} should have exactly one query, got zero instead",
                            message
                        )
                    });
                    assert_eq!(
                        queries.count(),
                        0,
                        "message {:?} should have exactly one query, got multiple instead",
                        message
                    );
                    query
                };
                let requested_record_type = query.query_type();

                if !expected_record_types.contains(&requested_record_type) {
                    // Reply with a `SERVFAIL` response since we want to ignore this query.
                    let mut response = Message::new();
                    let _: &mut Message = response.set_response_code(ResponseCode::ServFail);
                    response
                } else {
                    let answer = answer_for_hostname(
                        EXAMPLE_HOSTNAME,
                        match requested_record_type {
                            RecordType::A => EXAMPLE_IPV4_ADDR,
                            RecordType::AAAA => EXAMPLE_IPV6_ADDR,
                            _ => panic!(
                                "DNS resolver should only request A or AAAA records, \
                                 got {:?} with type {:?} instead",
                                query, requested_record_type,
                            ),
                        },
                    );
                    let mut response = Message::new();
                    let _: &mut Message =
                        response.set_response_code(ResponseCode::NoError).add_answer(answer);
                    response
                }
            })
        }
        .fuse();

        futures::pin_mut!(lookup_fut, error_server_fut, fallback_fut);
        futures::select! {
            () = lookup_fut => {},
            () = error_server_fut => panic!("error_server_fut should never complete"),
            () = fallback_fut => panic!("fallback_fut should never complete"),
        };
    }
}

async fn setup_dns_server(
    realm: &netemul::TestRealm<'_>,
    addr: std::net::SocketAddr,
) -> (fuchsia_async::net::UdpSocket, fuchsia_async::net::TcpListener) {
    let mut dns_servers = [fnet_ext::SocketAddress(addr).into()];
    let lookup_admin =
        realm.connect_to_protocol::<net_name::LookupAdminMarker>().expect("connect to protocol");
    let () = lookup_admin
        .set_dns_servers(&mut dns_servers.iter_mut())
        .await
        .expect("call set DNS servers")
        .expect("set DNS servers");
    let servers = lookup_admin.get_dns_servers().await.expect("get DNS servers");
    assert_eq!(servers, dns_servers);

    let udp_socket = fuchsia_async::net::UdpSocket::bind_in_realm(&realm, addr)
        .await
        .expect("create UDP socket and bind in realm");
    let tcp_listener = fuchsia_async::net::TcpListener::listen_in_realm(&realm, addr)
        .await
        .expect("create TCP socket and bind in realm");
    (udp_socket, tcp_listener)
}

#[fuchsia_async::run_singlethreaded(test)]
async fn no_fallback_to_tcp_on_failed_udp() {
    use trust_dns_proto::op::{Message, ResponseCode};

    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let realm = sandbox
        .create_netstack_realm_with::<Netstack2, _, _>(
            "realm",
            &[KnownServiceProvider::DnsResolver, KnownServiceProvider::FakeClock],
        )
        .expect("create realm");
    // The DNS resolver contains a timeout after which any in-flight queries are dropped, which can
    // cause test flakes.
    pause_fake_clock(&realm).await.expect("failed to pause fake clock");

    let name_lookup =
        realm.connect_to_protocol::<net_name::LookupMarker>().expect("connect to protocol");
    let lookup_fut = async {
        let lookup_result = name_lookup
            .lookup_ip(
                EXAMPLE_HOSTNAME,
                net_name::LookupIpOptions {
                    ipv4_lookup: Some(true),
                    ..net_name::LookupIpOptions::EMPTY
                },
            )
            .await
            .expect("call lookup IP");
        // The DNS resolver should not retry UDP errors over TCP, so when the request over UDP
        // fails, the overall lookup should result in an error.
        assert_eq!(lookup_result, Err(net_name::LookupError::NotFound));
    }
    .fuse();

    let (udp_socket, tcp_listener) =
        setup_dns_server(&realm, std_socket_addr!("127.0.0.1:1234")).await;
    // The name server responds to queries over UDP with a `SERVFAIL` response.
    let udp_fut = mock_udp_name_server(&udp_socket, |_: &Message| {
        let mut response = Message::new();
        let _: &mut Message = response.set_response_code(ResponseCode::ServFail);
        response
    })
    .fuse();
    // The name server panics if it gets any connection requests over TCP.
    let tcp_fut = async {
        let mut incoming = tcp_listener.accept_stream();
        if let Some(result) = incoming.next().await {
            let (_stream, addr) = result.expect("accept incoming TCP connection");
            panic!("we expect no queries over TCP; got a connection request from {:?}", addr);
        }
    }
    .fuse();

    futures::pin_mut!(lookup_fut, udp_fut, tcp_fut);
    futures::select! {
        () = lookup_fut => {},
        () = udp_fut => panic!("mock UDP name server future should never complete"),
        () = tcp_fut => panic!("mock TCP name server future should never complete"),
    };
}

#[fuchsia_async::run_singlethreaded(test)]
async fn fallback_to_tcp_on_truncated_response() {
    use trust_dns_proto::op::{Message, MessageType, OpCode, ResponseCode};

    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let realm = sandbox
        .create_netstack_realm_with::<Netstack2, _, _>(
            "realm",
            &[KnownServiceProvider::DnsResolver, KnownServiceProvider::FakeClock],
        )
        .expect("create realm");
    // The DNS resolver contains a timeout after which any in-flight queries are dropped, which can
    // cause test flakes.
    pause_fake_clock(&realm).await.expect("failed to pause fake clock");

    let name_lookup =
        realm.connect_to_protocol::<net_name::LookupMarker>().expect("connect to protocol");
    let lookup_fut = async {
        let ips = name_lookup
            .lookup_ip(
                EXAMPLE_HOSTNAME,
                net_name::LookupIpOptions {
                    ipv4_lookup: Some(true),
                    ..net_name::LookupIpOptions::EMPTY
                },
            )
            .await
            .expect("call lookup IP")
            .expect("lookup IP");
        assert_eq!(
            ips,
            net_name::LookupResult {
                addresses: Some(vec![EXAMPLE_IPV4_ADDR]),
                ..net_name::LookupResult::EMPTY
            }
        );
    }
    .fuse();

    let (udp_socket, tcp_listener) =
        setup_dns_server(&realm, std_socket_addr!("127.0.0.1:1234")).await;
    // The name server responds to queries over UDP with a response with the `truncated` bit set,
    // indicating that the query should be retried over TCP.
    //
    // Also, reply with an incorrect resolved IP address here to ensure that the eventual lookup
    // result comes from the TCP name server.
    let udp_fut = mock_udp_name_server(&udp_socket, |_: &Message| {
        let answer = answer_for_hostname(EXAMPLE_HOSTNAME, fidl_ip!("2.2.2.2"));
        let mut response = Message::new();
        let _: &mut Message = response
            .set_response_code(ResponseCode::NoError)
            .add_answer(answer)
            .set_truncated(true);
        response
    })
    .fuse();
    // The name server responds to queries over TCP with the full response.
    let tcp_fut = async {
        let mut incoming = tcp_listener.accept_stream();
        let (mut stream, _src_addr) = incoming
            .next()
            .await
            .expect("DNS query over TCP")
            .expect("accept incoming TCP connection");
        loop {
            // Read the two-octet length field, which tells us the length of the following DNS
            // message, in network (big-endian) order.
            let mut len_buf = [0_u8; 2];
            let () = stream.read_exact(&mut len_buf).await.expect("read length field");
            let len = u16::from_be_bytes(len_buf);
            let len = usize::from(len);

            let mut buf = vec![0_u8; len];
            let () = stream.read_exact(&mut buf).await.expect("receive DNS query");
            let query = Message::from_vec(&buf).expect("deserialize DNS query");
            let answer = answer_for_hostname(EXAMPLE_HOSTNAME, EXAMPLE_IPV4_ADDR);
            let mut response = Message::new();
            let _: &mut Message = response
                .set_message_type(MessageType::Response)
                .set_op_code(OpCode::Update)
                .set_response_code(ResponseCode::NoError)
                .add_answer(answer)
                .set_id(query.id())
                .add_queries(query.queries().to_vec());
            let response = response.to_vec().expect("serialize DNS response");

            // Write the two-octet length field.
            let len = u16::try_from(response.len())
                .expect("response is larger than maximum size")
                .to_be_bytes();
            let written = stream.write(&len).await.expect("send length field");
            assert_eq!(written, len.len());
            let written = stream.write(&response).await.expect("send DNS response");
            assert_eq!(written, response.len());
        }
    }
    .fuse();

    futures::pin_mut!(lookup_fut, udp_fut, tcp_fut);
    futures::select! {
        () = lookup_fut => {},
        () = udp_fut => panic!("mock UDP name server future should never complete"),
        () = tcp_fut => panic!("mock TCP name server future should never complete"),
    };
}

#[fuchsia_async::run_singlethreaded(test)]
async fn query_preferred_name_servers_first() {
    use trust_dns_proto::op::{Message, MessageType, OpCode, ResponseCode};

    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let realm = sandbox
        .create_netstack_realm_with::<Netstack2, _, _>(
            "realm",
            &[KnownServiceProvider::DnsResolver, KnownServiceProvider::FakeClock],
        )
        .expect("create realm");
    // Pause the clock so that we can always trigger timeouts manually.
    let fake_clock = realm
        .connect_to_protocol::<ftesting::FakeClockControlMarker>()
        .expect("connect to protocol");
    fake_clock.pause().await.expect("pause fake clock");

    struct NameServer {
        addr: std::net::SocketAddr,
        socket: fuchsia_async::net::UdpSocket,
    }

    impl NameServer {
        async fn bind_in_realm(addr: std::net::SocketAddr, realm: &netemul::TestRealm<'_>) -> Self {
            let socket = fuchsia_async::net::UdpSocket::bind_in_realm(realm, addr)
                .await
                .expect("create UDP socket and bind in realm");
            Self { addr, socket }
        }

        async fn ignore_next_query(&self) {
            let mut buf = [0; MAX_DNS_UDP_MESSAGE_LEN];
            let (read, src_addr) =
                self.socket.recv_from(&mut buf).await.expect("receive DNS query");
            assert_eq!(src_addr.ip(), std::net::Ipv4Addr::LOCALHOST);
            let query = Message::from_vec(&buf[..read]).expect("deserialize DNS query");
            assert_eq!(query.message_type(), MessageType::Query);
        }

        async fn handle_next_query(
            &self,
            configure_response: impl Fn(&mut trust_dns_proto::op::Message),
        ) {
            let mut buf = [0; MAX_DNS_UDP_MESSAGE_LEN];
            let (read, src_addr) =
                self.socket.recv_from(&mut buf).await.expect("receive DNS query");
            let query = Message::from_vec(&buf[..read]).expect("deserialize DNS query");
            let mut response = Message::new();
            configure_response(&mut response);
            let _: &mut Message = response
                .set_message_type(MessageType::Response)
                .set_op_code(OpCode::Update)
                .set_id(query.id())
                .add_queries(query.queries().to_vec());
            let response = response.to_vec().expect("serialize DNS response");
            let written =
                self.socket.send_to(&response, src_addr).await.expect("send DNS response");
            assert_eq!(written, response.len());
        }
    }

    // NB: this should match the setting for `ResolverOpts::num_concurrent_reqs` set
    // in the DNS resolver, to ensure that the test is deterministic and doesn't
    // rely on external factors to avoid flakiness.
    const CONCURRENT_NAME_SERVER_QUERIES: usize = 10;
    const QUERY_TIMEOUT: zx::Duration = zx::Duration::from_minutes(1);

    let preferred_servers: Vec<_> =
        futures::future::join_all((0..CONCURRENT_NAME_SERVER_QUERIES).map(|i| {
            NameServer::bind_in_realm(
                std::net::SocketAddr::new(
                    std::net::IpAddr::V4(std::net::Ipv4Addr::LOCALHOST),
                    (8000 + i).try_into().unwrap(),
                ),
                &realm,
            )
        }))
        .await;
    let secondary_server =
        NameServer::bind_in_realm(std_socket_addr!("127.0.0.1:1234"), &realm).await;

    // Configure name servers in priority order.
    {
        let mut dns_servers = preferred_servers
            .iter()
            .chain(std::iter::once(&secondary_server))
            .map(|name_server| fnet_ext::SocketAddress(name_server.addr).into())
            .collect::<Vec<_>>();
        let lookup_admin = realm
            .connect_to_protocol::<net_name::LookupAdminMarker>()
            .expect("connect to protocol");
        lookup_admin
            .set_dns_servers(&mut dns_servers.iter_mut())
            .await
            .expect("call set DNS servers")
            .expect("set DNS servers");
    }

    let name_servers_fut = async {
        // First, expect a query to each of the preferred servers. Ignore the queries to
        // cause Trust-DNS to note that queries to the preferred servers have failed.
        for server in &preferred_servers {
            server.ignore_next_query().await;
        }

        // Advance the clock in order to trigger the timeout on the first batch of
        // queries.
        fake_clock
            .advance(&mut ftesting::Increment::Determined(QUERY_TIMEOUT.into_nanos()))
            .await
            .expect("call advance")
            .expect("advance fake clock");

        // Then, respond to a query to the other name server with a successful response,
        // so Trust-DNS will note that a query to it has succeeded.
        secondary_server
            .handle_next_query(|response| {
                let _: &mut Message = response
                    .set_response_code(ResponseCode::NoError)
                    .add_answer(answer_for_hostname(EXAMPLE_HOSTNAME, EXAMPLE_IPV4_ADDR));
            })
            .await;

        // At this point, either Trust-DNS is (correctly) respecting its static
        // configuration of preferred servers, and on the next query will again query
        // the preferred servers first, or it is (incorrectly) performing sorting
        // internally based on query stats, which would cause it to query the secondary
        // server first, given it's responded successfully and the preferred servers
        // haven't.
        //
        // Expect that the preferred name servers are queried first.
        let preferred_queried =
            futures::future::select_all(preferred_servers.iter().map(|server| {
                server
                    .handle_next_query(|message| {
                        let _: &mut Message = message
                            .set_response_code(ResponseCode::NoError)
                            .add_answer(answer_for_hostname(EXAMPLE_HOSTNAME, EXAMPLE_IPV6_ADDR));
                    })
                    .boxed()
            }))
            .fuse();
        let secondary_queried = secondary_server.ignore_next_query().fuse();
        futures::pin_mut!(preferred_queried, secondary_queried);
        futures::select! {
            ((), i, _remaining) = preferred_queried =>
                println!("observed a query to preferred name server {}, exiting", i),
            () = secondary_queried => panic!("a preferred name server was not queried first"),
        }
    };

    let name_lookup =
        realm.connect_to_protocol::<net_name::LookupMarker>().expect("connect to protocol");
    let lookup_fut = async {
        // First, send a query that we expect to be answered by the non-preferred
        // server. If Trust-DNS is (incorrectly) doing its own internal
        // reprioritization, then the follow-up query will fail, because that server
        // only responds once and then drops future queries.
        let lookup_result = name_lookup
            .lookup_ip(
                EXAMPLE_HOSTNAME,
                net_name::LookupIpOptions {
                    ipv4_lookup: Some(true),
                    ..net_name::LookupIpOptions::EMPTY
                },
            )
            .await
            .expect("call lookup IP");
        assert_eq!(
            lookup_result,
            Ok(net_name::LookupResult {
                addresses: Some(vec![EXAMPLE_IPV4_ADDR]),
                ..net_name::LookupResult::EMPTY
            })
        );

        // Then make another query (for AAAA records this time to distinguish it from
        // the first), to ensure that the resolver still queries the preferred servers
        // first, even though they failed to respond the first time.
        let lookup_result = name_lookup
            .lookup_ip(
                EXAMPLE_HOSTNAME,
                net_name::LookupIpOptions {
                    ipv6_lookup: Some(true),
                    ..net_name::LookupIpOptions::EMPTY
                },
            )
            .await
            .expect("call lookup IP");
        assert_eq!(
            lookup_result,
            Ok(net_name::LookupResult {
                addresses: Some(vec![EXAMPLE_IPV6_ADDR]),
                ..net_name::LookupResult::EMPTY
            })
        );
    };

    futures::join!(name_servers_fut, lookup_fut);
}
