// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Context as _;
use futures::stream::{self, StreamExt, TryStreamExt};
use net_declare::{fidl_ip, fidl_subnet};
use netstack_testing_macros::variants_test;

use crate::environments::{KnownServices, Netstack2, TestSandboxExt as _};
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
    static_addr: Option<fidl_fuchsia_net::Subnet>,
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
#[variants_test]
async fn acquire_dhcp<E: netemul::Endpoint>(name: &str) -> Result {
    test_dhcp::<E>(
        name,
        &mut [DhcpTestNetwork {
            name: "net",
            eps: &mut [
                DhcpTestEndpoint {
                    name: "server-ep",
                    env: DhcpTestEnv::Server,
                    static_addr: Some(fidl_subnet!(192.168.0.1/24)),
                    want_addr: None,
                },
                DhcpTestEndpoint {
                    name: "client-ep",
                    env: DhcpTestEnv::Client,
                    static_addr: None,
                    want_addr: Some((fidl_ip!(192.168.0.2), fidl_ip!(255.255.255.128))),
                },
            ],
        }],
        &["/config/data/dhcpd-testing/test_config.json"],
        3,
    )
    .await
}

#[variants_test]
async fn acquire_dhcp_with_dhcpd_bound_device<E: netemul::Endpoint>(name: &str) -> Result {
    test_dhcp::<E>(
        name,
        &mut [DhcpTestNetwork {
            name: "net",
            eps: &mut [
                DhcpTestEndpoint {
                    name: "server-ep",
                    env: DhcpTestEnv::Server,
                    static_addr: Some(fidl_subnet!(192.168.0.1/24)),
                    want_addr: None,
                },
                DhcpTestEndpoint {
                    name: "client-ep",
                    env: DhcpTestEnv::Client,
                    static_addr: None,
                    want_addr: Some((fidl_ip!(192.168.0.2), fidl_ip!(255.255.255.128))),
                },
            ],
        }],
        &["/config/data/dhcpd-testing/bound_device_test_config_eth2.json"],
        1,
    )
    .await
}

#[variants_test]
async fn acquire_dhcp_with_multiple_network<E: netemul::Endpoint>(name: &str) -> Result {
    test_dhcp::<E>(
        name,
        &mut [
            DhcpTestNetwork {
                name: "net1",
                eps: &mut [
                    DhcpTestEndpoint {
                        name: "server-ep1",
                        env: DhcpTestEnv::Server,
                        static_addr: Some(fidl_subnet!(192.168.0.1/24)),
                        want_addr: None,
                    },
                    DhcpTestEndpoint {
                        name: "client-ep1",
                        env: DhcpTestEnv::Client,
                        static_addr: None,
                        want_addr: Some((fidl_ip!(192.168.0.2), fidl_ip!(255.255.255.128))),
                    },
                ],
            },
            DhcpTestNetwork {
                name: "net2",
                eps: &mut [
                    DhcpTestEndpoint {
                        name: "server-ep2",
                        env: DhcpTestEnv::Server,
                        static_addr: Some(fidl_subnet!(192.168.1.1/24)),
                        want_addr: None,
                    },
                    DhcpTestEndpoint {
                        name: "client-ep2",
                        env: DhcpTestEnv::Client,
                        static_addr: None,
                        want_addr: Some((fidl_ip!(192.168.1.2), fidl_ip!(255.255.255.0))),
                    },
                ],
            },
        ],
        &[
            "/config/data/dhcpd-testing/bound_device_test_config_eth2.json",
            "/config/data/dhcpd-testing/bound_device_test_config_eth3.json",
        ],
        1,
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
async fn test_dhcp<E: netemul::Endpoint>(
    name: &str,
    network_configs: &mut [DhcpTestNetwork<'_>],
    dhcpd_config_paths: &[&str],
    cycles: u32,
) -> Result {
    let sandbox = netemul::TestSandbox::new().context("failed to create sandbox")?;

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
                            netemul::InterfaceConfig::StaticIp(fidl_fuchsia_net::Subnet {
                                addr: addr.addr,
                                prefix_len: addr.prefix_len,
                            })
                        }
                        None => netemul::InterfaceConfig::None,
                    };

                    let interface = test_environment
                        .join_network::<E, _>(network_ref, *name, config)
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

        for _ in 0..cycles {
            let () = interface.set_link_up(true).await.context("Failed to bring link up")?;
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
                .ok_or(anyhow::format_err!(
                    "failed to observe DHCP acquisition on client ep {}",
                    name
                ))?
                .context(format!("failed to observe DHCP acquisition on client ep {}", name))?;
            assert_eq!(addr, want_addr);
            assert_eq!(netmask, want_netmask);
            // Drop so we can listen on the stream again.
            std::mem::drop(address_change_stream);

            // Set interface online signal to down and wait for address to be removed.
            let () = interface.set_link_up(false).await.context("Failed to bring link up")?;
            let _ifaces = futures::TryStreamExt::try_filter(
                client_netstack.take_event_stream(),
                |fidl_fuchsia_netstack::NetstackEvent::OnInterfacesChanged { interfaces }| {
                    futures::future::ready(
                        interfaces
                            .into_iter()
                            .find(|i| i.id as u64 == interface.id())
                            .map(|i| i.addr == fidl_ip!(0.0.0.0))
                            .unwrap_or(false),
                    )
                },
            )
            .next()
            .await
            .ok_or_else(|| anyhow::anyhow!("event stream ended unexpectedly"))?;
        }
        Result::Ok(())
    })
    .await?;

    Ok(())
}
