// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use failure::ResultExt;
use std::convert::TryInto;

use fidl_fuchsia_net_stack_ext::{exec_fidl, FidlReturn};

type Result = std::result::Result<(), failure::Error>;

// TODO(gongt) Use an attribute macro to reduce the boilerplate of running the
// same test for both N2 and N3.
/// Abstraction for a Fuchsia component which offers network stack services.
trait Netstack {
    const URL: &'static str;
    const ARGS: &'static [&'static str];
}

/// Uninstantiable type that represents Netstack2's implementation of a
/// network stack.
enum Netstack2 {}

impl Netstack for Netstack2 {
    const URL: &'static str = fuchsia_component::fuchsia_single_component_package_url!("netstack");
    const ARGS: &'static [&'static str] = &["--sniff", "verbosity=debug"];
}

/// Uninstantiable type that represents Netstack3's implementation of a
/// network stack.
enum Netstack3 {}

impl Netstack for Netstack3 {
    const URL: &'static str = fuchsia_component::fuchsia_single_component_package_url!("netstack3");
    const ARGS: &'static [&'static str] = &[];
}

fn connect_to_service<S: fidl::endpoints::ServiceMarker + fidl::endpoints::DiscoverableService>(
    managed_environment: &fidl_fuchsia_netemul_environment::ManagedEnvironmentProxy,
) -> std::result::Result<S::Proxy, failure::Error> {
    let (proxy, server) = fuchsia_zircon::Channel::create()?;
    let () = managed_environment.connect_to_service(S::SERVICE_NAME, server)?;
    let proxy = fuchsia_async::Channel::from_channel(proxy)?;
    Ok(<S::Proxy as fidl::endpoints::Proxy>::from_channel(proxy))
}

fn get_network_context(
    sandbox: &fidl_fuchsia_netemul_sandbox::SandboxProxy,
) -> std::result::Result<fidl_fuchsia_netemul_network::NetworkContextProxy, failure::Error> {
    let (client, server) =
        fidl::endpoints::create_proxy::<fidl_fuchsia_netemul_network::NetworkContextMarker>()
            .context("failed to create network context proxy")?;
    let () = sandbox.get_network_context(server).context("failed to get network context")?;
    Ok(client)
}

fn get_endpoint_manager(
    network_context: &fidl_fuchsia_netemul_network::NetworkContextProxy,
) -> std::result::Result<fidl_fuchsia_netemul_network::EndpointManagerProxy, failure::Error> {
    let (client, server) =
        fidl::endpoints::create_proxy::<fidl_fuchsia_netemul_network::EndpointManagerMarker>()
            .context("failed to create endpoint manager proxy")?;
    let () =
        network_context.get_endpoint_manager(server).context("failed to get endpoint manager")?;
    Ok(client)
}

async fn create_endpoint(
    name: &'static str,
    endpoint_manager: &fidl_fuchsia_netemul_network::EndpointManagerProxy,
) -> std::result::Result<fidl_fuchsia_netemul_network::EndpointProxy, failure::Error> {
    let (status, endpoint) = endpoint_manager
        .create_endpoint(
            name,
            &mut fidl_fuchsia_netemul_network::EndpointConfig {
                mtu: 1500,
                mac: None,
                backing: fidl_fuchsia_netemul_network::EndpointBacking::Ethertap,
            },
        )
        .await
        .context("failed to create endpoint")?;
    let () = fuchsia_zircon::Status::ok(status).context("failed to create endpoint")?;
    let endpoint = endpoint
        .ok_or(failure::err_msg("failed to create endpoint"))?
        .into_proxy()
        .context("failed to get endpoint proxy")?;
    Ok(endpoint)
}

