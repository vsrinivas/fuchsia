// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

mod environments;

use environments::*;

use anyhow::Context as _;

use fidl_fuchsia_net_ext::ip_addr;
use fidl_fuchsia_net_stack_ext::{exec_fidl, FidlReturn};
use futures::stream::{self, StreamExt, TryStreamExt};

type Result<T = ()> = std::result::Result<T, anyhow::Error>;

/// Launches a new netstack with the endpoint and returns the IPv6 addresses
/// initially assigned to it.
///
/// If `run_netstack_and_get_ipv6_addrs_for_endpoint` returns successfully, it
/// is guaranteed that the launched netstack has been terminated. Note, if
/// `run_netstack_and_get_ipv6_addrs_for_endpoint` does not return successfully,
/// the launched netstack will still be terminated, but no guarantees are made
/// about when that will happen.
async fn run_netstack_and_get_ipv6_addrs_for_endpoint<N: Netstack>(
    endpoint: &TestEndpoint<'_>,
    launcher: &fidl_fuchsia_sys::LauncherProxy,
    name: String,
) -> Result<Vec<fidl_fuchsia_net::Subnet>> {
    // Launch the netstack service.

    let mut app = fuchsia_component::client::AppBuilder::new(N::VERSION.get_url())
        .spawn(launcher)
        .context("failed to spawn netstack")?;
    let netstack = app
        .connect_to_service::<fidl_fuchsia_netstack::NetstackMarker>()
        .context("failed to connect to netstack service")?;

    // Add the device and get its interface state from netstack.
    // TODO(48907) Support Network Device. This helper fn should use stack.fidl
    // and be agnostic over interface type.
    let id = netstack
        .add_ethernet_device(
            &name,
            &mut fidl_fuchsia_netstack::InterfaceConfig {
                name: name.to_string(),
                filepath: "/fake/filepath/for_test".to_string(),
                metric: 0,
                ip_address_config: fidl_fuchsia_netstack::IpAddressConfig::Dhcp(true),
            },
            endpoint
                .get_ethernet()
                .await
                .context("add_ethernet_device requires an Ethernet endpoint")?,
        )
        .await
        .context("failed to add ethernet device")?;
    let interface = netstack
        .get_interfaces2()
        .await
        .context("failed to get interfaces")?
        .into_iter()
        .find(|interface| interface.id == id)
        .ok_or(anyhow::format_err!("failed to find added ethernet device"))?;

    // Kill the netstack.
    //
    // Note, simply dropping `component_controller` would also kill the netstack
    // but we explicitly kill it and wait for the terminated event before
    // proceeding.
    let () = app.kill().context("failed to kill app")?;
    let _exit_status = app.wait().await.context("failed to observe netstack termination")?;

    Ok(interface.ipv6addrs)
}

/// Regression test: test that Netstack.SetInterfaceStatus does not kill the channel to the client
/// if given an invalid interface id.
#[fuchsia_async::run_singlethreaded(test)]
async fn set_interface_status_unknown_interface() -> Result {
    let name = "set_interface_status";
    let sandbox = TestSandbox::new()?;
    let (_env, netstack) =
        sandbox.new_netstack::<Netstack2, fidl_fuchsia_netstack::NetstackMarker>(name)?;

    let interfaces = netstack.get_interfaces2().await.context("failed to call get_interfaces2")?;
    let next_id =
        1 + interfaces.iter().map(|interface| interface.id).max().ok_or(anyhow::format_err!(
            "failed to find any network interfaces (at least localhost should be present)"
        ))?;

    let () = netstack
        .set_interface_status(next_id, false)
        .context("failed to call set_interface_status")?;
    let _interfaces = netstack
        .get_interfaces2()
        .await
        .context("failed to invoke netstack method after calling set_interface_status with an invalid argument")?;

    Ok(())
}

