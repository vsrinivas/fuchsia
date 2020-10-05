// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Context as _;
use fuchsia_async::TimeoutExt as _;
use futures::stream::{self, StreamExt as _, TryStreamExt as _};
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
    want_addr: Option<fidl_fuchsia_net::Subnet>,
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
                    want_addr: Some(fidl_fuchsia_net::Subnet {
                        addr: fidl_ip!(192.168.0.2),
                        prefix_len: 25,
                    }),
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
                    want_addr: Some(fidl_fuchsia_net::Subnet {
                        addr: fidl_ip!(192.168.0.2),
                        prefix_len: 25,
                    }),
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
                        want_addr: Some(fidl_fuchsia_net::Subnet {
                            addr: fidl_ip!(192.168.0.2),
                            prefix_len: 25,
                        }),
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
                        want_addr: Some(fidl_fuchsia_net::Subnet {
                            addr: fidl_ip!(192.168.1.2),
                            prefix_len: 24,
                        }),
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
        let want_addr =
            addr.ok_or(anyhow::format_err!("expected address must be set for client endpoints"))?;

        let bind = || {
            use netemul::EnvironmentUdpSocket as _;

            let fidl_fuchsia_net::Subnet { addr, prefix_len: _ } = want_addr;
            let fidl_fuchsia_net_ext::IpAddress(ip_address) = addr.into();
            std::net::UdpSocket::bind_in_env(
                client_env_ref,
                std::net::SocketAddr::new(ip_address, 0),
            )
        };

        let client_interface_state = client_env_ref
            .connect_to_service::<fidl_fuchsia_net_interfaces::StateMarker>()
            .context("failed to connect to client fuchsia.net.interfaces/State")?;
        let (watcher, watcher_server) =
            ::fidl::endpoints::create_proxy::<fidl_fuchsia_net_interfaces::WatcherMarker>()?;
        let () = client_interface_state
            .get_watcher(fidl_fuchsia_net_interfaces::WatcherOptions {}, watcher_server)
            .context("failed to initialize interface watcher")?;
        let mut properties =
            fidl_fuchsia_net_interfaces_ext::InterfaceState::Unknown(interface.id());
        for _ in 0..cycles {
            // Enable the interface and assert that binding fails before the address is acquired.
            let () = interface.stop_dhcp().await.context("failed to stop DHCP")?;
            let () = interface.set_link_up(true).await.context("failed to bring link up")?;
            matches::assert_matches!(bind().await, Err(_));

            let () = interface.start_dhcp().await.context("failed to start DHCP")?;

            let addr = fidl_fuchsia_net_interfaces_ext::wait_interface_with_id(
                fidl_fuchsia_net_interfaces_ext::event_stream(watcher.clone()),
                &mut properties,
                |properties| {
                    properties.addresses.as_ref()?.iter().filter_map(|a| a.addr).find(|a| {
                        match a.addr {
                            fidl_fuchsia_net::IpAddress::Ipv4(_) => true,
                            fidl_fuchsia_net::IpAddress::Ipv6(_) => false,
                        }
                    })
                },
            )
            .on_timeout(
                // Netstack's DHCP client retries every 3 seconds. At the time of writing, dhcpd loses
                // the race here and only starts after the first request from the DHCP client, which
                // results in a 3 second toll. This test typically takes ~4.5 seconds; we apply a large
                // multiple to be safe.
                fuchsia_async::Time::after(fuchsia_zircon::Duration::from_seconds(60)),
                || Err(anyhow::anyhow!("timed out")),
            )
            .await
            .context("failed to observe DHCP acquisition on client ep {}")?;
            assert_eq!(addr, want_addr);

            // Address acquired; bind is expected to succeed.
            matches::assert_matches!(bind().await, Ok(_));

            // Set interface online signal to down and wait for address to be removed.
            let () = interface.set_link_up(false).await.context("failed to bring link down")?;

            let () = fidl_fuchsia_net_interfaces_ext::wait_interface_with_id(
                fidl_fuchsia_net_interfaces_ext::event_stream(watcher.clone()),
                &mut properties,
                |properties| {
                    properties.addresses.as_ref().map_or(Some(()), |addresses| {
                        if addresses.iter().any(|a| a.addr == Some(addr)) {
                            None
                        } else {
                            Some(())
                        }
                    })
                },
            )
            .await
            .context("failed to wait for address removal")?;
        }
        Result::Ok(())
    })
    .await?;

    Ok(())
}