fn create_netstack_environment<N: Netstack>(
    sandbox: &fidl_fuchsia_netemul_sandbox::SandboxProxy,
    name: String,
) -> std::result::Result<fidl_fuchsia_netemul_environment::ManagedEnvironmentProxy, failure::Error>
{
    let (client, server) = fidl::endpoints::create_proxy::<
        fidl_fuchsia_netemul_environment::ManagedEnvironmentMarker,
    >()
    .context("failed to create managed environment proxy")?;
    let () = sandbox
        .create_environment(
            server,
            fidl_fuchsia_netemul_environment::EnvironmentOptions {
                name: Some(name),
                services: Some([
                    <fidl_fuchsia_net_stack::StackMarker as fidl::endpoints::DiscoverableService>::SERVICE_NAME,
                    <fidl_fuchsia_netstack::NetstackMarker as fidl::endpoints::DiscoverableService>::SERVICE_NAME,
                    <fidl_fuchsia_posix_socket::ProviderMarker as fidl::endpoints::DiscoverableService>::SERVICE_NAME,
                ]
                    // TODO(tamird): use into_iter after
                    // https://github.com/rust-lang/rust/issues/25725.
                    .iter()
                    .map(std::ops::Deref::deref)
                    .map(str::to_string)
                    .map(|name| fidl_fuchsia_netemul_environment::LaunchService {
                        name,
                        url: N::URL.to_string(),
                        arguments: Some(
                            N::ARGS
                                // TODO(tamird): use into_iter after
                                // https://github.com/rust-lang/rust/issues/25725.
                                .iter()
                                .map(std::ops::Deref::deref)
                                .map(str::to_string)
                                .collect(),
                        ),
                    })
                    .chain(
                        Some(
                            fidl_fuchsia_netemul_environment::LaunchService {
                                name: <fidl_fuchsia_stash::StoreMarker as fidl::endpoints::DiscoverableService>::SERVICE_NAME.to_string(),
                                url: fuchsia_component::fuchsia_single_component_package_url!("stash").to_string(),
                                arguments: None,
                            }
                    ))
                    .chain(
                        Some(
                            fidl_fuchsia_netemul_environment::LaunchService {
                                name: <fidl_fuchsia_cobalt::LoggerFactoryMarker as fidl::endpoints::DiscoverableService>::SERVICE_NAME.to_string(),
                                url: fuchsia_component::fuchsia_single_component_package_url!("mock_cobalt").to_string(),
                                arguments: None,
                            }
                    ))
                    .collect()),
                devices: None,
                inherit_parent_launch_services: None,
                logger_options: Some(fidl_fuchsia_netemul_environment::LoggerOptions {
                    enabled: Some(true),
                    klogs_enabled: None,
                    filter_options: None,
                    syslog_output: Some(true),
                }),
            },
        )
        .context("failed to create environment")?;
    Ok(client)
}

async fn with_netstack_and_device<F, T, N, S>(name: &'static str, async_fn: T) -> Result
where
    F: futures::Future<Output = Result>,
    T: FnOnce(
        S::Proxy,
        fidl::endpoints::ClientEnd<fidl_fuchsia_hardware_ethernet::DeviceMarker>,
    ) -> F,
    N: Netstack,
    S: fidl::endpoints::ServiceMarker + fidl::endpoints::DiscoverableService,
{
    let sandbox = fuchsia_component::client::connect_to_service::<
        fidl_fuchsia_netemul_sandbox::SandboxMarker,
    >()
    .context("failed to connect to sandbox")?;
    let network_context = get_network_context(&sandbox).context("failed to get network context")?;
    let endpoint_manager =
        get_endpoint_manager(&network_context).context("failed to get endpoint manager")?;
    let endpoint =
        create_endpoint(name, &endpoint_manager).await.context("failed to create endpoint")?;
    let device = endpoint.get_ethernet_device().await.context("failed to get ethernet device")?;
    let managed_environment = create_netstack_environment::<N>(&sandbox, name.to_string())
        .context("failed to create netstack environment")?;
    let netstack_proxy =
        connect_to_service::<S>(&managed_environment).context("failed to connect to netstack")?;
    async_fn(netstack_proxy, device).await
}

#[fuchsia_async::run_singlethreaded(test)]
async fn inspect_objects() -> Result {
    let launcher = fuchsia_component::client::launcher().context("failed to create launcher")?;

    let netstack = fuchsia_component::client::launch(
        &launcher,
        fuchsia_component::fuchsia_single_component_package_url!("netstack").to_string(),
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
                .with_context(|_| format!("failed to connect to {}", path))?;

            let _object = client.read_data().await.context("failed to call ReadData")?;
        }
    }
    Ok(())
}

