// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_netstack as netstack;

use anyhow::Context as _;
use futures;
use futures::future::FutureExt;
use net_declare::{fidl_ip, fidl_ip_v4, fidl_ip_v6};
use net_types::ethernet::Mac;
use net_types::ip as net_types_ip;
use net_types::Witness;
use netstack_testing_macros::variants_test;
use packet_formats::icmp::ndp::{
    options::{NdpOption, RecursiveDnsServer},
    RouterAdvertisement,
};

use crate::constants::{eth as eth_consts, ipv6 as ipv6_consts};
use crate::environments::*;
use crate::ipv6::write_ndp_message;
use crate::*;

/// Tests that dns-resolver does not support `fuchsia.net.name/LookupAdmin.SetDefaultDnsServers`.
#[fuchsia_async::run_singlethreaded(test)]
async fn test_set_default_dns_servers() -> Result {
    /// Static DNS server given directly to `LookupAdmin`.
    const STATIC_DNS_SERVER: fidl_fuchsia_net::IpAddress = fidl_ip!(123.12.34.99);

    let name = "test_set_default_dns_servers";
    let sandbox = TestSandbox::new().context("failed to create sandbox")?;

    let client_environment = sandbox
        .create_environment(name, &[KnownServices::LookupAdmin])
        .context("failed to create environment")?;

    // Connect to `fuchsia.net.name,LookupAdmin` and attempt to set the default servers. This
    // should fail with a not supported error as dns-resolver only uses `SetDnsServers` to
    // configure DNS servers.
    let lookup_admin = client_environment
        .connect_to_service::<fidl_fuchsia_net_name::LookupAdminMarker>()
        .context("failed to connect to LookupAdmin")?;
    let ret = lookup_admin
        .set_default_dns_servers(&mut vec![STATIC_DNS_SERVER].iter_mut())
        .await
        .context("Failed to set default DNS servers")?
        .map_err(fuchsia_zircon::Status::from_raw);
    let want_err = fuchsia_zircon::Status::NOT_SUPPORTED;
    let () = match ret {
        Ok(()) => Err(anyhow::anyhow!("call to SetDefaultDnsServers unexpectedly succeeded")),
        Err(status) if status == want_err => Ok(()),
        Err(status) => {
            Err(anyhow::anyhow!("got unexpected error; got = {}, want = {}", status, want_err))
        }
    }?;

    Ok(())
}

/// Tests that Netstack exposes DNS servers discovered dynamically and NetworkManager
/// configures the Lookup service.
#[variants_test]
async fn test_discovered_dns<E: Endpoint, M: Manager>(name: &str) -> Result {
    const SERVER_IP: fidl_fuchsia_net::IpAddress = fidl_ip!(192.168.0.1);
    /// DNS server served by DHCP.
    const DHCP_DNS_SERVER: fidl_fuchsia_net::Ipv4Address = fidl_ip_v4!(123.12.34.56);
    /// DNS server served by NDP.
    const NDP_DNS_SERVER: fidl_fuchsia_net::Ipv6Address = fidl_ip_v6!(20a::1234:5678);

    /// Maximum number of times we'll poll `LookupAdmin` to check DNS configuration
    /// succeeded.
    const RETRY_COUNT: u64 = 60;
    /// Duration to sleep between polls.
    const POLL_WAIT: fuchsia_zircon::Duration = fuchsia_zircon::Duration::from_seconds(1);

    const DEFAULT_DNS_PORT: u16 = 53;

    let sandbox = TestSandbox::new().context("failed to create sandbox")?;

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
            InterfaceConfig::StaticIp(fidl_fuchsia_net::Subnet { addr: SERVER_IP, prefix_len: 24 }),
        )
        .await
        .context("failed to configure server networking")?;

    let dhcp_server = server_environment
        .connect_to_service::<fidl_fuchsia_net_dhcp::Server_Marker>()
        .context("failed to connext to DHCP server")?;

    let () = dhcp_server
        .set_option(&mut fidl_fuchsia_net_dhcp::Option_::DomainNameServer(vec![DHCP_DNS_SERVER]))
        .await
        .context("Failed to set DNS option")?
        .map_err(fuchsia_zircon::Status::from_raw)
        .context("dhcp/Server.SetOption returned error")?;

    // Start networking on client environment.
    let client_iface = client_environment
        .join_network::<E, _>(&network, "client-ep", InterfaceConfig::Dhcp)
        .await
        .context("failed to configure client networking")?;
    let client_netstack = client_environment
        .connect_to_service::<netstack::NetstackMarker>()
        .context("failed to connect to netstack service")?;
    let () = wait_for_interface_up(
        client_netstack.take_event_stream(),
        client_iface.id(),
        ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT,
    )
    .await?;

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
        fidl_fuchsia_net_name::DnsServer_ {
            address: Some(fidl_fuchsia_net::SocketAddress::Ipv6(
                fidl_fuchsia_net::Ipv6SocketAddress {
                    address: NDP_DNS_SERVER,
                    port: DEFAULT_DNS_PORT,
                    zone_index: 0,
                },
            )),
            source: Some(fidl_fuchsia_net_name::DnsServerSource::Ndp(
                fidl_fuchsia_net_name::NdpDnsServerSource {
                    source_interface: Some(client_iface.id()),
                },
            )),
        },
        fidl_fuchsia_net_name::DnsServer_ {
            address: Some(fidl_fuchsia_net::SocketAddress::Ipv4(
                fidl_fuchsia_net::Ipv4SocketAddress {
                    address: DHCP_DNS_SERVER,
                    port: DEFAULT_DNS_PORT,
                },
            )),
            source: Some(fidl_fuchsia_net_name::DnsServerSource::Dhcp(
                fidl_fuchsia_net_name::DhcpDnsServerSource {
                    source_interface: Some(client_iface.id()),
                },
            )),
        },
    ];

    // Poll LookupAdmin until we get the servers we want or after too many tries.
    let lookup_admin = client_environment
        .connect_to_service::<fidl_fuchsia_net_name::LookupAdminMarker>()
        .context("failed to connect to LookupAdmin")?;
    let mut wait_for_netmgr_fut = netmgr.wait().fuse();
    for i in 0..RETRY_COUNT {
        let () = futures::select! {
            () = fuchsia_async::Timer::new(POLL_WAIT.after_now()).fuse() => {
                Ok(())
            }
            wait_for_netmgr_res = wait_for_netmgr_fut => {
                Err(anyhow::anyhow!("the network manager unexpectedly exited with exit status = {:?}", wait_for_netmgr_res?))
            }
        }?;

        let servers: Vec<fidl_fuchsia_net_name::DnsServer_> =
            lookup_admin.get_dns_servers().await.context("Failed to get DNS servers")?;
        println!("attempt {}) Got DNS servers {:?}", i, servers);
        if servers.len() > expect.len() {
            return Err(anyhow::anyhow!(
                "Got too many servers {:?}. Expected {:?}",
                servers,
                expect
            ));
        }
        if servers.len() == expect.len() {
            assert_eq!(servers, expect);
            return Ok(());
        }
    }

    // Too many retries.
    Err(anyhow::anyhow!("Timed out waiting for DNS server configurations"))
}
