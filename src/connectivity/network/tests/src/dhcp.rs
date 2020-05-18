// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_net_ext::{ip_addr, ipv4_addr};

use anyhow::Context as _;
use futures::stream::{self, StreamExt, TryStreamExt};

use crate::environments::*;
use crate::Result;

/// Endpoints in DHCP tests are either
/// 1. attached to the server stack, which will have DHCP servers serving on them.
/// 2. or attached to the client stack, which will have DHCP clients started on
///    them to request addresses.
enum DhcpTestEnv {
    Client,
    Server,
}

struct DhcpTestEndpoint<'a> {
    name: &'a str,
    env: DhcpTestEnv,
    /// static_addr is the static address configured on the endpoint before any
    /// server or client is started.
    static_addr: Option<fidl_fuchsia_net_stack::InterfaceAddress>,
    /// want_addr is the address expected after a successfull address acquisition
    /// from a DHCP client.
    want_addr: Option<(fidl_fuchsia_net::IpAddress, fidl_fuchsia_net::IpAddress)>,
}

/// A network can have multiple endpoints. Each endpoint can be attached to a
/// different stack.
struct DhcpTestNetwork<'a> {
    name: &'a str,
    eps: &'a mut [DhcpTestEndpoint<'a>],
}

// TODO(tamird): could this be done with a single stack and bridged interfaces?
#[fuchsia_async::run_singlethreaded(test)]
async fn acquire_dhcp() -> Result {
    test_dhcp(
        "acquire_dhcp",
        &mut [DhcpTestNetwork {
            name: "net",
            eps: &mut [
                DhcpTestEndpoint {
                    name: "server-ep",
                    env: DhcpTestEnv::Server,
                    static_addr: Some(fidl_fuchsia_net_stack::InterfaceAddress {
                        ip_address: ip_addr![192, 168, 0, 1],
                        prefix_len: 24,
                    }),
                    want_addr: None,
                },
                DhcpTestEndpoint {
                    name: "client-ep",
                    env: DhcpTestEnv::Client,
                    static_addr: None,
                    want_addr: Some((ip_addr![192, 168, 0, 2], ip_addr![255, 255, 255, 128])),
                },
            ],
        }],
        &["/pkg/data/test_config.json"],
    )
    .await
}

#[fuchsia_async::run_singlethreaded(test)]
async fn acquire_dhcp_with_dhcpd_bound_device() -> Result {
    test_dhcp(
        "acquire_dhcp_with_dhcpd_bound_device",
        &mut [DhcpTestNetwork {
            name: "net",
            eps: &mut [
                DhcpTestEndpoint {
                    name: "server-ep",
                    env: DhcpTestEnv::Server,
                    static_addr: Some(fidl_fuchsia_net_stack::InterfaceAddress {
                        ip_address: ip_addr![192, 168, 0, 1],
                        prefix_len: 24,
                    }),
                    want_addr: None,
                },
                DhcpTestEndpoint {
                    name: "client-ep",
                    env: DhcpTestEnv::Client,
                    static_addr: None,
                    want_addr: Some((ip_addr![192, 168, 0, 2], ip_addr![255, 255, 255, 128])),
                },
            ],
        }],
        &["/pkg/data/bound_device_test_config_eth2.json"],
    )
    .await
}

#[fuchsia_async::run_singlethreaded(test)]
async fn acquire_dhcp_with_multiple_network() -> Result {
    test_dhcp(
        "acquire_dhcp_with_dhcpd_bound_device",
        &mut [
            DhcpTestNetwork {
                name: "net1",
                eps: &mut [
                    DhcpTestEndpoint {
                        name: "server-ep1",
                        env: DhcpTestEnv::Server,
                        static_addr: Some(fidl_fuchsia_net_stack::InterfaceAddress {
                            ip_address: ip_addr![192, 168, 0, 1],
                            prefix_len: 24,
                        }),
                        want_addr: None,
                    },
                    DhcpTestEndpoint {
                        name: "client-ep1",
                        env: DhcpTestEnv::Client,
                        static_addr: None,
                        want_addr: Some((ip_addr![192, 168, 0, 2], ip_addr![255, 255, 255, 128])),
                    },
                ],
            },
            DhcpTestNetwork {
                name: "net2",
                eps: &mut [
                    DhcpTestEndpoint {
                        name: "server-ep2",
                        env: DhcpTestEnv::Server,
                        static_addr: Some(fidl_fuchsia_net_stack::InterfaceAddress {
                            ip_address: ip_addr![192, 168, 1, 1],
                            prefix_len: 24,
                        }),
                        want_addr: None,
                    },
                    DhcpTestEndpoint {
                        name: "client-ep2",
                        env: DhcpTestEnv::Client,
                        static_addr: None,
                        want_addr: Some((ip_addr![192, 168, 1, 2], ip_addr![255, 255, 255, 0])),
                    },
                ],
            },
        ],
        &[
            "/pkg/data/bound_device_test_config_eth2.json",
            "/pkg/data/bound_device_test_config_eth3.json",
        ],
    )
    .await
}