/// Test that across netstack runs, a device will initially be assigned the same
/// IPv6 addresses.
#[fuchsia_async::run_singlethreaded(test)]
async fn consistent_initial_ipv6_addrs() -> Result {
    let name = "consistent_initial_ipv6_addrs";
    let sandbox = TestSandbox::new().context("failed to create sandbox")?;
    let env = sandbox
        .create_environment(name, &[KnownServices::SecureStash])
        .context("failed to create environment")?;
    let launcher = env.get_launcher().context("failed to get launcher")?;
    let endpoint = sandbox.create_endpoint(name).await.context("failed to create endpoint")?;

    // Make sure netstack uses the same addresses across runs for a device.
    let first_run_addrs = run_netstack_and_get_ipv6_addrs_for_endpoint::<Netstack2>(
        &endpoint,
        &launcher,
        name.to_string(),
    )
    .await?;
    let second_run_addrs = run_netstack_and_get_ipv6_addrs_for_endpoint::<Netstack2>(
        &endpoint,
        &launcher,
        name.to_string(),
    )
    .await?;
    assert_eq!(first_run_addrs, second_run_addrs);

    Ok(())
}

#[fuchsia_async::run_singlethreaded(test)]
async fn inspect_objects() -> Result {
    let sandbox = TestSandbox::new().context("failed to create sandbox")?;
    let env = sandbox
        .create_empty_environment("inspect_objects")
        .context("failed to create environment")?;
    let launcher = env.get_launcher().context("failed to create launcher")?;

    let netstack = fuchsia_component::client::launch(
        &launcher,
        <Netstack2 as Netstack>::VERSION.get_url().to_string(),
        None,
    )
    .context("failed to start netstack")?;

    // TODO(fxbug.dev/4629): the launcher API lies and claims it connects you to "the" directory
    // request, but it doesn't. It connects you to the "svc" directory under the directory request.
    // That means that reading anything other than FIDL services isn't possible, and THAT means
    // that this test is impossible.
    if false {
        for path in ["counters", "interfaces"].iter() {
            let (client, server) =
                fidl::endpoints::create_proxy::<fidl_fuchsia_inspect_deprecated::InspectMarker>()
                    .context("failed to create proxy")?;

            let path = format!("diagnostics/{}/inspect", path);
            let () = netstack
                .pass_to_named_service(&path, server.into_channel())
                .with_context(|| format!("failed to connect to {}", path))?;

            let _object = client.read_data().await.context("failed to call ReadData")?;
        }
    }
    Ok(())
}

#[fuchsia_async::run_singlethreaded(test)]
async fn add_ethernet_device() -> Result {
    let name = "add_ethernet_device";
    let sandbox = TestSandbox::new().context("failed to create sandbox")?;
    let (_env, netstack, device) = sandbox
        .new_netstack_and_device::<Netstack2, fidl_fuchsia_netstack::NetstackMarker>(name)
        .await?;

    let id = netstack
        .add_ethernet_device(
            name,
            &mut fidl_fuchsia_netstack::InterfaceConfig {
                name: name.to_string(),
                filepath: "/fake/filepath/for_test".to_string(),
                metric: 0,
                ip_address_config: fidl_fuchsia_netstack::IpAddressConfig::Dhcp(true),
            },
            // We're testing add_ethernet_device (netstack.fidl), which
            // does not have a network device entry point.
            device
                .get_ethernet()
                .await
                .context("add_ethernet_device requires an Ethernet endpoint")?,
        )
        .await
        .context("failed to add ethernet device")?;
    let interface = netstack
        .get_interfaces2()
        .await
        .context("failed to get interfaces")?
        .into_iter()
        .find(|interface| interface.id == id)
        .ok_or(anyhow::format_err!("failed to find added ethernet device"))?;
    assert_eq!(interface.features & fidl_fuchsia_hardware_ethernet::INFO_FEATURE_LOOPBACK, 0);
    assert_eq!(interface.flags & fidl_fuchsia_netstack::NET_INTERFACE_FLAG_UP, 0);
    Ok::<(), anyhow::Error>(())
}

