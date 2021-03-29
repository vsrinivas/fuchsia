// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use anyhow::Context as _;
use fuchsia_async::TimeoutExt as _;
use futures::future::TryFutureExt as _;
use futures::stream::{self, StreamExt as _, TryStreamExt as _};
use net_declare::{fidl_ip_v4, fidl_mac, fidl_subnet};
use netstack_testing_common::environments::{KnownServices, Netstack2, TestSandboxExt as _};
use netstack_testing_common::Result;
use netstack_testing_macros::variants_test;

const DEFAULT_SERVER_IPV4: fidl_fuchsia_net::Ipv4Address = fidl_ip_v4!("192.168.0.1");
const DEFAULT_SERVER_ADDR: fidl_fuchsia_net::Subnet = fidl_fuchsia_net::Subnet {
    addr: fidl_fuchsia_net::IpAddress::Ipv4(DEFAULT_SERVER_IPV4),
    prefix_len: 24,
};
const ALT_SERVER_IPV4: fidl_fuchsia_net::Ipv4Address = fidl_ip_v4!("192.168.1.1");
const ALT_SERVER_ADDR: fidl_fuchsia_net::Subnet = fidl_fuchsia_net::Subnet {
    addr: fidl_fuchsia_net::IpAddress::Ipv4(ALT_SERVER_IPV4),
    prefix_len: 24,
};
const DEFAULT_CLIENT_WANT_ADDR: fidl_fuchsia_net::Subnet = fidl_subnet!("192.168.0.2/25");
const ALT_CLIENT_WANT_ADDR: fidl_fuchsia_net::Subnet = fidl_subnet!("192.168.1.2/24");
const DEFAULT_SERVER_PARAMETER_ADDRESSPOOL: fidl_fuchsia_net_dhcp::Parameter =
    fidl_fuchsia_net_dhcp::Parameter::AddressPool(fidl_fuchsia_net_dhcp::AddressPool {
        network_id: Some(fidl_ip_v4!("192.168.0.0")),
        broadcast: Some(fidl_ip_v4!("192.168.0.127")),
        mask: Some(fidl_ip_v4!("255.255.255.128")),
        pool_range_start: Some(fidl_ip_v4!("192.168.0.2")),
        pool_range_stop: Some(fidl_ip_v4!("192.168.0.5")),
        ..fidl_fuchsia_net_dhcp::AddressPool::EMPTY
    });
const ALT_SERVER_PARAMETER_ADDRESSPOOL: fidl_fuchsia_net_dhcp::Parameter =
    fidl_fuchsia_net_dhcp::Parameter::AddressPool(fidl_fuchsia_net_dhcp::AddressPool {
        network_id: Some(fidl_ip_v4!("192.168.1.0")),
        broadcast: Some(fidl_ip_v4!("192.168.1.255")),
        mask: Some(fidl_ip_v4!("255.255.255.0")),
        pool_range_start: Some(fidl_ip_v4!("192.168.1.2")),
        pool_range_stop: Some(fidl_ip_v4!("192.168.1.5")),
        ..fidl_fuchsia_net_dhcp::AddressPool::EMPTY
    });
const DEFAULT_NETWORK_NAME: &'static str = "net";

fn default_server_endpoint() -> DhcpTestEndpoint<'static> {
    DhcpTestEndpoint {
        name: "server-ep",
        env: DhcpTestEnv::Server,
        static_addrs: vec![DEFAULT_SERVER_ADDR],
        want_addr: None,
    }
}