/// test_dhcp starts 2 netstacks, client and server, and attaches endpoints to
/// them in potentially multiple networks based on the input network
/// configuration.
///
/// DHCP servers are started on the server side, based on the dhcpd config files
/// from the input paths. Notice based on the configuration, it is possible that
/// multiple servers are started and bound to different endpoints.
///
/// DHCP clients are started on each client endpoint, attempt to acquire
/// addresses through DHCP and compare them to expected address.
async fn test_dhcp(
    name: &str,
    network_configs: &mut [DhcpTestNetwork<'_>],
    dhcpd_config_paths: &[&str],
) -> Result {
    let sandbox = TestSandbox::new().context("failed to create sandbox")?;

    let server_environment = sandbox
        .create_netstack_environment_with::<Netstack2, _, _>(
            format!("{}_server", name),
            &[KnownServices::SecureStash],
        )
        .context("failed to create server environment")?;
    let server_env_ref = &server_environment;

    let client_environment = sandbox
        .create_netstack_environment::<Netstack2, _>(format!("{}_client", name))
        .context("failed to create client environment")?;
    let client_env_ref = &client_environment;

    let sandbox_ref = &sandbox;

    let networks = stream::iter(network_configs)
        .then(|DhcpTestNetwork { name, eps }| async move {
            let network =
                sandbox_ref.create_network(*name).await.context("failed to create network")?;
            // `network` is returned at the end of the scope so it is not
            // dropped.
            let network_ref = &network;
            let eps = stream::iter(eps.into_iter())
                .then(|DhcpTestEndpoint { name, env, static_addr, want_addr }| async move {
                    let test_environment = match env {
                        DhcpTestEnv::Client => client_env_ref,
                        DhcpTestEnv::Server => server_env_ref,
                    };

                    let config = match static_addr {
                        Some(addr) => {
                            // NOTE: InterfaceAddress does not currently
                            // implement Clone, it probably will at some point
                            // as FIDL bindings evolve.
                            InterfaceConfig::StaticIp(fidl_fuchsia_net_stack::InterfaceAddress {
                                ip_address: addr.ip_address,
                                prefix_len: addr.prefix_len,
                            })
                        }
                        None => InterfaceConfig::None,
                    };

                    let interface = test_environment
                        .join_network(network_ref, *name, config)
                        .await
                        .context("failed to create endpoint")?;

                    Result::Ok((env, test_environment, want_addr, interface))
                })
                .try_collect::<Vec<_>>()
                .await?;

            Result::Ok((network, eps))
        })
        .try_collect::<Vec<_>>()
        .await?;

    let launcher = server_environment.get_launcher().context("failed to create launcher")?;
    let _dhcpds = dhcpd_config_paths
        .into_iter()
        .map(|config_path| {
            fuchsia_component::client::launch(
                &launcher,
                KnownServices::DhcpServer.get_url().to_string(),
                Some(vec![String::from("--config"), config_path.to_string()]),
            )
            .context("failed to start dhcpd")
        })
        .collect::<Result<Vec<_>>>()?;

    let () = stream::iter(
        // Iterate over references to prevent filter from dropping endpoints.
        networks
            .iter()
            .flat_map(|(_, eps)| eps)
            .filter(|(env, _, _, _)| match env {
                // We only care about whether client endpoints can acquire
                // addresses through DHCP client or not.
                DhcpTestEnv::Client => true,
                _ => false,
            })
            .map(Result::Ok),
    )
    .try_for_each(|(_, _, addr, interface)| async move {
        let (want_addr, want_netmask) =
            addr.ok_or(anyhow::format_err!("expected address must be set for client endpoints"))?;

        interface.start_dhcp().await.context("Failed to start DHCP")?;

        let client_netstack = client_env_ref
            .connect_to_service::<fidl_fuchsia_netstack::NetstackMarker>()
            .context("failed to connect to client netstack")?;

        let mut address_change_stream = futures::TryStreamExt::try_filter_map(
            client_netstack.take_event_stream(),
            |fidl_fuchsia_netstack::NetstackEvent::OnInterfacesChanged { interfaces }| {
                futures::future::ok(
                    interfaces.into_iter().find(|i| i.id as u64 == interface.id()).and_then(
                        |fidl_fuchsia_netstack::NetInterface { addr, netmask, .. }| {
                            let ipaddr = addr;
                            match ipaddr {
                                fidl_fuchsia_net::IpAddress::Ipv4(
                                    fidl_fuchsia_net::Ipv4Address { addr },
                                ) => {
                                    if addr == std::net::Ipv4Addr::UNSPECIFIED.octets() {
                                        None
                                    } else {
                                        Some((ipaddr, netmask))
                                    }
                                }
                                fidl_fuchsia_net::IpAddress::Ipv6(
                                    fidl_fuchsia_net::Ipv6Address { .. },
                                ) => None,
                            }
                        },
                    ),
                )
            },
        );
        let address_change = futures::StreamExt::next(&mut address_change_stream);
        let address_change = fuchsia_async::TimeoutExt::on_timeout(
            address_change,
            // Netstack's DHCP client retries every 3 seconds. At the time of writing, dhcpd loses
            // the race here and only starts after the first request from the DHCP client, which
            // results in a 3 second toll. This test typically takes ~4.5 seconds; we apply a large
            // multiple to be safe.
            fuchsia_async::Time::after(fuchsia_zircon::Duration::from_seconds(60)),
            || None,
        );
        let (addr, netmask) = address_change
            .await
            .ok_or(anyhow::format_err!("failed to observe DHCP acquisition on client ep {}", name))?
            .context(format!("failed to observe DHCP acquisition on client ep {}", name))?;
        assert_eq!(addr, want_addr);
        assert_eq!(netmask, want_netmask);
        Result::Ok(())
    })
    .await?;

    Ok(())
}

/// Tests that Netstack exposes DNS servers discovered through DHCP and
/// `dns_resolver` loads them into its name servers configuration.
#[fuchsia_async::run_singlethreaded(test)]
async fn test_discovered_dns() -> Result {
    const SERVER_IP: fidl_fuchsia_net::IpAddress = ip_addr![192, 168, 0, 1];
    /// DNS server served by DHCP.
    const DHCP_DNS_SERVER: fidl_fuchsia_net::Ipv4Address = ipv4_addr![123, 12, 34, 56];
    /// Static DNS server given directly to `LookupAdmin`.
    const STATIC_DNS_SERVER: fidl_fuchsia_net::Ipv4Address = ipv4_addr![123, 12, 34, 99];

    /// Maximum number of times we'll poll `LookupAdmin` to check DNS configuration
    /// succeeded.
    const RETRY_COUNT: u64 = 60;
    /// Duration to sleep between polls.
    const POLL_WAIT: fuchsia_zircon::Duration = fuchsia_zircon::Duration::from_seconds(1);

    const DEFAULT_DNS_PORT: u16 = 53;

    let name = "test_discovered_dns";
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
                    "/pkg/data/test_config.json",
                ]),
                KnownServices::SecureStash.into_launch_service(),
            ],
        )
        .context("failed to create server environment")?;

    let client_environment = sandbox
        .create_netstack_environment_with::<Netstack2, _, _>(
            format!("{}_client", name),
            &[KnownServices::LoopkupAdmin],
        )
        .context("failed to create client environment")?;

    let _server_iface = server_environment
        .join_network(
            &network,
            "server-ep",
            InterfaceConfig::StaticIp(fidl_fuchsia_net_stack::InterfaceAddress {
                ip_address: SERVER_IP,
                prefix_len: 24,
            }),
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

    // Connect to lookup admin and set up the default servers.
    let lookup_admin = client_environment
        .connect_to_service::<fidl_fuchsia_net_name::LookupAdminMarker>()
        .context("failed to connect to LookupAdmin")?;

    lookup_admin
        .set_default_dns_servers(
            &mut vec![fidl_fuchsia_net::IpAddress::Ipv4(STATIC_DNS_SERVER)].iter_mut(),
        )
        .await
        .context("Failed to set default DNS servers")?
        .map_err(fuchsia_zircon::Status::from_raw)
        .context("LookupAdmin.SetDefaultDnsServers returned error")?;

    // Start networking on client environment.
    let client_iface = client_environment
        .join_network(&network, "client-ep", InterfaceConfig::Dhcp)
        .await
        .context("failed to configure client networking")?;

    // The list of servers we expect to retrieve from LookupAdmin.
    let expect = vec![
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
        fidl_fuchsia_net_name::DnsServer_ {
            address: Some(fidl_fuchsia_net::SocketAddress::Ipv4(
                fidl_fuchsia_net::Ipv4SocketAddress {
                    address: STATIC_DNS_SERVER,
                    port: DEFAULT_DNS_PORT,
                },
            )),
            source: Some(fidl_fuchsia_net_name::DnsServerSource::StaticSource(
                fidl_fuchsia_net_name::StaticDnsServerSource {},
            )),
        },
    ];

    // Poll LookupAdmin until we get the servers we want or after too many tries.
    for i in 0..RETRY_COUNT {
        let () = fuchsia_async::Timer::new(fuchsia_async::Time::after(POLL_WAIT)).await;
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
