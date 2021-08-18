// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use std::num::NonZeroU16;
use std::str::FromStr as _;

use fidl_fuchsia_net as fnet;
use fidl_fuchsia_net_dhcp as net_dhcp;
use fidl_fuchsia_net_dhcpv6 as net_dhcpv6;
use fidl_fuchsia_net_interfaces as net_interfaces;
use fidl_fuchsia_net_name as net_name;
use fuchsia_async::{DurationExt as _, TimeoutExt as _};
use fuchsia_zircon as zx;

use anyhow::Context as _;
use futures::future::{self, FusedFuture, Future, FutureExt as _};
use futures::stream::{self, StreamExt as _, TryStreamExt as _};
use net_declare::{fidl_ip, fidl_ip_v4, fidl_ip_v6, fidl_subnet, std_ip_v6, std_socket_addr};
use net_types::ethernet::Mac;
use net_types::ip as net_types_ip;
use net_types::Witness;
use netemul::RealmUdpSocket as _;
use netstack_testing_common::constants::{eth as eth_consts, ipv6 as ipv6_consts};
use netstack_testing_common::realms::{
    constants, KnownServiceProvider, Manager, NetCfg, Netstack2, TestSandboxExt as _,
};
use netstack_testing_common::{
    wait_for_component_stopped, write_ndp_message, Result, ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT,
};
use netstack_testing_macros::variants_test;
use packet::serialize::{InnerPacketBuilder as _, Serializer as _};
use packet::ParsablePacket as _;
use packet_formats::ethernet::{EtherType, EthernetFrameBuilder};
use packet_formats::icmp::ndp::{
    options::{NdpOptionBuilder, RecursiveDnsServer},
    RouterAdvertisement,
};
use packet_formats::ip::IpProto;
use packet_formats::ipv6::Ipv6PacketBuilder;
use packet_formats::testutil::parse_ip_packet_in_ethernet_frame;
use packet_formats::udp::{UdpPacket, UdpPacketBuilder, UdpParseArgs};
use packet_formats_dhcp::v6;
use test_case::test_case;

/// Keep polling the lookup admin's DNS servers until it returns `expect`.
async fn poll_lookup_admin<
    F: Unpin + FusedFuture + Future<Output = Result<component_events::events::Stopped>>,
>(
    lookup_admin: &net_name::LookupAdminProxy,
    expect: &[fnet::SocketAddress],
    mut wait_for_netmgr_fut: &mut F,
    poll_wait: zx::Duration,
    retry_count: u64,
) -> Result {
    for i in 0..retry_count {
        let () = futures::select! {
            () = fuchsia_async::Timer::new(poll_wait.after_now()).fuse() => Ok(()),
            stopped_event = wait_for_netmgr_fut => {
                Err(anyhow::anyhow!(
                    "the network manager unexpectedly exited with event: {:?}",
                    stopped_event,
                ))
            }
        }?;

        let servers = lookup_admin.get_dns_servers().await.context("Failed to get DNS servers")?;
        println!("attempt {}) Got DNS servers {:?}", i, servers);

        if servers == expect {
            return Ok(());
        }
    }

    // Too many retries.
    Err(anyhow::anyhow!(
        "timed out waiting for DNS server configurations; retry_count={}, poll_wait={:?}",
        retry_count,
        poll_wait,
    ))
}