#[fuchsia_async::run_singlethreaded(test)]
async fn add_ethernet_device() -> Result {
    let name = "add_ethernet_device";

    with_netstack_and_device::<_, _, Netstack2, fidl_fuchsia_netstack::NetstackMarker>(
        name,
        |netstack, device| {
            async move {
                let id = netstack
                    .add_ethernet_device(
                        name,
                        &mut fidl_fuchsia_netstack::InterfaceConfig {
                            name: name.to_string(),
                            filepath: "/fake/filepath/for_test".to_string(),
                            metric: 0,
                            ip_address_config: fidl_fuchsia_netstack::IpAddressConfig::Dhcp(true),
                        },
                        device,
                    )
                    .await
                    .context("failed to add ethernet device")?;
                let interface = netstack
                    .get_interfaces2()
                    .await
                    .context("failed to get interfaces")?
                    .into_iter()
                    .find(|interface| interface.id == id)
                    .ok_or(failure::err_msg("failed to find added ethernet device"))?;
                assert_eq!(
                    interface.features & fidl_fuchsia_hardware_ethernet::INFO_FEATURE_LOOPBACK,
                    0
                );
                assert_eq!(interface.flags & fidl_fuchsia_netstack::NET_INTERFACE_FLAG_UP, 0);
                Ok::<(), failure::Error>(())
            }
        },
    )
    .await
}

