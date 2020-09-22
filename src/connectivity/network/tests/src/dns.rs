// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::num::NonZeroU16;
use std::str::FromStr as _;

use fidl_fuchsia_net_dhcp as net_dhcp;
use fidl_fuchsia_net_dhcpv6 as net_dhcpv6;
use fidl_fuchsia_net_name as net_name;
use fidl_fuchsia_netstack as netstack;
use fuchsia_async::{DurationExt as _, TimeoutExt as _};
use fuchsia_zircon as zx;

use anyhow::Context as _;
use futures::future::{self, FusedFuture, Future, FutureExt as _};
use futures::stream::TryStreamExt as _;
use net_declare::{fidl_ip_v4, fidl_ip_v6, fidl_subnet, std_ip_v6};
use net_types::ethernet::Mac;
use net_types::ip as net_types_ip;
use net_types::Witness;
use netstack_testing_macros::variants_test;
use packet::serialize::{InnerPacketBuilder as _, Serializer as _};
use packet::ParsablePacket as _;
use packet_formats::ethernet::{EtherType, EthernetFrameBuilder};
use packet_formats::icmp::ndp::{
    options::{NdpOption, RecursiveDnsServer},
    RouterAdvertisement,
};
use packet_formats::ip::IpProto;
use packet_formats::ipv6::Ipv6PacketBuilder;
use packet_formats::testutil::parse_ip_packet_in_ethernet_frame;
use packet_formats::udp::{UdpPacket, UdpPacketBuilder, UdpParseArgs};
use packet_formats_dhcp::v6;

use crate::constants::{eth as eth_consts, ipv6 as ipv6_consts};
use crate::environments::{KnownServices, Manager, NetCfg, Netstack2, TestSandboxExt as _};
use crate::ipv6::write_ndp_message;
use crate::{Result, ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT};

/// Keep polling the lookup admin's DNS servers until it returns `expect`.
async fn poll_lookup_admin<
    F: Unpin + FusedFuture + Future<Output = Result<fuchsia_component::client::ExitStatus>>,