/// Tests that Netstack exposes DNS servers discovered dynamically and NetworkManager
/// configures the Lookup service.
#[variants_test]
async fn test_discovered_dns<E: netemul::Endpoint, M: Manager>(name: &str) -> Result {
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

    let sandbox = netemul::TestSandbox::new().context("failed to create sandbox")?;

    let network = sandbox.create_network("net").await.context("failed to create network")?;
    let server_realm = sandbox
        .create_netstack_realm_with::<Netstack2, _, _>(
            format!("{}_server", name),
            &[
                KnownServiceProvider::DnsResolver,
                KnownServiceProvider::DhcpServer { persistent: false },
                KnownServiceProvider::SecureStash,
            ],
        )
        .context("failed to create server realm")?;

    let client_realm = sandbox
        .create_netstack_realm_with::<Netstack2, _, _>(
            format!("{}_client", name),
            &[
                // Start the network manager on the client.
                //
                // The network manager should listen for DNS server events from the netstack and
                // configure the DNS resolver accordingly.
                KnownServiceProvider::Manager(M::MANAGEMENT_AGENT),
                KnownServiceProvider::DnsResolver,
                KnownServiceProvider::DhcpServer { persistent: false },
                KnownServiceProvider::Dhcpv6Client,
                KnownServiceProvider::SecureStash,
            ],
        )
        .context("failed to create client realm")?;

    let _server_iface = server_realm
        .join_network::<E, _>(
            &network,
            "server-ep",
            &netemul::InterfaceConfig::StaticIp(SERVER_ADDR),
        )
        .await
        .context("failed to configure server networking")?;

    let dhcp_server = server_realm
        .connect_to_protocol::<net_dhcp::Server_Marker>()
        .context("failed to connect to DHCP server")?;

    let dhcp_server_ref = &dhcp_server;
    // TODO(fxbug.dev/62554): derive these from SERVER_ADDR.
    let () = stream::iter(
        [
            fidl_fuchsia_net_dhcp::Parameter::IpAddrs(vec![fidl_ip_v4!("192.168.0.1")]),
            fidl_fuchsia_net_dhcp::Parameter::AddressPool(fidl_fuchsia_net_dhcp::AddressPool {
                prefix_length: Some(25),
                range_start: Some(fidl_ip_v4!("192.168.0.2")),
                range_stop: Some(fidl_ip_v4!("192.168.0.5")),
                ..fidl_fuchsia_net_dhcp::AddressPool::EMPTY
            }),
            fidl_fuchsia_net_dhcp::Parameter::BoundDeviceNames(vec!["eth2".to_string()]),
        ]
        .iter_mut(),
    )
    .map(Ok)
    .try_for_each_concurrent(None, |parameter| async move {
        dhcp_server_ref
            .set_parameter(parameter)
            .await
            .context("failed to call dhcp/Server.SetParameter")?
            .map_err(fuchsia_zircon::Status::from_raw)
            .with_context(|| format!("dhcp/Server.SetParameter({:?}) returned error", parameter))
    })
    .await?;

    let () = dhcp_server
        .set_option(&mut net_dhcp::Option_::DomainNameServer(vec![DHCP_DNS_SERVER]))
        .await
        .context("Failed to set DNS option")?
        .map_err(zx::Status::from_raw)
        .context("dhcp/Server.SetOption returned error")?;

    let () = dhcp_server
        .start_serving()
        .await
        .context("failed to call dhcp/Server.StartServing")?
        .map_err(fuchsia_zircon::Status::from_raw)
        .context("dhcp/Server.StartServing returned error")?;

    // Start networking on client realm.
    let _client_iface = client_realm
        .join_network::<E, _>(&network, "client-ep", &netemul::InterfaceConfig::Dhcp)
        .await
        .context("failed to configure client networking")?;

    // Send a Router Advertisement with DNS server configurations.
    let fake_ep = network.create_fake_endpoint()?;
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
    .context("failed to write NDP message")?;

    // The list of servers we expect to retrieve from `fuchsia.net.name/LookupAdmin`.
    let expect = [
        fnet::SocketAddress::Ipv6(fnet::Ipv6SocketAddress {
            address: NDP_DNS_SERVER,
            port: DEFAULT_DNS_PORT,
            zone_index: 0,
        }),
        fnet::SocketAddress::Ipv4(fnet::Ipv4SocketAddress {
            address: DHCP_DNS_SERVER,
            port: DEFAULT_DNS_PORT,
        }),
    ];

    // Poll LookupAdmin until we get the servers we want or after too many tries.
    let lookup_admin = client_realm
        .connect_to_protocol::<net_name::LookupAdminMarker>()
        .context("failed to connect to LookupAdmin")?;
    let wait_for_netmgr =
        wait_for_component_stopped(&client_realm, constants::netcfg::COMPONENT_NAME, None).fuse();
    futures::pin_mut!(wait_for_netmgr);
    poll_lookup_admin(&lookup_admin, &expect, &mut wait_for_netmgr, POLL_WAIT, RETRY_COUNT)
        .await
        .context("poll lookup admin")
}

