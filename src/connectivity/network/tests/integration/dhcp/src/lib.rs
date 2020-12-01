// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use anyhow::Context as _;
use fuchsia_async::TimeoutExt as _;
use futures::future::TryFutureExt as _;
use futures::stream::{self, StreamExt as _, TryStreamExt as _};
use net_declare::{fidl_ip_v4, fidl_subnet};
use netstack_testing_common::environments::{KnownServices, Netstack2, TestSandboxExt as _};
use netstack_testing_common::Result;
use netstack_testing_macros::variants_test;

const DEFAULT_SERVER_IPV4: fidl_fuchsia_net::Ipv4Address = fidl_ip_v4!(192.168.0.1);
const DEFAULT_SERVER_ADDR: fidl_fuchsia_net::Subnet = fidl_fuchsia_net::Subnet {
    addr: fidl_fuchsia_net::IpAddress::Ipv4(DEFAULT_SERVER_IPV4),
    prefix_len: 24,
};
const ALT_SERVER_IPV4: fidl_fuchsia_net::Ipv4Address = fidl_ip_v4!(192.168.1.1);
const ALT_SERVER_ADDR: fidl_fuchsia_net::Subnet = fidl_fuchsia_net::Subnet {
    addr: fidl_fuchsia_net::IpAddress::Ipv4(ALT_SERVER_IPV4),
    prefix_len: 24,
};
const DEFAULT_CLIENT_WANT_ADDR: fidl_fuchsia_net::Subnet = fidl_subnet!(192.168.0.2/25);
const ALT_CLIENT_WANT_ADDR: fidl_fuchsia_net::Subnet = fidl_subnet!(192.168.1.2/24);
const DEFAULT_SERVER_PARAMETER_ADDRESSPOOL: fidl_fuchsia_net_dhcp::Parameter =
    fidl_fuchsia_net_dhcp::Parameter::AddressPool(fidl_fuchsia_net_dhcp::AddressPool {
        network_id: Some(fidl_ip_v4!(192.168.0.0)),
        broadcast: Some(fidl_ip_v4!(192.168.0.127)),
        mask: Some(fidl_ip_v4!(255.255.255.128)),
        pool_range_start: Some(fidl_ip_v4!(192.168.0.2)),
        pool_range_stop: Some(fidl_ip_v4!(192.168.0.5)),
        ..fidl_fuchsia_net_dhcp::AddressPool::EMPTY
    });
const ALT_SERVER_PARAMETER_ADDRESSPOOL: fidl_fuchsia_net_dhcp::Parameter =
    fidl_fuchsia_net_dhcp::Parameter::AddressPool(fidl_fuchsia_net_dhcp::AddressPool {
        network_id: Some(fidl_ip_v4!(192.168.1.0)),
        broadcast: Some(fidl_ip_v4!(192.168.1.255)),
        mask: Some(fidl_ip_v4!(255.255.255.0)),
        pool_range_start: Some(fidl_ip_v4!(192.168.1.2)),
        pool_range_stop: Some(fidl_ip_v4!(192.168.1.5)),
        ..fidl_fuchsia_net_dhcp::AddressPool::EMPTY
    });
const DEFAULT_SERVER_ENDPOINT: DhcpTestEndpoint<'_> = DhcpTestEndpoint {
    name: "server-ep",
    env: DhcpTestEnv::Server,
    static_addr: Some(DEFAULT_SERVER_ADDR),
    want_addr: None,
};
const DEFAULT_CLIENT_ENDPOINT: DhcpTestEndpoint<'_> = DhcpTestEndpoint {
    name: "client-ep",
    env: DhcpTestEnv::Client,
    static_addr: None,
    want_addr: Some(DEFAULT_CLIENT_WANT_ADDR),
};
const DEFAULT_NETWORK_NAME: &'static str = "net";

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