>(
    lookup_admin: &net_name::LookupAdminProxy,
    expect: Vec<fidl_fuchsia_net::SocketAddress>,
    mut wait_for_netmgr_fut: &mut F,
    poll_wait: zx::Duration,
    retry_count: u64,
) -> Result {
    for i in 0..retry_count {
        let () = futures::select! {
            () = fuchsia_async::Timer::new(poll_wait.after_now()).fuse() => {
                Ok(())
            }
            wait_for_netmgr_res = wait_for_netmgr_fut => {
                Err(anyhow::anyhow!("the network manager unexpectedly exited with exit status = {:?}", wait_for_netmgr_res?))
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
    const SERVER_ADDR: fidl_fuchsia_net::Subnet = fidl_subnet!(192.168.0.1/24);
    /// DNS server served by DHCP.
    const DHCP_DNS_SERVER: fidl_fuchsia_net::Ipv4Address = fidl_ip_v4!(123.12.34.56);
    /// DNS server served by NDP.
    const NDP_DNS_SERVER: fidl_fuchsia_net::Ipv6Address = fidl_ip_v6!(20a::1234:5678);

    /// Maximum number of times we'll poll `LookupAdmin` to check DNS configuration
    /// succeeded.
    const RETRY_COUNT: u64 = 60;
    /// Duration to sleep between polls.
    const POLL_WAIT: zx::Duration = zx::Duration::from_seconds(1);

    const DEFAULT_DNS_PORT: u16 = 53;

    let sandbox = netemul::TestSandbox::new().context("failed to create sandbox")?;

    let network = sandbox.create_network("net").await.context("failed to create network")?;
    let server_environment = sandbox
        .create_netstack_environment_with::<Netstack2, _, _>(
            format!("{}_server", name),
            vec![
                KnownServices::DhcpServer.into_launch_service_with_arguments(vec![
                    // TODO: Once DHCP server supports dynamic configuration
                    // (fxbug.dev/45830), stop using the config file and configure
                    // it programatically. For now, the constants defined in this
                    // test reflect the ones defined in test_config.json.
                    "--config",
                    "/config/data/dhcpd-testing/test_config.json",
                ]),
                KnownServices::SecureStash.into_launch_service(),
            ],
        )
        .context("failed to create server environment")?;

    let client_environment = sandbox
        .create_netstack_environment_with::<Netstack2, _, _>(
            format!("{}_client", name),
            &[KnownServices::LookupAdmin],
        )
        .context("failed to create client environment")?;

    let _server_iface = server_environment
        .join_network::<E, _>(
            &network,
            "server-ep",
            netemul::InterfaceConfig::StaticIp(SERVER_ADDR),
        )
        .await
        .context("failed to configure server networking")?;

    let dhcp_server = server_environment
        .connect_to_service::<net_dhcp::Server_Marker>()
        .context("failed to connext to DHCP server")?;

    let () = dhcp_server
        .set_option(&mut net_dhcp::Option_::DomainNameServer(vec![DHCP_DNS_SERVER]))
        .await
        .context("Failed to set DNS option")?
        .map_err(zx::Status::from_raw)
        .context("dhcp/Server.SetOption returned error")?;

    // Start networking on client environment.
    let _client_iface = client_environment
        .join_network::<E, _>(&network, "client-ep", netemul::InterfaceConfig::Dhcp)
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
    let options = [NdpOption::RecursiveDnsServer(rdnss)];
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

    // Start the network manager on the client.
    //
    // The network manager should listen for DNS server events from the netstack and
    // configure the DNS resolver accordingly.
    let launcher =
        client_environment.get_launcher().context("failed to create launcher for client env")?;
    let mut netmgr =
        fuchsia_component::client::launch(&launcher, M::PKG_URL.to_string(), M::testing_args())
            .context("launch the network manager")?;

    // The list of servers we expect to retrieve from `fuchsia.net.name/LookupAdmin`.
    let expect = vec![
        fidl_fuchsia_net::SocketAddress::Ipv6(fidl_fuchsia_net::Ipv6SocketAddress {
            address: NDP_DNS_SERVER,
            port: DEFAULT_DNS_PORT,
            zone_index: 0,
        }),
        fidl_fuchsia_net::SocketAddress::Ipv4(fidl_fuchsia_net::Ipv4SocketAddress {
            address: DHCP_DNS_SERVER,
            port: DEFAULT_DNS_PORT,
        }),
    ];

    // Poll LookupAdmin until we get the servers we want or after too many tries.
    let lookup_admin = client_environment
        .connect_to_service::<net_name::LookupAdminMarker>()
        .context("failed to connect to LookupAdmin")?;
    let mut wait_for_netmgr_fut = netmgr.wait().fuse();
    poll_lookup_admin(&lookup_admin, expect, &mut wait_for_netmgr_fut, POLL_WAIT, RETRY_COUNT)
        .await
        .context("poll lookup admin")
}

/// Tests that DHCPv6 exposes DNS servers discovered dynamically and the network manager
/// configures the Lookup service.
#[variants_test]
async fn test_discovered_dhcpv6_dns<E: netemul::Endpoint>(name: &str) -> Result {
    /// DHCPv6 server IP.
    const DHCPV6_SERVER: net_types_ip::Ipv6Addr =
        net_types_ip::Ipv6Addr::new(std_ip_v6!(fe80::1).octets());
    /// DNS server served by DHCPv6.
    const DHCPV6_DNS_SERVER: fidl_fuchsia_net::Ipv6Address = fidl_ip_v6!(20a::1234:5678);
    const DEFAULT_TTL: u8 = 64;

    /// Maximum number of times we'll poll `LookupAdmin` to check DNS configuration
    /// succeeded.
    const RETRY_COUNT: u64 = 60;
    /// Duration to sleep between polls.
    const POLL_WAIT: zx::Duration = zx::Duration::from_seconds(1);

    const DEFAULT_DNS_PORT: u16 = 53;

    let sandbox = netemul::TestSandbox::new().context("failed to create sandbox")?;
    let network = sandbox.create_network("net").await.context("failed to create network")?;

    let environment = sandbox
        .create_netstack_environment_with::<Netstack2, _, _>(
            format!("{}_client", name),
            &[KnownServices::Dhcpv6Client, KnownServices::LookupAdmin],
        )
        .context("failed to create environment")?;

    // Start the network manager on the client.
    //
    // The network manager should listen for DNS server events from the DHCPv6 client and
    // configure the DNS resolver accordingly.
    let launcher = environment.get_launcher().context("failed to create launcher for env")?;
    let mut netmgr = fuchsia_component::client::launch(
        &launcher,
        NetCfg::PKG_URL.to_string(),
        NetCfg::testing_args(),
    )
    .context("launch the network manager")?;

    // Start networking on client environment.
    let endpoint = network.create_endpoint::<E, _>(name).await.context("create endpoint")?;
    let () = endpoint.set_link_up(true).await.context("set link up")?;
    let endpoint_mount_path = E::dev_path("ep");
    let endpoint_mount_path = endpoint_mount_path.as_path();
    let () = environment
        .add_virtual_device(&endpoint, endpoint_mount_path)
        .with_context(|| format!("add virtual device {}", endpoint_mount_path.display()))?;

    // Make sure the Netstack got the new device added.
    let netstack = environment
        .connect_to_service::<netstack::NetstackMarker>()
        .context("connect to netstack service")?;
    let mut wait_for_netmgr_fut = netmgr.wait().fuse();
    let (_id, _name): (u32, String) = crate::management::wait_for_non_loopback_interface_up(
        &netstack,
        &mut wait_for_netmgr_fut,
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
                        if let v6::DhcpOption::Oro(codes) = opt {
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
    let options = [
        v6::DhcpOption::ServerId(&[]),
        v6::DhcpOption::DnsServers(vec![std::net::Ipv6Addr::from(DHCPV6_DNS_SERVER.addr)]),
    ];
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
        .encapsulate(Ipv6PacketBuilder::new(DHCPV6_SERVER, src_ip, DEFAULT_TTL, IpProto::Udp))
        .encapsulate(EthernetFrameBuilder::new(dst_mac, src_mac, EtherType::Ipv6))
        .serialize_vec_outer()
        .map_err(|_| anyhow::anyhow!("failed to serialize DHCPv6 packet"))?
        .unwrap_b();
    let () = fake_ep.write(ser.as_ref()).await.context("failed to write to fake endpoint")?;

    // The list of servers we expect to retrieve from `fuchsia.net.name/LookupAdmin`.
    let expect = vec![fidl_fuchsia_net::SocketAddress::Ipv6(fidl_fuchsia_net::Ipv6SocketAddress {
        address: DHCPV6_DNS_SERVER,
        port: DEFAULT_DNS_PORT,
        zone_index: 0,
    })];

    // Poll LookupAdmin until we get the servers we want or after too many tries.
    let lookup_admin = environment
        .connect_to_service::<net_name::LookupAdminMarker>()
        .context("failed to connect to LookupAdmin")?;
    poll_lookup_admin(&lookup_admin, expect, &mut wait_for_netmgr_fut, POLL_WAIT, RETRY_COUNT)
        .await
        .context("poll lookup admin")
}