/// Tests that DHCPv6 exposes DNS servers discovered dynamically and the network manager
/// configures the Lookup service.
#[variants_test]
async fn test_discovered_dhcpv6_dns<E: netemul::Endpoint>(name: &str) -> Result {
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

    let sandbox = netemul::TestSandbox::new().context("failed to create sandbox")?;
    let network = sandbox.create_network("net").await.context("failed to create network")?;

    let realm = sandbox
        .create_netstack_realm_with::<Netstack2, _, _>(
            format!("{}_client", name),
            &[
                // Start the network manager on the client.
                //
                // The network manager should listen for DNS server events from the DHCPv6 client
                // and configure the DNS resolver accordingly.
                KnownServiceProvider::Manager(NetCfg::MANAGEMENT_AGENT),
                KnownServiceProvider::DhcpServer { persistent: false },
                KnownServiceProvider::Dhcpv6Client,
                KnownServiceProvider::DnsResolver,
                KnownServiceProvider::SecureStash,
            ],
        )
        .context("failed to create realm")?;

    // Start networking on client realm.
    let endpoint = network.create_endpoint::<E, _>(name).await.context("create endpoint")?;
    let () = endpoint.set_link_up(true).await.context("set link up")?;
    let endpoint_mount_path = E::dev_path("ep");
    let endpoint_mount_path = endpoint_mount_path.as_path();
    let () = realm
        .add_virtual_device(&endpoint, endpoint_mount_path)
        .await
        .with_context(|| format!("add virtual device {}", endpoint_mount_path.display()))?;

    // Make sure the Netstack got the new device added.
    let interface_state = realm
        .connect_to_protocol::<net_interfaces::StateMarker>()
        .context("connect to fuchsia.net.interfaces/State service")?;
    let wait_for_netmgr =
        wait_for_component_stopped(&realm, constants::netcfg::COMPONENT_NAME, None).fuse();
    futures::pin_mut!(wait_for_netmgr);
    let _: (u64, String) = netstack_testing_common::wait_for_non_loopback_interface_up(
        &interface_state,
        &mut wait_for_netmgr,
        None,
        ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT,
    )
    .await
    .context("wait for a non loopback interface to come up")?;

    // Wait for the DHCPv6 information request.
    let fake_ep = network.create_fake_endpoint()?;
    let (src_mac, dst_mac, src_ip, dst_ip, src_port, tx_id) = fake_ep
        .frame_stream()
        .try_filter_map(|(data, dropped)| {
            assert_eq!(dropped, 0);
            future::ok(parse_ip_packet_in_ethernet_frame::<net_types_ip::Ipv6>(&data).map_or(
                None,
                |(mut body, src_mac, dst_mac, src_ip, dst_ip, _proto, _ttl)| {
                    // DHCPv6 messages are held in UDP packets.
                    let udp = match UdpPacket::parse(&mut body, UdpParseArgs::new(src_ip, dst_ip)) {
                        Ok(o) => o,
                        Err(_) => return None,
                    };

                    // We only care about UDP packets directed at a DHCPv6 server.
                    if udp.dst_port().get() != net_dhcpv6::RELAY_AGENT_AND_SERVER_PORT {
                        return None;
                    }

                    // We only care about DHCPv6 messages.
                    let mut body = udp.body();
                    let msg = match v6::Message::parse(&mut body, ()) {
                        Ok(o) => o,
                        Err(_) => return None,
                    };

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
                        src_mac,
                        dst_mac,
                        src_ip,
                        dst_ip,
                        udp.src_port(),
                        msg.transaction_id().clone(),
                    ))
                },
            ))
        })
        .try_next()
        .map(|r| r.context("error getting OnData event"))
        .on_timeout(ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT.after_now(), || {
            return Err(anyhow::anyhow!("timed out waiting for the DHCPv6 Information request"));
        })
        .await
        .context("wait for DHCPv6 Information Request")?
        .ok_or(anyhow::anyhow!("ran out of incoming frames"))?;
    assert!(src_ip.is_unicast_linklocal(), "src ip {} should be a unicast link-local", src_ip);
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
    let dns_servers = [std::net::Ipv6Addr::from(DHCPV6_DNS_SERVER.addr)];
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
        .map_err(|_| anyhow::anyhow!("failed to serialize DHCPv6 packet"))?
        .unwrap_b();
    let () = fake_ep.write(ser.as_ref()).await.context("failed to write to fake endpoint")?;

    // The list of servers we expect to retrieve from `fuchsia.net.name/LookupAdmin`.
    let expect = [fnet::SocketAddress::Ipv6(fnet::Ipv6SocketAddress {
        address: DHCPV6_DNS_SERVER,
        port: DEFAULT_DNS_PORT,
        zone_index: 0,
    })];

    // Poll LookupAdmin until we get the servers we want or after too many tries.
    let lookup_admin = realm
        .connect_to_protocol::<net_name::LookupAdminMarker>()
        .context("failed to connect to LookupAdmin")?;
    poll_lookup_admin(&lookup_admin, &expect, &mut wait_for_netmgr, POLL_WAIT, RETRY_COUNT)
        .await
        .context("poll lookup admin")
}