fn default_client_endpoint() -> DhcpTestEndpoint<'static> {
    DhcpTestEndpoint {
        name: "client-ep",
        env: DhcpTestEnv::Client,
        static_addrs: Vec::new(),
        want_addr: Some(DEFAULT_CLIENT_WANT_ADDR),
    }
}

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
    /// static_addrs holds the static addresses configured on the endpoint
    /// before any server or client is started.
    static_addrs: Vec<fidl_fuchsia_net::Subnet>,
    /// want_addr is the address expected after a successful address acquisition
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
    client_renews: bool,
) -> Result {
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
            matches::assert_matches!(
                bind(&client_env, want_addr).await,
                Err(e @ anyhow::Error {..})
                    if e.downcast_ref::<std::io::Error>()
                        .ok_or(anyhow::anyhow!("bind() did not return std::io::Error"))?
                        .raw_os_error() == Some(libc::EADDRNOTAVAIL)
            );

            let () = interface.start_dhcp().await.context("failed to start DHCP")?;

            let () =
                assert_interface_assigned_addr(client_env, want_addr, &watcher, &mut properties)
                    .await?;

            // If test covers renewal behavior, check that a subsequent interface changed event
            // occurs where the client retains its address, i.e. that it successfully renewed its
            // lease. It will take lease_length/2 duration for the client to renew its address
            // and trigger the subsequent interface changed event.
            if client_renews {
                let () = assert_interface_assigned_addr(
                    client_env,
                    want_addr,
                    &watcher,
                    &mut properties,
                )
                .await?;
            }
            // Set interface online signal to down and wait for address to be removed.
            let () = interface.set_link_up(false).await.context("failed to bring link down")?;

            let () = fidl_fuchsia_net_interfaces_ext::wait_interface_with_id(
                fidl_fuchsia_net_interfaces_ext::event_stream(watcher.clone()),
                &mut properties,
                |fidl_fuchsia_net_interfaces_ext::Properties {
                     id: _,
                     addresses,
                     online: _,
                     device_class: _,
                     has_default_ipv4_route: _,
                     has_default_ipv6_route: _,
                     name: _,
                 }| {
                    if addresses
                        .iter()
                        .any(|&fidl_fuchsia_net_interfaces_ext::Address { addr }| addr == want_addr)
                    {
                        None
                    } else {
                        Some(())
                    }
                },
            )
            .await
            .context("failed to wait for address removal")?;
        }
    }
    Ok(())
}