async fn add_ethernet_interface<N: Netstack>(name: &'static str) -> Result {
    let sandbox = TestSandbox::new()?;
    let (_env, stack, device) =
        sandbox.new_netstack_and_device::<N, fidl_fuchsia_net_stack::StackMarker>(name).await?;
    let id = device.add_to_stack(&stack).await?;
    let interface = stack
        .list_interfaces()
        .await
        .context("failed to list interfaces")?
        .into_iter()
        .find(|interface| interface.id == id)
        .ok_or(anyhow::format_err!("failed to find added ethernet interface"))?;
    assert_eq!(
        interface.properties.features & fidl_fuchsia_hardware_ethernet::INFO_FEATURE_LOOPBACK,
        0
    );
    assert_eq!(interface.properties.physical_status, fidl_fuchsia_net_stack::PhysicalStatus::Down);
    Ok(())
}

#[fuchsia_async::run_singlethreaded(test)]
async fn add_ethernet_interface_n2() -> Result {
    add_ethernet_interface::<Netstack2>("add_ethernet_interface_n2").await
}

#[fuchsia_async::run_singlethreaded(test)]
async fn add_ethernet_interface_n3() -> Result {
    add_ethernet_interface::<Netstack3>("add_ethernet_interface_n3").await
}

#[fuchsia_async::run_singlethreaded(test)]
async fn add_del_interface_address() -> Result {
    let name = "add_del_interface_address";

    let sandbox = TestSandbox::new().context("failed to create sandbox")?;
    let (_env, stack) = sandbox
        .new_netstack::<Netstack2, fidl_fuchsia_net_stack::StackMarker>(name)
        .context("failed to create environment")?;

    let interfaces = stack.list_interfaces().await.context("failed to list interfaces")?;
    let loopback = interfaces
        .iter()
        .find(|interface| {
            interface.properties.features & fidl_fuchsia_hardware_ethernet::INFO_FEATURE_LOOPBACK
                != 0
        })
        .ok_or(anyhow::format_err!("failed to find loopback"))?;
    let mut interface_address = fidl_fuchsia_net_stack::InterfaceAddress {
        ip_address: ip_addr![1, 1, 1, 1],
        prefix_len: 32,
    };
    let res = stack
        .add_interface_address(loopback.id, &mut interface_address)
        .await
        .context("failed to call add interface address")?;
    assert_eq!(res, Ok(()));
    let loopback =
        exec_fidl!(stack.get_interface_info(loopback.id), "failed to get loopback interface")?;

    assert!(
        loopback.properties.addresses.iter().find(|addr| *addr == &interface_address).is_some(),
        "couldn't find {:#?} in {:#?}",
        interface_address,
        loopback.properties.addresses
    );

    let res = stack
        .del_interface_address(loopback.id, &mut interface_address)
        .await
        .context("failed to call del interface address")?;
    assert_eq!(res, Ok(()));
    let loopback =
        exec_fidl!(stack.get_interface_info(loopback.id), "failed to get loopback interface")?;

    assert!(
        loopback.properties.addresses.iter().find(|addr| *addr == &interface_address).is_none(),
        "did not expect to find {:#?} in {:#?}",
        interface_address,
        loopback.properties.addresses
    );

    Ok(())
}