const EXAMPLE_HOSTNAME: &str = "www.example.com.";
const EXAMPLE_IPV4_ADDR: fnet::IpAddress = fidl_ip!("93.184.216.34");
const EXAMPLE_IPV6_ADDR: fnet::IpAddress = fidl_ip!("2606:2800:220:1:248:1893:25c8:1946");

#[test_case(true, false, EXAMPLE_HOSTNAME, EXAMPLE_IPV4_ADDR; "ipv4 only")]
#[test_case(false, true, EXAMPLE_HOSTNAME, EXAMPLE_IPV6_ADDR; "ipv6 only")]
#[test_case(true, true, EXAMPLE_HOSTNAME, EXAMPLE_IPV4_ADDR; "ipv4 and ipv6")]
#[fuchsia_async::run_singlethreaded(test)]
async fn test_fallback_on_query_refused(
    ipv4_lookup: bool,
    ipv6_lookup: bool,
    hostname: &str,
    resolved_addr: fnet::IpAddress,
) {
    use trust_dns_proto::{
        op::{message::Message, MessageType, OpCode, ResponseCode},
        rr::{dns_class::DNSClass, Name, RData, Record, RecordType},
    };

    const MAX_DNS_UDP_MESSAGE_LEN: usize = 512;
    let refusing_dns_server: std::net::SocketAddr = std_socket_addr!("127.0.0.1:1234");
    let fallback_dns_server: std::net::SocketAddr = std_socket_addr!("127.0.0.1:5678");

    let sandbox = netemul::TestSandbox::new().expect("failed to create sandbox");
    let realm = sandbox
        .create_netstack_realm_with::<Netstack2, _, _>(
            "realm",
            &[KnownServiceProvider::DnsResolver],
        )
        .expect("failed to create realm");

    // Mock name servers in priority order.
    let mut expect = [
        fidl_fuchsia_net_ext::SocketAddress(refusing_dns_server).into(),
        fidl_fuchsia_net_ext::SocketAddress(fallback_dns_server).into(),
    ];
    let lookup_admin = realm
        .connect_to_protocol::<net_name::LookupAdminMarker>()
        .expect("failed to connect to LookupAdmin");
    let () = lookup_admin
        .set_dns_servers(&mut expect.iter_mut())
        .await
        .expect("FIDL error")
        .expect("failed to set DNS servers");
    let servers = lookup_admin.get_dns_servers().await.expect("failed to get DNS servers");
    assert_eq!(servers, expect);

    let refusing_sock = fuchsia_async::net::UdpSocket::bind_in_realm(&realm, refusing_dns_server)
        .await
        .expect("failed to create socket");
    let fallback_sock = fuchsia_async::net::UdpSocket::bind_in_realm(&realm, fallback_dns_server)
        .await
        .expect("failed to create socket");

    let name_lookup =
        realm.connect_to_protocol::<net_name::LookupMarker>().expect("failed to connect to Lookup");
    let lookup_fut = async {
        let ips = name_lookup
            .lookup_ip(
                hostname,
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
        assert_eq!(
            ips,
            net_name::LookupResult {
                addresses: Some(vec![resolved_addr]),
                ..net_name::LookupResult::EMPTY
            }
        );
    }
    .fuse();

    async fn run_mock_server(
        socket: &fuchsia_async::net::UdpSocket,
        handle_query: impl Fn(&Message) -> Message,
    ) {
        let mut buf = [0; MAX_DNS_UDP_MESSAGE_LEN];
        loop {
            let (read, src_addr) =
                socket.recv_from(&mut buf).await.expect("failed to receive DNS query");
            let query = Message::from_vec(&buf[..read]).expect("failed to deserialize DNS query");
            let mut message = handle_query(&query);
            let _: &mut Message = message.set_id(query.id()).add_queries(query.queries().to_vec());
            let response = message.to_vec().expect("failed to serialize DNS message");
            let written =
                socket.send_to(&response, src_addr).await.expect("failed to send DNS message");
            assert_eq!(written, response.len());
        }
    }
    // The refusing name server expects initial queries from dns-resolver, and always replies with a
    // `REFUSED` response.
    let refuse_fut = run_mock_server(&refusing_sock, |_: &Message| {
        let mut message = Message::new();
        let _: &mut Message = message
            .set_message_type(MessageType::Response)
            .set_response_code(ResponseCode::Refused)
            .set_op_code(OpCode::Update);
        message
    })
    .fuse();
    // The fallback name server expects fallback queries from dns-resolver, and replies with a
    // non-error response, unless it gets a query for a different record type than it expects.
    let fallback_fut = {
        let expected_record_type = if ipv4_lookup { RecordType::A } else { RecordType::AAAA };
        run_mock_server(&fallback_sock, move |query| {
            if query.queries().iter().any(|q| q.query_type() != expected_record_type) {
                // Reply with a `SERVFAIL` response since we want to ignore this query.
                let mut message = Message::new();
                let _: &mut Message = message
                    .set_id(query.id())
                    .set_message_type(MessageType::Response)
                    .set_response_code(ResponseCode::ServFail)
                    .set_op_code(OpCode::Update)
                    .add_queries(query.queries().to_vec());
                message
            } else {
                let mut answer = Record::new();
                let _: &mut Record = answer
                    .set_name(Name::from_str(hostname).expect("failed to parse hostname"))
                    .set_dns_class(DNSClass::IN);
                let fidl_fuchsia_net_ext::IpAddress(addr) = resolved_addr.into();
                match addr {
                    std::net::IpAddr::V4(addr) => {
                        let _: &mut Record =
                            answer.set_rr_type(RecordType::A).set_rdata(RData::A(addr));
                    }
                    std::net::IpAddr::V6(addr) => {
                        let _: &mut Record =
                            answer.set_rr_type(RecordType::AAAA).set_rdata(RData::AAAA(addr));
                    }
                }
                let mut message = Message::new();
                let _: &mut Message = message
                    .set_message_type(MessageType::Response)
                    .set_op_code(OpCode::Update)
                    .add_answer(answer);
                message
            }
        })
    }
    .fuse();

    pin_utils::pin_mut!(lookup_fut, refuse_fut, fallback_fut);
    futures::select! {
        () = lookup_fut => {},
        () = refuse_fut => panic!("refuse_fut should never complete"),
        () = fallback_fut => panic!("fallback_fut should never complete"),
    };
}