async fn assert_interface_assigned_addr(
    client_env: &netemul::TestEnvironment<'_>,
    want_addr: fidl_fuchsia_net::Subnet,
    watcher: &fidl_fuchsia_net_interfaces::WatcherProxy,
    mut properties: &mut fidl_fuchsia_net_interfaces_ext::InterfaceState,
) -> Result {
    let addr = fidl_fuchsia_net_interfaces_ext::wait_interface_with_id(
        fidl_fuchsia_net_interfaces_ext::event_stream(watcher.clone()),
        &mut properties,
        |fidl_fuchsia_net_interfaces_ext::Properties {
             id: _,
             addresses,
             online: _,
             device_class: _,
             has_default_ipv4_route: _,
             has_default_ipv6_route: _,
             name: _,
         }| {
            addresses.iter().find_map(
                |&fidl_fuchsia_net_interfaces_ext::Address { addr: subnet }| {
                    let fidl_fuchsia_net::Subnet { addr, prefix_len: _ } = subnet;
                    match addr {
                        fidl_fuchsia_net::IpAddress::Ipv4(_) => Some(subnet),
                        fidl_fuchsia_net::IpAddress::Ipv6(_) => None,
                    }
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
    let _: std::net::UdpSocket = bind(&client_env, want_addr).await?;
    Ok(())
}

fn bind<'a>(
    client_env: &'a netemul::TestEnvironment<'_>,
    fidl_fuchsia_net::Subnet { addr, prefix_len: _ }: fidl_fuchsia_net::Subnet,
) -> impl futures::Future<Output = Result<std::net::UdpSocket>> + 'a {
    use netemul::EnvironmentUdpSocket as _;

    let fidl_fuchsia_net_ext::IpAddress(ip_address) = addr.into();
    std::net::UdpSocket::bind_in_env(client_env, std::net::SocketAddr::new(ip_address, 0))
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
///
/// The DHCP client's renewal path is tested with the `client_renews` flag. Since a client only
/// renews after lease_length/2 seconds has passed, `dhcp_parameters` should include a short lease
/// length when `client_renews` is set.
async fn test_dhcp<E: netemul::Endpoint>(
    name: &str,
    network_configs: &mut [DhcpTestNetwork<'_>],
    dhcp_parameters: &mut [&mut [fidl_fuchsia_net_dhcp::Parameter]],
    cycles: usize,
    client_renews: bool,
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
                .then(|DhcpTestEndpoint { name, env, static_addrs, want_addr }| async move {
                    let test_environments = match env {
                        DhcpTestEnv::Client => {
                            stream::iter(std::iter::once(client_env_ref)).left_stream()
                        }
                        DhcpTestEnv::Server => {
                            stream::iter(server_environments_ref.iter().map(|(env, _dhcpd)| env))
                                .right_stream()
                        }
                    };

                    let name = &*name;
                    let interfaces = test_environments
                        .enumerate()
                        .zip(futures::stream::repeat(static_addrs.clone()))
                        .then(|((id, test_environment), static_addrs)| async move {
                            let name = format!("{}-{}", name, id);
                            let iface = test_environment
                                .join_network::<E, _>(
                                    network_ref,
                                    name,
                                    &netemul::InterfaceConfig::None,
                                )
                                .await
                                .context("failed to create endpoint")?;
                            for a in static_addrs.into_iter() {
                                let () = iface
                                    .add_ip_addr(a)
                                    .await
                                    .with_context(|| format!("failed to add address {:?}", a))?;
                            }

                            Ok::<_, anyhow::Error>(iface)
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
        client_acquires_addr(client_env_ref, interfaces, want_addr, cycles, client_renews).await
    })
    .await
}

// TODO(fxbug.dev/62554): the tests below are awful and contain lots of repetition. Reduce
// repetition.

#[variants_test]
async fn acquire_dhcp_with_dhcpd_bound_device<E: netemul::Endpoint>(name: &str) -> Result {
    test_dhcp::<E>(
        name,
        &mut [DhcpTestNetwork {
            name: DEFAULT_NETWORK_NAME,
            eps: &mut [default_server_endpoint(), default_client_endpoint()],
        }],
        &mut [&mut [
            fidl_fuchsia_net_dhcp::Parameter::IpAddrs(vec![DEFAULT_SERVER_IPV4]),
            DEFAULT_SERVER_PARAMETER_ADDRESSPOOL,
            fidl_fuchsia_net_dhcp::Parameter::BoundDeviceNames(vec!["eth2".to_string()]),
        ]],
        1,
        false,
    )
    .await
}

#[variants_test]
async fn acquire_dhcp_then_renew_with_dhcpd_bound_device<E: netemul::Endpoint>(
    name: &str,
) -> Result {
    test_dhcp::<E>(
        name,
        &mut [DhcpTestNetwork {
            name: DEFAULT_NETWORK_NAME,
            eps: &mut [default_server_endpoint(), default_client_endpoint()],
        }],
        &mut [&mut [
            fidl_fuchsia_net_dhcp::Parameter::IpAddrs(vec![DEFAULT_SERVER_IPV4]),
            // Use shortest lease length (4s) that provides distinct T1 and T2
            // values, i.e. 2s and 3s respectively.
            fidl_fuchsia_net_dhcp::Parameter::Lease(fidl_fuchsia_net_dhcp::LeaseLength {
                default: Some(4),
                max: Some(4),
                ..fidl_fuchsia_net_dhcp::LeaseLength::EMPTY
            }),
            DEFAULT_SERVER_PARAMETER_ADDRESSPOOL,
            fidl_fuchsia_net_dhcp::Parameter::BoundDeviceNames(vec!["eth2".to_string()]),
        ]],
        1,
        true,
    )
    .await
}

#[variants_test]
async fn acquire_dhcp_with_dhcpd_bound_device_dup_addr<E: netemul::Endpoint>(name: &str) -> Result {
    let expected_addr = match DEFAULT_CLIENT_WANT_ADDR.addr {
        fidl_fuchsia_net::IpAddress::Ipv4(fidl_fuchsia_net::Ipv4Address { addr: mut octets }) => {
            // We expect to assign the address numericaly succeeding the default client address
            // since the default client address will be assigned to a neighbor of the client so
            // the client should decline the offer and restart DHCP.
            *octets.iter_mut().last().expect("IPv4 addresses have a non-zero number of octets") +=
                1;

            fidl_fuchsia_net::Subnet {
                addr: fidl_fuchsia_net::IpAddress::Ipv4(fidl_fuchsia_net::Ipv4Address {
                    addr: octets,
                }),
                ..DEFAULT_CLIENT_WANT_ADDR
            }
        }
        fidl_fuchsia_net::IpAddress::Ipv6(a) => {
            return Err(anyhow::anyhow!("expected IPv4 address; got IPv6 address = {:?}", a));
        }
    };

    // Tests that if the client detects an address is already assigned to a neighbor,
    // the client will decline the request and restart DHCP. In this test, the neighbor
    // with the address assigned is the DHCP server.
    test_dhcp::<E>(
        name,
        &mut [DhcpTestNetwork {
            name: DEFAULT_NETWORK_NAME,
            eps: &mut [
                // Use two separate endpoints for the server so that its unicast responses to
                // DEFAULT_CLIENT_WANT_ADDR actually reach the network. This is not a hack around
                // Fuchsia behavior: Linux behaves the same way. On Linux, given an endpoint that
                // is BINDTODEVICE, unicasting to an IP address bound to another endpoint on the
                // same host WILL reach the network, rather than going through loopback. However,
                // if the first endpoint is NOT BINDTODEVICE, the outgoing message will be sent via
                // loopback.
                DhcpTestEndpoint {
                    name: "server-ep",
                    env: DhcpTestEnv::Server,
                    static_addrs: vec![DEFAULT_SERVER_ADDR],
                    want_addr: None,
                },
                DhcpTestEndpoint {
                    name: "server-ep2",
                    env: DhcpTestEnv::Server,
                    static_addrs: vec![DEFAULT_CLIENT_WANT_ADDR],
                    want_addr: None,
                },
                DhcpTestEndpoint {
                    name: "client-ep",
                    env: DhcpTestEnv::Client,
                    static_addrs: Vec::new(),
                    want_addr: Some(expected_addr),
                },
            ],
        }],
        &mut [&mut [
            fidl_fuchsia_net_dhcp::Parameter::IpAddrs(vec![DEFAULT_SERVER_IPV4]),
            DEFAULT_SERVER_PARAMETER_ADDRESSPOOL,
            fidl_fuchsia_net_dhcp::Parameter::BoundDeviceNames(vec!["eth2".to_string()]),
        ]],
        1,
        false,
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
                        static_addrs: vec![DEFAULT_SERVER_ADDR],
                        want_addr: None,
                    },
                    DhcpTestEndpoint {
                        name: "client-ep1",
                        env: DhcpTestEnv::Client,
                        static_addrs: Vec::new(),
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
                        static_addrs: vec![ALT_SERVER_ADDR],
                        want_addr: None,
                    },
                    DhcpTestEndpoint {
                        name: "client-ep2",
                        env: DhcpTestEnv::Client,
                        static_addrs: Vec::new(),
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
        false,
    )
    .await
}

#[derive(Copy, Clone)]
enum PersistenceMode {
    Persistent,
    Ephemeral,
}

impl std::fmt::Display for PersistenceMode {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            PersistenceMode::Persistent => write!(f, "persistent"),
            PersistenceMode::Ephemeral => write!(f, "ephemeral"),
        }
    }
}

impl PersistenceMode {
    fn dhcpd_args(&self) -> Option<Vec<String>> {
        match self {
            Self::Persistent => Some(vec![String::from("--persistent")]),
            Self::Ephemeral => None,
        }
    }

    fn dhcpd_params_after_restart(
        &self,
    ) -> Vec<(fidl_fuchsia_net_dhcp::ParameterName, fidl_fuchsia_net_dhcp::Parameter)> {
        match self {
            Self::Persistent => {
                test_dhcpd_params().into_iter().map(|p| (param_name(&p), p)).collect()
            }
            Self::Ephemeral => vec![
                fidl_fuchsia_net_dhcp::Parameter::IpAddrs(vec![]),
                fidl_fuchsia_net_dhcp::Parameter::AddressPool(fidl_fuchsia_net_dhcp::AddressPool {
                    network_id: Some(fidl_fuchsia_net::Ipv4Address { addr: [0, 0, 0, 0] }),
                    broadcast: Some(fidl_fuchsia_net::Ipv4Address { addr: [0, 0, 0, 0] }),
                    mask: Some(fidl_fuchsia_net::Ipv4Address { addr: [0, 0, 0, 0] }),
                    pool_range_start: Some(fidl_fuchsia_net::Ipv4Address { addr: [0, 0, 0, 0] }),
                    pool_range_stop: Some(fidl_fuchsia_net::Ipv4Address { addr: [0, 0, 0, 0] }),
                    ..fidl_fuchsia_net_dhcp::AddressPool::EMPTY
                }),
                fidl_fuchsia_net_dhcp::Parameter::Lease(fidl_fuchsia_net_dhcp::LeaseLength {
                    default: Some(86400),
                    max: Some(86400),
                    ..fidl_fuchsia_net_dhcp::LeaseLength::EMPTY
                }),
                fidl_fuchsia_net_dhcp::Parameter::PermittedMacs(vec![]),
                fidl_fuchsia_net_dhcp::Parameter::StaticallyAssignedAddrs(vec![]),
                fidl_fuchsia_net_dhcp::Parameter::ArpProbe(false),
                fidl_fuchsia_net_dhcp::Parameter::BoundDeviceNames(vec![]),
            ]
            .into_iter()
            .map(|p| (param_name(&p), p))
            .collect(),
        }
    }
}

// This collection of parameters is defined as a function because we need to allocate a Vec which
// cannot be done statically, i.e. as a constant.
fn test_dhcpd_params() -> Vec<fidl_fuchsia_net_dhcp::Parameter> {
    vec![
        fidl_fuchsia_net_dhcp::Parameter::IpAddrs(vec![DEFAULT_SERVER_IPV4]),
        DEFAULT_SERVER_PARAMETER_ADDRESSPOOL,
        fidl_fuchsia_net_dhcp::Parameter::Lease(fidl_fuchsia_net_dhcp::LeaseLength {
            default: Some(60),
            max: Some(60),
            ..fidl_fuchsia_net_dhcp::LeaseLength::EMPTY
        }),
        fidl_fuchsia_net_dhcp::Parameter::PermittedMacs(vec![fidl_mac!("aa:bb:cc:dd:ee:ff")]),
        fidl_fuchsia_net_dhcp::Parameter::StaticallyAssignedAddrs(vec![
            fidl_fuchsia_net_dhcp::StaticAssignment {
                host: Some(fidl_mac!("aa:bb:cc:dd:ee:ff")),
                assigned_addr: Some(fidl_ip_v4!("192.168.0.2")),
                ..fidl_fuchsia_net_dhcp::StaticAssignment::EMPTY
            },
        ]),
        fidl_fuchsia_net_dhcp::Parameter::ArpProbe(true),
        fidl_fuchsia_net_dhcp::Parameter::BoundDeviceNames(vec!["eth2".to_string()]),
    ]
}

fn param_name(param: &fidl_fuchsia_net_dhcp::Parameter) -> fidl_fuchsia_net_dhcp::ParameterName {
    match param {
        fidl_fuchsia_net_dhcp::Parameter::IpAddrs(_) => {
            fidl_fuchsia_net_dhcp::ParameterName::IpAddrs
        }
        fidl_fuchsia_net_dhcp::Parameter::AddressPool(_) => {
            fidl_fuchsia_net_dhcp::ParameterName::AddressPool
        }
        fidl_fuchsia_net_dhcp::Parameter::Lease(_) => {
            fidl_fuchsia_net_dhcp::ParameterName::LeaseLength
        }
        fidl_fuchsia_net_dhcp::Parameter::PermittedMacs(_) => {
            fidl_fuchsia_net_dhcp::ParameterName::PermittedMacs
        }
        fidl_fuchsia_net_dhcp::Parameter::StaticallyAssignedAddrs(_) => {
            fidl_fuchsia_net_dhcp::ParameterName::StaticallyAssignedAddrs
        }
        fidl_fuchsia_net_dhcp::Parameter::ArpProbe(_) => {
            fidl_fuchsia_net_dhcp::ParameterName::ArpProbe
        }
        fidl_fuchsia_net_dhcp::Parameter::BoundDeviceNames(_) => {
            fidl_fuchsia_net_dhcp::ParameterName::BoundDeviceNames
        }
        fidl_fuchsia_net_dhcp::ParameterUnknown!() => {
            panic!("attempted to retrieve name of Parameter::Unknown");
        }
    }
}

// This test guards against regression for the issue found in https://fxbug.dev/62989. The test
// attempts to create an inconsistent state on the dhcp server by allowing the server to complete a
// transaction with a client, thereby creating a record of a lease. The server is then restarted;
// if the linked issue has not been fixed, then the server will inadvertently erase its
// configuration parameters from persistent storage, which will lead to an inconsistent server
// state on the next restart.  Finally, the server is restarted one more time, and then its
// clear_leases() function is triggered, which will cause a panic if the server is in an
// inconsistent state.
#[variants_test]
async fn acquire_persistent_dhcp_server_after_restart<E: netemul::Endpoint>(name: &str) -> Result {
    let mode = PersistenceMode::Persistent;
    Ok(acquire_dhcp_server_after_restart::<E>(&format!("{}_{}", name, mode), mode).await?)
}

// An ephemeral dhcp server cannot become inconsistent with its persistent state because it has
// none.  However, without persistent state, an ephemeral dhcp server cannot run without explicit
// configuration.  This test verifies that an ephemeral dhcp server will return an error if run
// after restarting.
#[variants_test]
async fn acquire_ephemeral_dhcp_server_after_restart<E: netemul::Endpoint>(name: &str) -> Result {
    let mode = PersistenceMode::Ephemeral;
    Ok(acquire_dhcp_server_after_restart::<E>(&format!("{}_{}", name, mode), mode).await?)
}

fn setup_component_proxy(
    mode: PersistenceMode,
    server_env: &netemul::TestEnvironment<'_>,
) -> Result<(fuchsia_component::client::App, fidl_fuchsia_net_dhcp::Server_Proxy)> {
    let dhcpd = fuchsia_component::client::launch(
        &server_env.get_launcher().context("failed to create launcher")?,
        KnownServices::DhcpServer.get_url().to_string(),
        mode.dhcpd_args(),
    )
    .context("failed to start dhcpd")?;
    let dhcp_server = dhcpd
        .connect_to_service::<fidl_fuchsia_net_dhcp::Server_Marker>()
        .context("failed to connect to DHCP server")?;
    Ok((dhcpd, dhcp_server))
}

async fn cleanup_component(dhcpd: &mut fuchsia_component::client::App) -> Result {
    let () = dhcpd.kill().context("failed to kill dhcpd component")?;
    assert_eq!(
        dhcpd.wait().await.context("failed to await dhcpd component exit")?.code(),
        fuchsia_zircon::sys::ZX_TASK_RETCODE_SYSCALL_KILL
    );
    Ok(())
}

async fn acquire_dhcp_server_after_restart<E: netemul::Endpoint>(
    name: &str,
    mode: PersistenceMode,
) -> Result {
    let sandbox = netemul::TestSandbox::new().context("failed to create sandbox")?;

    let server_env = sandbox
        .create_netstack_environment_with::<Netstack2, _, _>(
            format!("{}_server", name),
            &[KnownServices::SecureStash],
        )
        .context("failed to create server environment")?;

    let client_env = sandbox
        .create_netstack_environment::<Netstack2, _>(format!("{}_client", name))
        .context("failed to create client environment")?;

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
        let (mut dhcpd, dhcp_server) = setup_component_proxy(mode, &server_env)?;
        let () = set_server_parameters(
            &dhcp_server,
            &mut [
                fidl_fuchsia_net_dhcp::Parameter::IpAddrs(vec![DEFAULT_SERVER_IPV4]),
                DEFAULT_SERVER_PARAMETER_ADDRESSPOOL,
                fidl_fuchsia_net_dhcp::Parameter::BoundDeviceNames(vec!["eth2".to_string()]),
            ],
        )
        .await?;
        let () = dhcp_server
            .start_serving()
            .await
            .context("failed to call dhcp/Server.StartServing")?
            .map_err(fuchsia_zircon::Status::from_raw)
            .context("dhcp/Server.StartServing returned error")?;
        let () =
            client_acquires_addr(&client_env, &[client_ep], DEFAULT_CLIENT_WANT_ADDR, 1, false)
                .await
                .context("client failed to acquire address")?;
        let () =
            dhcp_server.stop_serving().await.context("failed to call dhcp/Server.StopServing")?;
        let () = cleanup_component(&mut dhcpd).await?;
    }

    // Restart the server in an attempt to force the server's persistent storage into an
    // inconsistent state whereby the addresses leased to clients do not agree with the contents of
    // the server's address pool. If the server is in ephemeral mode, it will fail at the call to
    // start_serving() since it will not have retained its parameters.
    {
        let (mut dhcpd, dhcp_server) = setup_component_proxy(mode, &server_env)?;
        let () = match mode {
            PersistenceMode::Persistent => {
                let () = dhcp_server
                    .start_serving()
                    .await
                    .context("failed to call dhcp/Server.StartServing")?
                    .map_err(fuchsia_zircon::Status::from_raw)
                    .context("dhcp/Server.StartServing returned error")?;
                dhcp_server
                    .stop_serving()
                    .await
                    .context("failed to call dhcp/Server.StopServing")?
            }
            PersistenceMode::Ephemeral => {
                matches::assert_matches!(
                    dhcp_server
                        .start_serving()
                        .await
                        .context("failed to call dhcp/Server.StartServing")?
                        .map_err(fuchsia_zircon::Status::from_raw),
                    Err(fuchsia_zircon::Status::INVALID_ARGS)
                );
            }
        };
        let () = cleanup_component(&mut dhcpd).await?;
    }

    // Restart the server again in order to load the inconsistent state into the server's runtime
    // representation. Call clear_leases() to trigger a panic resulting from inconsistent state,
    // provided that the issue motivating this test is unfixed/regressed. If the server is in
    // ephemeral mode, it will fail at the call to start_serving() since it will not have retained
    // its parameters.
    {
        let (mut dhcpd, dhcp_server) = setup_component_proxy(mode, &server_env)?;
        let () = match mode {
            PersistenceMode::Persistent => {
                let () = dhcp_server
                    .start_serving()
                    .await
                    .context("failed to call dhcp/Server.StartServing")?
                    .map_err(fuchsia_zircon::Status::from_raw)
                    .context("dhcp/Server.StartServing returned error")?;
                let () = dhcp_server
                    .stop_serving()
                    .await
                    .context("failed to call dhcp/Server.StopServing")?;
                dhcp_server
                    .clear_leases()
                    .await
                    .context("failed to call dhcp/Server.ClearLeases")?
                    .map_err(fuchsia_zircon::Status::from_raw)
                    .context("dhcp/Server.ClearLeases returned error")?;
            }
            PersistenceMode::Ephemeral => {
                matches::assert_matches!(
                    dhcp_server
                        .start_serving()
                        .await
                        .context("failed to call dhcp/Server.StartServing")?
                        .map_err(fuchsia_zircon::Status::from_raw),
                    Err(fuchsia_zircon::Status::INVALID_ARGS)
                );
            }
        };
        let () = cleanup_component(&mut dhcpd).await?;
    }

    Ok(())
}

#[variants_test]
async fn test_dhcp_server_persistence_mode_persistent<E: netemul::Endpoint>(name: &str) -> Result {
    let mode = PersistenceMode::Persistent;
    Ok(test_dhcp_server_persistence_mode::<E>(&format!("{}_{}", name, mode), mode).await?)
}

#[variants_test]
async fn test_dhcp_server_persistence_mode_ephemeral<E: netemul::Endpoint>(name: &str) -> Result {
    let mode = PersistenceMode::Ephemeral;
    Ok(test_dhcp_server_persistence_mode::<E>(&format!("{}_{}", name, mode), mode).await?)
}

async fn test_dhcp_server_persistence_mode<E: netemul::Endpoint>(
    name: &str,
    mode: PersistenceMode,
) -> Result {
    let sandbox = netemul::TestSandbox::new().context("failed to create sandbox")?;

    let server_env = sandbox
        .create_netstack_environment_with::<Netstack2, _, _>(
            format!("{}_server", name),
            &[KnownServices::SecureStash],
        )
        .context("failed to create server environment")?;

    let network = sandbox.create_network(name).await.context("failed to create network")?;
    let _server_ep = server_env
        .join_network::<E, _>(
            &network,
            "server-ep",
            &netemul::InterfaceConfig::StaticIp(DEFAULT_SERVER_ADDR),
        )
        .await
        .context("failed to create server network endpoint")?;

    // Configure the server with parameters and then restart it.
    {
        let (mut dhcpd, dhcp_server) = setup_component_proxy(mode, &server_env)?;
        let () = set_server_parameters(&dhcp_server, &mut test_dhcpd_params()).await?;
        let () = cleanup_component(&mut dhcpd).await?;
    }

    // Assert that configured parameters after the restart correspond to the persistence mode of the
    // server.
    {
        let (mut dhcpd, dhcp_server) = setup_component_proxy(mode, &server_env)?;
        let dhcp_server = &dhcp_server;
        let () = stream::iter(mode.dhcpd_params_after_restart().iter_mut())
            .map(Ok)
            .try_for_each_concurrent(None, |(name, parameter)| async move {
                Result::Ok(assert_eq!(
                    dhcp_server
                        .get_parameter(*name)
                        .await
                        .with_context(|| {
                            format!("failed to call dhcp/Server.GetParameter({:?})", name)
                        })?
                        .map_err(fuchsia_zircon::Status::from_raw)
                        .with_context(|| {
                            format!("dhcp/Server.GetParameter({:?}) returned error", name)
                        })
                        .unwrap(),
                    *parameter
                ))
            })
            .await
            .context("failed to get server parameters")?;
        let () = cleanup_component(&mut dhcpd).await?;
    }
    Ok(())
}