#[fuchsia_async::run_singlethreaded(test)]
async fn add_remove_interface_address_errors() -> Result {
    let name = "add_remove_interface_address_errors";

    let sandbox = TestSandbox::new().context("failed to create sandbox")?;
    let (env, stack) = sandbox
        .new_netstack::<Netstack2, fidl_fuchsia_net_stack::StackMarker>(name)
        .context("failed to create environment")?;
    let netstack = env
        .connect_to_service::<fidl_fuchsia_netstack::NetstackMarker>()
        .context("failed to connect to netstack")?;

    let interfaces = stack.list_interfaces().await.context("failed to list interfaces")?;
    let max_id = interfaces.iter().map(|interface| interface.id).max().unwrap_or(0);
    let mut interface_address = fidl_fuchsia_net_stack::InterfaceAddress {
        ip_address: ip_addr![0, 0, 0, 0],
        prefix_len: 0,
    };

    // Don't crash on interface not found.

    let error = stack
        .add_interface_address(max_id + 1, &mut interface_address)
        .await
        .context("failed to call add interface address")?
        .unwrap_err();
    assert_eq!(error, fidl_fuchsia_net_stack::Error::NotFound);

    let error = netstack
        .remove_interface_address(
            std::convert::TryInto::try_into(max_id + 1).expect("should fit"),
            &mut interface_address.ip_address,
            interface_address.prefix_len,
        )
        .await
        .context("failed to call add interface address")?;
    assert_eq!(
        error,
        fidl_fuchsia_netstack::NetErr {
            status: fidl_fuchsia_netstack::Status::UnknownInterface,
            message: "".to_string(),
        },
    );

    // Don't crash on invalid prefix length.
    interface_address.prefix_len = 43;
    let error = stack
        .add_interface_address(max_id, &mut interface_address)
        .await
        .context("failed to call add interface address")?
        .unwrap_err();
    assert_eq!(error, fidl_fuchsia_net_stack::Error::InvalidArgs);

    let error = netstack
        .remove_interface_address(
            std::convert::TryInto::try_into(max_id).expect("should fit"),
            &mut interface_address.ip_address,
            interface_address.prefix_len,
        )
        .await
        .context("failed to call add interface address")?;
    assert_eq!(
        error,
        fidl_fuchsia_netstack::NetErr {
            status: fidl_fuchsia_netstack::Status::ParseError,
            message: "prefix length exceeds address length".to_string(),
        },
    );

    Ok(())
}

async fn get_interface_info_not_found<N: Netstack>(name: &'static str) -> Result {
    let sandbox = TestSandbox::new().context("failed to create sandbox")?;
    let (_env, stack) = sandbox
        .new_netstack::<N, fidl_fuchsia_net_stack::StackMarker>(name)
        .context("failed to create environment")?;

    let interfaces = stack.list_interfaces().await.context("failed to list interfaces")?;
    let max_id = interfaces.iter().map(|interface| interface.id).max().unwrap_or(0);
    let res =
        stack.get_interface_info(max_id + 1).await.context("failed to call get interface info")?;
    assert_eq!(res, Err(fidl_fuchsia_net_stack::Error::NotFound));
    Ok(())
}

#[fuchsia_async::run_singlethreaded(test)]
async fn get_interface_info_not_found_n2() -> Result {
    get_interface_info_not_found::<Netstack2>("get_interface_info_not_found_n2").await
}

#[fuchsia_async::run_singlethreaded(test)]
async fn get_interface_info_not_found_n3() -> Result {
    get_interface_info_not_found::<Netstack3>("get_interface_info_not_found_n3").await
}

#[fuchsia_async::run_singlethreaded(test)]
async fn disable_interface_loopback() -> Result {
    let name = "disable_interface_loopback";

    let sandbox = TestSandbox::new().context("failed to create sandbox")?;
    let (_env, stack) = sandbox
        .new_netstack::<Netstack2, fidl_fuchsia_net_stack::StackMarker>(name)
        .context("failed to create environment")?;

    let interfaces = stack.list_interfaces().await.context("failed to list interfaces")?;
    let localhost = interfaces
        .iter()
        .find(|interface| {
            interface.properties.features & fidl_fuchsia_hardware_ethernet::INFO_FEATURE_LOOPBACK
                != 0
        })
        .ok_or(anyhow::format_err!("failed to find loopback interface"))?;
    assert_eq!(
        localhost.properties.administrative_status,
        fidl_fuchsia_net_stack::AdministrativeStatus::Enabled
    );
    let () = exec_fidl!(stack.disable_interface(localhost.id), "failed to disable interface")?;
    let info = exec_fidl!(stack.get_interface_info(localhost.id), "failed to get interface info")?;
    assert_eq!(
        info.properties.administrative_status,
        fidl_fuchsia_net_stack::AdministrativeStatus::Disabled
    );
    Ok(())
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
        .create_netstack_environment::<Netstack2, _>(format!("{}_server", name))
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