async fn set_server_parameters(
    dhcp_server: &fidl_fuchsia_net_dhcp::Server_Proxy,
    parameters: &mut [fidl_fuchsia_net_dhcp::Parameter],
) -> Result {
    stream::iter(parameters.iter_mut())
        .map(Ok)
        .try_for_each_concurrent(None, |parameter| async move {
            dhcp_server
                .set_parameter(parameter)
                .await
                .context("failed to call dhcp/Server.SetParameter")?
                .map_err(fuchsia_zircon::Status::from_raw)
                .with_context(|| {
                    format!("dhcp/Server.SetParameter({:?}) returned error", parameter)
                })
        })
        .await
        .context("failed to set server parameters")
}

async fn client_acquires_addr(
    client_env: &netemul::TestEnvironment<'_>,
    interfaces: &[netemul::TestInterface<'_>],
    want_addr: fidl_fuchsia_net::Subnet,
    cycles: usize,
) -> Result {
    let bind = || {
        use netemul::EnvironmentUdpSocket as _;

        let fidl_fuchsia_net::Subnet { addr, prefix_len: _ } = want_addr;
        let fidl_fuchsia_net_ext::IpAddress(ip_address) = addr.into();
        std::net::UdpSocket::bind_in_env(client_env, std::net::SocketAddr::new(ip_address, 0))
    };

    let client_interface_state = client_env
        .connect_to_service::<fidl_fuchsia_net_interfaces::StateMarker>()
        .context("failed to connect to client fuchsia.net.interfaces/State")?;
    let (watcher, watcher_server) =
        ::fidl::endpoints::create_proxy::<fidl_fuchsia_net_interfaces::WatcherMarker>()?;
    let () = client_interface_state
        .get_watcher(fidl_fuchsia_net_interfaces::WatcherOptions::EMPTY, watcher_server)
        .context("failed to initialize interface watcher")?;
    for interface in interfaces.iter() {
        let mut properties =
            fidl_fuchsia_net_interfaces_ext::InterfaceState::Unknown(interface.id());
        for () in std::iter::repeat(()).take(cycles) {
            // Enable the interface and assert that binding fails before the address is acquired.
            let () = interface.stop_dhcp().await.context("failed to stop DHCP")?;
            let () = interface.set_link_up(true).await.context("failed to bring link up")?;
            matches::assert_matches!(bind().await, Err(_));

            let () = interface.start_dhcp().await.context("failed to start DHCP")?;

            let addr = fidl_fuchsia_net_interfaces_ext::wait_interface_with_id(
                fidl_fuchsia_net_interfaces_ext::event_stream(watcher.clone()),
                &mut properties,
                |fidl_fuchsia_net_interfaces::Properties {
                     id: _,
                     addresses,
                     online: _,
                     device_class: _,
                     has_default_ipv4_route: _,
                     has_default_ipv6_route: _,
                     name: _,
                     .. // TODO(fxbug.dev/63727): Remove this when we have a validated type.
                }| {
                    addresses.as_ref()?.iter().find_map(
                        |fidl_fuchsia_net_interfaces::Address { addr, .. /* TODO(fxbug.dev/63727): Remove this when we have a validated type. */ }| {

                            addr.and_then(|subnet| {
                                let fidl_fuchsia_net::Subnet { addr, prefix_len: _ } = subnet;
                                match addr {
                                    fidl_fuchsia_net::IpAddress::Ipv4(_) => Some(subnet),
                                    fidl_fuchsia_net::IpAddress::Ipv6(_) => None,
                                }
                            })
                        },
                    )
                },
            )
            .map_err(anyhow::Error::from)
            .on_timeout(
                // Netstack's DHCP client retries every 3 seconds. At the time of writing, dhcpd
                // loses the race here and only starts after the first request from the DHCP
                // client, which results in a 3 second toll. This test typically takes ~4.5
                // seconds; we apply a large multiple to be safe.
                fuchsia_async::Time::after(fuchsia_zircon::Duration::from_seconds(60)),
                || Err(anyhow::anyhow!("timed out")),
            )
            .await
            .context("failed to observe DHCP acquisition on client ep")?;
            assert_eq!(addr, want_addr);

            // Address acquired; bind is expected to succeed.
            matches::assert_matches!(bind().await, Ok(_));

            // Set interface online signal to down and wait for address to be removed.
            let () = interface.set_link_up(false).await.context("failed to bring link down")?;

            let () = fidl_fuchsia_net_interfaces_ext::wait_interface_with_id(
                fidl_fuchsia_net_interfaces_ext::event_stream(watcher.clone()),
                &mut properties,
                |fidl_fuchsia_net_interfaces::Properties {
                    id: _,
                    addresses,
                    online: _,
                    device_class: _,
                    has_default_ipv4_route: _,
                    has_default_ipv6_route: _,
                    name: _,
                    .. // TODO(fxbug.dev/63727): Remove this when we have a validated type.
                }| {
                    addresses.as_ref().map_or(Some(()), |addresses| {
                        if addresses.iter().any(
                            |fidl_fuchsia_net_interfaces::Address { addr, .. /* TODO(fxbug.dev/63727): Remove this when we have a validated type. */ }| {
                                addr == &Some(want_addr)
                            },
                        ) {
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
    }
    Ok(())
}

/// test_dhcp starts 2 netstacks, client and server, and attaches endpoints to
/// them in potentially multiple networks based on the input network
/// configuration.
///
/// DHCP servers are started on the server side, configured from the dhcp_parameters argument.
/// Notice based on the configuration, it is possible that multiple servers are started and bound
/// to different endpoints.
///
/// DHCP clients are started on each client endpoint, attempt to acquire
/// addresses through DHCP and compare them to expected address.
async fn test_dhcp<E: netemul::Endpoint>(
    name: &str,
    network_configs: &mut [DhcpTestNetwork<'_>],
    dhcp_parameters: &mut [&mut [fidl_fuchsia_net_dhcp::Parameter]],
    cycles: usize,
) -> Result {
    let sandbox = netemul::TestSandbox::new().context("failed to create sandbox")?;

    let sandbox_ref = &sandbox;
    let server_environments = stream::iter(dhcp_parameters)
        .enumerate()
        .then(|(id, parameters)| async move {
            let server_environment = sandbox_ref
                .create_netstack_environment_with::<Netstack2, _, _>(
                    format!("{}_server_{}", name, id),
                    &[KnownServices::SecureStash],
                )
                .context("failed to create server environment")?;

            let launcher =
                server_environment.get_launcher().context("failed to create launcher")?;

            let dhcpd = fuchsia_component::client::launch(
                &launcher,
                KnownServices::DhcpServer.get_url().to_string(),
                None,
            )
            .context("failed to start dhcpd")?;

            let dhcp_server = dhcpd
                .connect_to_service::<fidl_fuchsia_net_dhcp::Server_Marker>()
                .context("failed to connect to DHCP server")?;
            let () = set_server_parameters(&dhcp_server, parameters).await?;

            Result::Ok((server_environment, dhcpd))
        })
        .try_collect::<Vec<_>>()
        .await?;

    let client_environment = sandbox
        .create_netstack_environment::<Netstack2, _>(format!("{}_client", name))
        .context("failed to create client environment")?;
    let client_env_ref = &client_environment;

    let server_environments_ref = &server_environments;
    let networks = stream::iter(network_configs)
        .then(|DhcpTestNetwork { name, eps }| async move {
            let network =
                sandbox_ref.create_network(*name).await.context("failed to create network")?;
            // `network` is returned at the end of the scope so it is not
            // dropped.
            let network_ref = &network;
            let eps = stream::iter(eps.into_iter())
                .then(|DhcpTestEndpoint { name, env, static_addr, want_addr }| async move {
                    let test_environments = match env {
                        DhcpTestEnv::Client => {
                            stream::iter(std::iter::once(client_env_ref)).left_stream()
                        }
                        DhcpTestEnv::Server => {
                            stream::iter(server_environments_ref.iter().map(|(env, _dhcpd)| env))
                                .right_stream()
                        }
                    };

                    let config = match static_addr {
                        // NOTE: InterfaceAddress does not currently
                        // implement Clone, it probably will at some point
                        // as FIDL bindings evolve.
                        Some(fidl_fuchsia_net::Subnet { addr, prefix_len }) => {
                            netemul::InterfaceConfig::StaticIp(fidl_fuchsia_net::Subnet {
                                addr: *addr,
                                prefix_len: *prefix_len,
                            })
                        }
                        None => netemul::InterfaceConfig::None,
                    };

                    let name = &*name;
                    let config = &config;
                    let interfaces = test_environments
                        .enumerate()
                        .then(|(id, test_environment)| async move {
                            let name = format!("{}-{}", name, id);
                            test_environment
                                .join_network::<E, _>(network_ref, name, config)
                                .await
                                .context("failed to create endpoint")
                        })
                        .try_collect::<Vec<_>>()
                        .await?;

                    Result::Ok((env, want_addr, interfaces))
                })
                .try_collect::<Vec<_>>()
                .await?;

            Result::Ok((network, eps))
        })
        .try_collect::<Vec<_>>()
        .await?;

    let () = stream::iter(server_environments.iter())
        .map(Ok)
        .try_for_each_concurrent(None, |(_env, dhcpd)| async move {
            let dhcp_server = dhcpd
                .connect_to_service::<fidl_fuchsia_net_dhcp::Server_Marker>()
                .context("failed to connect to DHCP server")?;
            dhcp_server
                .start_serving()
                .await
                .context("failed to call dhcp/Server.StartServing")?
                .map_err(fuchsia_zircon::Status::from_raw)
                .context("dhcp/Server.StartServing returned error")
        })
        .await?;

    stream::iter(
        // Iterate over references to prevent filter from dropping endpoints.
        networks
            .iter()
            .flat_map(|(_, eps)| eps)
            .filter(|(env, _, _)| match env {
                // We only care about whether client endpoints can acquire
                // addresses through DHCP client or not.
                DhcpTestEnv::Client => true,
                _ => false,
            })
            .map(Result::Ok),
    )
    .try_for_each(|(_, want_addr, interfaces)| async move {
        let want_addr = want_addr
            .ok_or(anyhow::format_err!("expected address must be set for client endpoints"))?;
        client_acquires_addr(client_env_ref, interfaces, want_addr, cycles).await
    })
    .await
}

// TODO(fxbug.dev/62554): the tests below are awful and contain lots of repetition. Reduce
// repetition.

#[variants_test]
async fn acquire_dhcp<E: netemul::Endpoint>(name: &str) -> Result {
    test_dhcp::<E>(
        name,
        &mut [DhcpTestNetwork {
            name: DEFAULT_NETWORK_NAME,
            eps: &mut [DEFAULT_SERVER_ENDPOINT, DEFAULT_CLIENT_ENDPOINT],
        }],
        &mut [&mut [
            fidl_fuchsia_net_dhcp::Parameter::IpAddrs(vec![DEFAULT_SERVER_IPV4]),
            DEFAULT_SERVER_PARAMETER_ADDRESSPOOL,
        ]],
        3,
    )
    .await
}

#[variants_test]
async fn acquire_dhcp_with_dhcpd_bound_device<E: netemul::Endpoint>(name: &str) -> Result {
    test_dhcp::<E>(
        name,
        &mut [DhcpTestNetwork {
            name: DEFAULT_NETWORK_NAME,
            eps: &mut [DEFAULT_SERVER_ENDPOINT, DEFAULT_CLIENT_ENDPOINT],
        }],
        &mut [&mut [
            fidl_fuchsia_net_dhcp::Parameter::IpAddrs(vec![DEFAULT_SERVER_IPV4]),
            DEFAULT_SERVER_PARAMETER_ADDRESSPOOL,
            fidl_fuchsia_net_dhcp::Parameter::BoundDeviceNames(vec!["eth2".to_string()]),
        ]],
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
                        static_addr: Some(DEFAULT_SERVER_ADDR),
                        want_addr: None,
                    },
                    DhcpTestEndpoint {
                        name: "client-ep1",
                        env: DhcpTestEnv::Client,
                        static_addr: None,
                        want_addr: Some(DEFAULT_CLIENT_WANT_ADDR),
                    },
                ],
            },
            DhcpTestNetwork {
                name: "net2",
                eps: &mut [
                    DhcpTestEndpoint {
                        name: "server-ep2",
                        env: DhcpTestEnv::Server,
                        static_addr: Some(ALT_SERVER_ADDR),
                        want_addr: None,
                    },
                    DhcpTestEndpoint {
                        name: "client-ep2",
                        env: DhcpTestEnv::Client,
                        static_addr: None,
                        want_addr: Some(ALT_CLIENT_WANT_ADDR),
                    },
                ],
            },
        ],
        &mut [
            &mut [
                fidl_fuchsia_net_dhcp::Parameter::IpAddrs(vec![DEFAULT_SERVER_IPV4]),
                DEFAULT_SERVER_PARAMETER_ADDRESSPOOL,
                fidl_fuchsia_net_dhcp::Parameter::BoundDeviceNames(vec!["eth2".to_string()]),
            ],
            &mut [
                fidl_fuchsia_net_dhcp::Parameter::IpAddrs(vec![ALT_SERVER_IPV4]),
                ALT_SERVER_PARAMETER_ADDRESSPOOL,
                fidl_fuchsia_net_dhcp::Parameter::BoundDeviceNames(vec!["eth3".to_string()]),
            ],
        ],
        1,
    )
    .await
}

// This test guards against regression for the issue found in fxbug.dev/62989. The test attempts to
// create an inconsistent state on the dhcp server by allowing the server to complete a transaction
// with a client, thereby creating a record of a lease. The server is then restarted; if the linked
// issue has not been fixed, then the server will inadvertently erase its configuration parameters
// from persistent storage, which will lead to an inconsistent server state on the next restart.
// Finally, the server is restarted one more time, and then its clear_leases() function is
// triggered, which will cause a panic if the server is in an inconsistent state.
#[variants_test]
async fn acquire_dhcp_server_restart_consistent_state<E: netemul::Endpoint>(name: &str) -> Result {
    let sandbox = netemul::TestSandbox::new().context("failed to create sandbox")?;

    let server_env = sandbox
        .create_netstack_environment_with::<Netstack2, _, _>(
            format!("{}_server", name),
            &[KnownServices::SecureStash],
        )
        .context("failed to create server environment")?;

    let client_env = sandbox
        .create_netstack_environment::<Netstack2, _>(format!("{}_client", name))
        .context("failed to create client environemnt")?;

    let network = sandbox.create_network(name).await.context("failed to create network")?;
    let _server_ep = server_env
        .join_network::<E, _>(
            &network,
            "server-ep",
            &netemul::InterfaceConfig::StaticIp(DEFAULT_SERVER_ADDR),
        )
        .await
        .context("failed to create server network endpoint")?;
    let client_ep = client_env
        .join_network::<E, _>(&network, "client-ep", &netemul::InterfaceConfig::None)
        .await
        .context("failed to create client network endpoint")?;

    // Complete initial DHCP transaction in order to store a lease record in the server's
    // persistent storage.
    {
        let mut dhcpd = fuchsia_component::client::launch(
            &server_env.get_launcher().context("failed to create launcher")?,
            KnownServices::DhcpServer.get_url().to_string(),
            None,
        )
        .context("failed to start dhcpd")?;
        let dhcp_server = dhcpd
            .connect_to_service::<fidl_fuchsia_net_dhcp::Server_Marker>()
            .context("failed to connect to DHCP server")?;
        let () = set_server_parameters(
            &dhcp_server,
            &mut [
                fidl_fuchsia_net_dhcp::Parameter::IpAddrs(vec![DEFAULT_SERVER_IPV4]),
                DEFAULT_SERVER_PARAMETER_ADDRESSPOOL,
            ],
        )
        .await?;

        let () = dhcp_server
            .start_serving()
            .await
            .context("failed to call dhcp/Server.StartServing")?
            .map_err(fuchsia_zircon::Status::from_raw)
            .context("dhcp/Server.StartServing returned error")?;

        let () = client_acquires_addr(&client_env, &[client_ep], DEFAULT_CLIENT_WANT_ADDR, 1)
            .await
            .context("client failed to acquire address")?;

        let () =
            dhcp_server.stop_serving().await.context("failed to call dhcp/Server.StopServing")?;
        let () = dhcpd.kill().context("failed to kill dhcpd component")?;
        assert_eq!(
            dhcpd.wait().await.context("failed to await dhcpd component exit")?.code(),
            fuchsia_zircon::sys::ZX_TASK_RETCODE_SYSCALL_KILL
        );
    }

    // Restart the server in an attempt to force the server's persistent storage into an
    // inconsistent state whereby the addresses leased to clients do not agree with the contents of
    // the server's address pool.
    {
        let mut dhcpd = fuchsia_component::client::launch(
            &server_env.get_launcher().context("failed to create launcher")?,
            KnownServices::DhcpServer.get_url().to_string(),
            None,
        )
        .context("failed to start dhcpd")?;
        let dhcp_server = dhcpd
            .connect_to_service::<fidl_fuchsia_net_dhcp::Server_Marker>()
            .context("failed to connect to DHCP server")?;
        let () = dhcp_server
            .start_serving()
            .await
            .context("failed to call dhcp/Server.StartServing")?
            .map_err(fuchsia_zircon::Status::from_raw)
            .context("dhcp/Server.StartServing returned error")?;
        let () =
            dhcp_server.stop_serving().await.context("failed to call dhcp/Server.StopServing")?;
        let () = dhcpd.kill().context("failed to kill dhcpd component")?;
        assert_eq!(
            dhcpd.wait().await.context("failed to await dhcpd component exit")?.code(),
            fuchsia_zircon::sys::ZX_TASK_RETCODE_SYSCALL_KILL
        );
    }

    // Restart the server again in order to load the inconsistent state into the server's runtime
    // representation. Call clear_leases() to trigger a panic resulting from inconsistent state,
    // provided that the issue motivating this test is unfixed/regressed.
    {
        let mut dhcpd = fuchsia_component::client::launch(
            &server_env.get_launcher().context("failed to create launcher")?,
            KnownServices::DhcpServer.get_url().to_string(),
            None,
        )
        .context("failed to start dhcpd")?;
        let dhcp_server = dhcpd
            .connect_to_service::<fidl_fuchsia_net_dhcp::Server_Marker>()
            .context("failed to connect to DHCP server")?;
        let () = dhcp_server
            .start_serving()
            .await
            .context("failed to call dhcp/Server.StartServing")?
            .map_err(fuchsia_zircon::Status::from_raw)
            .context("dhcp/Server.StartServing returned error")?;
        let () =
            dhcp_server.stop_serving().await.context("failed to call dhcp/Server.StopServing")?;
        let () = dhcp_server
            .clear_leases()
            .await
            .context("failed to call dhcp/Server.ClearLeases")?
            .map_err(fuchsia_zircon::Status::from_raw)
            .context("dhcp/Server.ClearLeases returned error")?;

        let () = dhcpd.kill().context("failed to kill dhcpd component")?;
        assert_eq!(
            dhcpd.wait().await.context("failed to await dhcpd component exit")?.code(),
            fuchsia_zircon::sys::ZX_TASK_RETCODE_SYSCALL_KILL
        );
    }

    Ok(())
}