async fn add_ethernet_interface<N: Netstack>(name: &'static str) -> Result {
    with_netstack_and_device::<_, _, N, fidl_fuchsia_net_stack::StackMarker>(
        name,
        |stack, device| {
            async move {
                let id = exec_fidl!(
                    stack.add_ethernet_interface(name, device),
                    "failed to add ethernet interface"
                )?;
                let interface = stack
                    .list_interfaces()
                    .await
                    .context("failed to list interfaces")?
                    .into_iter()
                    .find(|interface| interface.id == id)
                    .ok_or(failure::err_msg("failed to find added ethernet interface"))?;
                assert_eq!(
                    interface.properties.features
                        & fidl_fuchsia_hardware_ethernet::INFO_FEATURE_LOOPBACK,
                    0
                );
                assert_eq!(
                    interface.properties.physical_status,
                    fidl_fuchsia_net_stack::PhysicalStatus::Down
                );
                Ok(())
            }
        },
    )
    .await
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

    let sandbox = fuchsia_component::client::connect_to_service::<
        fidl_fuchsia_netemul_sandbox::SandboxMarker,
    >()
    .context("failed to connect to sandbox")?;
    let managed_environment = create_netstack_environment::<Netstack2>(&sandbox, name.to_string())
        .context("failed to create netstack environment")?;
    let stack = connect_to_service::<fidl_fuchsia_net_stack::StackMarker>(&managed_environment)
        .context("failed to connect to netstack")?;

    let interfaces = stack.list_interfaces().await.context("failed to list interfaces")?;
    let loopback = interfaces
        .iter()
        .find(|interface| {
            interface.properties.features & fidl_fuchsia_hardware_ethernet::INFO_FEATURE_LOOPBACK
                != 0
        })
        .ok_or(failure::err_msg("failed to find loopback"))?;
    let mut interface_address = fidl_fuchsia_net_stack::InterfaceAddress {
        ip_address: fidl_fuchsia_net::IpAddress::Ipv4(fidl_fuchsia_net::Ipv4Address {
            addr: [1, 1, 1, 1],
        }),
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

    let sandbox = fuchsia_component::client::connect_to_service::<
        fidl_fuchsia_netemul_sandbox::SandboxMarker,
    >()
    .context("failed to connect to sandbox")?;
    let managed_environment = create_netstack_environment::<Netstack2>(&sandbox, name.to_string())
        .context("failed to create netstack environment")?;
    let stack = connect_to_service::<fidl_fuchsia_net_stack::StackMarker>(&managed_environment)
        .context("failed to connect to stack")?;
    let netstack =
        connect_to_service::<fidl_fuchsia_netstack::NetstackMarker>(&managed_environment)
            .context("failed to connect to netstack")?;
    let interfaces = stack.list_interfaces().await.context("failed to list interfaces")?;
    let max_id = interfaces.iter().map(|interface| interface.id).max().unwrap_or(0);
    let mut interface_address = fidl_fuchsia_net_stack::InterfaceAddress {
        ip_address: fidl_fuchsia_net::IpAddress::Ipv4(fidl_fuchsia_net::Ipv4Address {
            addr: [0, 0, 0, 0],
        }),
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
    let sandbox = fuchsia_component::client::connect_to_service::<
        fidl_fuchsia_netemul_sandbox::SandboxMarker,
    >()
    .context("failed to connect to sandbox")?;
    let managed_environment = create_netstack_environment::<N>(&sandbox, name.to_string())
        .context("failed to create netstack environment")?;
    let stack = connect_to_service::<fidl_fuchsia_net_stack::StackMarker>(&managed_environment)
        .context("failed to connect to netstack")?;
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

    let sandbox = fuchsia_component::client::connect_to_service::<
        fidl_fuchsia_netemul_sandbox::SandboxMarker,
    >()
    .context("failed to connect to sandbox")?;
    let managed_environment = create_netstack_environment::<Netstack2>(&sandbox, name.to_string())
        .context("failed to create netstack environment")?;
    let stack = connect_to_service::<fidl_fuchsia_net_stack::StackMarker>(&managed_environment)
        .context("failed to connect to netstack")?;
    let interfaces = stack.list_interfaces().await.context("failed to list interfaces")?;
    let localhost = interfaces
        .iter()
        .find(|interface| {
            interface.properties.features & fidl_fuchsia_hardware_ethernet::INFO_FEATURE_LOOPBACK
                != 0
        })
        .ok_or(failure::err_msg("failed to find loopback interface"))?;
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

// TODO(tamird): could this be done with a single stack and bridged interfaces?
#[fuchsia_async::run_singlethreaded(test)]
async fn acquire_dhcp() -> Result {
    let name = "acquire_dhcp";

    let sandbox = fuchsia_component::client::connect_to_service::<
        fidl_fuchsia_netemul_sandbox::SandboxMarker,
    >()
    .context("failed to connect to sandbox")?;
    let network_context = get_network_context(&sandbox).context("failed to get network context")?;
    let endpoint_manager =
        get_endpoint_manager(&network_context).context("failed to get endpoint manager")?;
    let server_environment =
        create_netstack_environment::<Netstack2>(&sandbox, format!("{}_server", name))
            .context("failed to create server environment")?;
    let server_endpoint_name = "server";
    let server_endpoint = create_endpoint(server_endpoint_name, &endpoint_manager)
        .await
        .context("failed to create endpoint")?;
    let () = server_endpoint.set_link_up(true).await.context("failed to start server endpoint")?;
    {
        let server_device = server_endpoint
            .get_ethernet_device()
            .await
            .context("failed to get server ethernet device")?;
        let server_stack =
            connect_to_service::<fidl_fuchsia_net_stack::StackMarker>(&server_environment)
                .context("failed to connect to server stack")?;
        let id = exec_fidl!(
            server_stack.add_ethernet_interface(name, server_device),
            "failed to add server ethernet interface"
        )?;
        let () = exec_fidl!(
            server_stack.add_interface_address(
                id,
                &mut fidl_fuchsia_net_stack::InterfaceAddress {
                    ip_address: fidl_fuchsia_net::IpAddress::Ipv4(fidl_fuchsia_net::Ipv4Address {
                        addr: [192, 168, 0, 1],
                    }),
                    prefix_len: 24,
                },
            ),
            "failed to add interface address"
        )?;
        let () =
            exec_fidl!(server_stack.enable_interface(id), "failed to enable server interface")?;
    }
    let launcher = {
        let (client, server) = fidl::endpoints::create_proxy::<fidl_fuchsia_sys::LauncherMarker>()
            .context("failed to create launcher proxy")?;
        let () = server_environment.get_launcher(server).context("failed to get launcher")?;
        client
    };
    let _dhcpd = fuchsia_component::client::launch(
        &launcher,
        fuchsia_component::fuchsia_single_component_package_url!("dhcpd").to_string(),
        Some(vec![String::from("--config"), String::from("/pkg/data/test_config.json")]),
    )
    .context("failed to start dhcpd")?;
    let client_environment =
        create_netstack_environment::<Netstack2>(&sandbox, format!("{}_client", name))
            .context("failed to create client environment")?;
    let client_endpoint_name = "client";
    let client_endpoint = create_endpoint(client_endpoint_name, &endpoint_manager)
        .await
        .context("failed to create endpoint")?;
    let () = client_endpoint.set_link_up(true).await.context("failed to start client endpoint")?;

    let network_manager = {
        let (client, server) =
            fidl::endpoints::create_proxy::<fidl_fuchsia_netemul_network::NetworkManagerMarker>()
                .context("failed to create network manager proxy")?;
        let () =
            network_context.get_network_manager(server).context("failed to get network manager")?;
        client
    };

    let (status, network) = network_manager
        .create_network(
            name,
            fidl_fuchsia_netemul_network::NetworkConfig {
                latency: None,
                packet_loss: None,
                reorder: None,
            },
        )
        .await
        .context("failed to create network")?;
    let network = network
        .ok_or(failure::err_msg("failed to create network"))?
        .into_proxy()
        .context("failed to get network proxy")?;
    let () = fuchsia_zircon::Status::ok(status).context("failed to create network")?;
    let status = network
        .attach_endpoint(server_endpoint_name)
        .await
        .context("failed to attach server endpoint")?;
    let () = fuchsia_zircon::Status::ok(status).context("failed to attach server endpoint")?;
    let status = network
        .attach_endpoint(client_endpoint_name)
        .await
        .context("failed to attach client endpoint")?;
    let () = fuchsia_zircon::Status::ok(status).context("failed to attach client endpoint")?;

    {
        let client_device = client_endpoint
            .get_ethernet_device()
            .await
            .context("failed to get client ethernet device")?;
        let client_stack =
            connect_to_service::<fidl_fuchsia_net_stack::StackMarker>(&client_environment)
                .context("failed to connect to client stack")?;
        let id = exec_fidl!(
            client_stack.add_ethernet_interface(name, client_device),
            "failed to add client ethernet interface"
        )?;
        let () =
            exec_fidl!(client_stack.enable_interface(id), "failed to enable client interface")?;
        let client_netstack =
            connect_to_service::<fidl_fuchsia_netstack::NetstackMarker>(&client_environment)
                .context("failed to connect to client netstack")?;
        let (dhcp_client, server_end) =
            fidl::endpoints::create_proxy::<fidl_fuchsia_net_dhcp::ClientMarker>()
                .context("failed to create endpoints for fuchsia.net.dhcp.Client")?;

        let () = client_netstack
            .get_dhcp_client(id.try_into().expect("should fit"), server_end)
            .await
            .context("failed to call client_netstack.get_dhcp_client")?
            .map_err(fuchsia_zircon::Status::from_raw)
            .context("failed to get dhcp client")?;
        let () = dhcp_client
            .start()
            .await
            .context("failed to call dhcp_client.start")?
            .map_err(fuchsia_zircon::Status::from_raw)
            .context("failed to start dhcp client")?;

        let mut address_change_stream = futures::TryStreamExt::try_filter_map(
            client_netstack.take_event_stream(),
            |fidl_fuchsia_netstack::NetstackEvent::OnInterfacesChanged { interfaces }| {
                futures::future::ok(
                    interfaces.into_iter().find(|interface| interface.id as u64 == id).and_then(
                        |interface| match interface.addr {
                            fidl_fuchsia_net::IpAddress::Ipv4(fidl_fuchsia_net::Ipv4Address {
                                addr,
                            }) => {
                                if addr == std::net::Ipv4Addr::UNSPECIFIED.octets() {
                                    None
                                } else {
                                    Some((interface.addr, interface.netmask))
                                }
                            }
                            fidl_fuchsia_net::IpAddress::Ipv6(fidl_fuchsia_net::Ipv6Address {
                                ..
                            }) => None,
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
            .ok_or(failure::err_msg("failed to observe DHCP acquisition"))?
            .context("failed to observe DHCP acquisition")?;
        assert_eq!(
            addr,
            fidl_fuchsia_net::IpAddress::Ipv4(fidl_fuchsia_net::Ipv4Address {
                addr: [192, 168, 0, 2]
            })
        );
        assert_eq!(
            netmask,
            fidl_fuchsia_net::IpAddress::Ipv4(fidl_fuchsia_net::Ipv4Address {
                addr: [255, 255, 255, 128]
            })
        );
    }
    Ok(())
}
