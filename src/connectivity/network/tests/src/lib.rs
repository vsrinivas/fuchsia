// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]
#![deny(warnings)]
#![feature(async_await, await_macro)]

use failure::ResultExt;

type Result = std::result::Result<(), failure::Error>;

fn connect_to_service<S: fidl::endpoints::ServiceMarker>(
    managed_environment: &fidl_fuchsia_netemul_environment::ManagedEnvironmentProxy,
) -> std::result::Result<S::Proxy, failure::Error> {
    let (proxy, server) = fuchsia_zircon::Channel::create()?;
    let () = managed_environment.connect_to_service(S::NAME, server)?;
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

async fn create_endpoint<'a>(
    name: &'static str,
    endpoint_manager: &'a fidl_fuchsia_netemul_network::EndpointManagerProxy,
) -> std::result::Result<fidl_fuchsia_netemul_network::EndpointProxy, failure::Error> {
    let (status, endpoint) = await!(endpoint_manager.create_endpoint(
        name,
        &mut fidl_fuchsia_netemul_network::EndpointConfig {
            mtu: 1500,
            mac: None,
            backing: fidl_fuchsia_netemul_network::EndpointBacking::Ethertap,
        },
    ))
    .context("failed to create endpoint")?;
    let () = fuchsia_zircon::Status::ok(status).context("failed to create endpoint")?;
    let endpoint = endpoint
        .ok_or(failure::err_msg("failed to create endpoint"))?
        .into_proxy()
        .context("failed to get endpoint proxy")?;
    Ok(endpoint)
}

fn create_netstack_environment(
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
                services:  Some([
                    <fidl_fuchsia_netstack::NetstackMarker as fidl::endpoints::ServiceMarker>::NAME,
                    <fidl_fuchsia_net::SocketProviderMarker as fidl::endpoints::ServiceMarker>::NAME,
                    <fidl_fuchsia_net_stack::StackMarker as fidl::endpoints::ServiceMarker>::NAME,
                ]
                    // TODO(tamird): use into_iter after
                    // https://github.com/rust-lang/rust/issues/25725.
                    .iter()
                    .map(std::ops::Deref::deref)
                    .map(str::to_string)
                    .map(|name| fidl_fuchsia_netemul_environment::LaunchService {
                        name,
                        url: fuchsia_component::fuchsia_single_component_package_url!("netstack")
                            .to_string(),
                        arguments: Some(
                            [
                                "--sniff",
                                "--verbosity=debug",
                            ]
                                // TODO(tamird): use into_iter after
                                // https://github.com/rust-lang/rust/issues/25725.
                                .iter()
                                .map(std::ops::Deref::deref)
                                .map(str::to_string).collect()
                        ),
                    })
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

async fn with_netstack_and_device<F, T, S>(name: &'static str, async_fn: T) -> Result
where
    F: futures::Future<Output = Result>,
    T: FnOnce(
        S::Proxy,
        fidl::endpoints::ClientEnd<fidl_fuchsia_hardware_ethernet::DeviceMarker>,
    ) -> F,
    S: fidl::endpoints::ServiceMarker,
{
    let sandbox = fuchsia_component::client::connect_to_service::<
        fidl_fuchsia_netemul_sandbox::SandboxMarker,
    >()
    .context("failed to connect to sandbox")?;
    let network_context = get_network_context(&sandbox).context("failed to get network context")?;
    let endpoint_manager =
        get_endpoint_manager(&network_context).context("failed to get endpoint manager")?;
    let endpoint =
        await!(create_endpoint(name, &endpoint_manager)).context("failed to create endpoint")?;
    let device = await!(endpoint.get_ethernet_device()).context("failed to get ethernet device")?;
    let managed_environment = create_netstack_environment(&sandbox, name.to_string())
        .context("failed to create netstack environment")?;
    let netstack_proxy =
        connect_to_service::<S>(&managed_environment).context("failed to connect to netstack")?;
    await!(async_fn(netstack_proxy, device))
}

#[fuchsia_async::run_singlethreaded(test)]
async fn add_ethernet_device() -> Result {
    let name = stringify!(add_ethernet_device);

    await!(with_netstack_and_device::<_, _, fidl_fuchsia_netstack::NetstackMarker>(
        name,
        async move |netstack, device| -> Result {
            let id = await!(netstack.add_ethernet_device(
                name,
                &mut fidl_fuchsia_netstack::InterfaceConfig {
                    name: name.to_string(),
                    metric: 0,
                    ip_address_config: fidl_fuchsia_netstack::IpAddressConfig::Dhcp(true),
                },
                device,
            ))
            .context("failed to add ethernet device")?;
            let interface = await!(netstack.get_interfaces2())
                .context("failed to get interfaces")?
                .into_iter()
                .find(|interface| interface.id == id)
                .ok_or(failure::err_msg("failed to find added ethernet device"))?;
            assert_eq!(
                interface.features & fidl_fuchsia_hardware_ethernet::INFO_FEATURE_LOOPBACK,
                0
            );
            assert_eq!(interface.flags & fidl_fuchsia_netstack::NET_INTERFACE_FLAG_UP, 0);
            Ok(())
        },
    ))
}

#[fuchsia_async::run_singlethreaded(test)]
async fn add_ethernet_interface() -> Result {
    let name = stringify!(add_ethernet_interface);

    await!(with_netstack_and_device::<_, _, fidl_fuchsia_net_stack::StackMarker>(
        name,
        async move |stack, device| -> Result {
            let (error, id) = await!(stack.add_ethernet_interface(name, device))
                .context("failed to add ethernet interface")?;
            assert_eq!(error, None);
            let interface = await!(stack.list_interfaces())
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
        },
    ))
}

#[fuchsia_async::run_singlethreaded(test)]
async fn add_del_interface_address() -> Result {
    let name = stringify!(add_del_interface_address);

    let sandbox = fuchsia_component::client::connect_to_service::<
        fidl_fuchsia_netemul_sandbox::SandboxMarker,
    >()
    .context("failed to connect to sandbox")?;
    let managed_environment = create_netstack_environment(&sandbox, name.to_string())
        .context("failed to create netstack environment")?;
    let stack = connect_to_service::<fidl_fuchsia_net_stack::StackMarker>(&managed_environment)
        .context("failed to connect to netstack")?;

    let interfaces = await!(stack.list_interfaces()).context("failed to list interfaces")?;
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
    let error = await!(stack.add_interface_address(loopback.id, &mut interface_address))
        .context("failed to call add interface address")?;
    assert_eq!(error.as_ref(), None);
    let (loopback, error) = await!(stack.get_interface_info(loopback.id))
        .context("failed to get loopback interface")?;
    assert_eq!(error.as_ref(), None);
    let loopback = loopback.ok_or(failure::err_msg("failed to find loopback"))?;

    assert!(
        loopback.properties.addresses.iter().find(|addr| *addr == &interface_address).is_some(),
        "couldn't find {:#?} in {:#?}",
        interface_address,
        loopback.properties.addresses
    );

    let error = await!(stack.del_interface_address(loopback.id, &mut interface_address))
        .context("failed to call del interface address")?;
    assert_eq!(error.as_ref(), None);
    let (loopback, error) = await!(stack.get_interface_info(loopback.id))
        .context("failed to get loopback interface")?;
    assert_eq!(error.as_ref(), None);
    let loopback = loopback.ok_or(failure::err_msg("failed to find loopback"))?;

    assert!(
        loopback.properties.addresses.iter().find(|addr| *addr == &interface_address).is_none(),
        "did not expect to find {:#?} in {:#?}",
        interface_address,
        loopback.properties.addresses
    );

    Ok(())
}

#[fuchsia_async::run_singlethreaded(test)]
async fn add_interface_address_errors() -> Result {
    let name = stringify!(add_interface_address_errors);

    let sandbox = fuchsia_component::client::connect_to_service::<
        fidl_fuchsia_netemul_sandbox::SandboxMarker,
    >()
    .context("failed to connect to sandbox")?;
    let managed_environment = create_netstack_environment(&sandbox, name.to_string())
        .context("failed to create netstack environment")?;
    let stack = connect_to_service::<fidl_fuchsia_net_stack::StackMarker>(&managed_environment)
        .context("failed to connect to netstack")?;
    let interfaces = await!(stack.list_interfaces()).context("failed to list interfaces")?;
    let max_id = interfaces.iter().map(|interface| interface.id).max().unwrap_or(0);
    let mut interface_address = fidl_fuchsia_net_stack::InterfaceAddress {
        ip_address: fidl_fuchsia_net::IpAddress::Ipv4(fidl_fuchsia_net::Ipv4Address {
            addr: [0, 0, 0, 0],
        }),
        prefix_len: 0,
    };

    // NET-2234 (crash on interface not found).
    let error = await!(stack.add_interface_address(max_id + 1, &mut interface_address))
        .context("failed to call add interface address")?
        .ok_or(failure::err_msg("failed to get add interface address error"))?;
    assert_eq!(
        error.as_ref(),
        &fidl_fuchsia_net_stack::Error { type_: fidl_fuchsia_net_stack::ErrorType::NotFound }
    );

    // NET-2334 (crash on invalid prefix length).
    interface_address.prefix_len = 43;
    let error = await!(stack.add_interface_address(max_id, &mut interface_address))
        .context("failed to call add interface address")?
        .ok_or(failure::err_msg("failed to get add interface address error"))?;
    assert_eq!(
        error.as_ref(),
        &fidl_fuchsia_net_stack::Error { type_: fidl_fuchsia_net_stack::ErrorType::BadState }
    );

    Ok(())
}

#[fuchsia_async::run_singlethreaded(test)]
async fn get_interface_info_not_found() -> Result {
    let name = stringify!(get_interface_info_not_found);

    let sandbox = fuchsia_component::client::connect_to_service::<
        fidl_fuchsia_netemul_sandbox::SandboxMarker,
    >()
    .context("failed to connect to sandbox")?;
    let managed_environment = create_netstack_environment(&sandbox, name.to_string())
        .context("failed to create netstack environment")?;
    let stack = connect_to_service::<fidl_fuchsia_net_stack::StackMarker>(&managed_environment)
        .context("failed to connect to netstack")?;
    let interfaces = await!(stack.list_interfaces()).context("failed to list interfaces")?;
    let max_id = interfaces.iter().map(|interface| interface.id).max().unwrap_or(0);
    let (info, error) = await!(stack.get_interface_info(max_id + 1))
        .context("failed to call get interface info")?;
    assert_eq!(info, None);
    let error = error.ok_or(failure::err_msg("failed to get get interface info error"))?;
    assert_eq!(
        error.as_ref(),
        &fidl_fuchsia_net_stack::Error { type_: fidl_fuchsia_net_stack::ErrorType::NotFound }
    );
    Ok(())
}

#[fuchsia_async::run_singlethreaded(test)]
async fn disable_interface_loopback() -> Result {
    let name = stringify!(disable_interface_loopback);

    let sandbox = fuchsia_component::client::connect_to_service::<
        fidl_fuchsia_netemul_sandbox::SandboxMarker,
    >()
    .context("failed to connect to sandbox")?;
    let managed_environment = create_netstack_environment(&sandbox, name.to_string())
        .context("failed to create netstack environment")?;
    let stack = connect_to_service::<fidl_fuchsia_net_stack::StackMarker>(&managed_environment)
        .context("failed to connect to netstack")?;
    let interfaces = await!(stack.list_interfaces()).context("failed to list interfaces")?;
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
    assert_eq!(
        await!(stack.disable_interface(localhost.id)).context("failed to disable interface")?,
        None
    );
    let (info, error) =
        await!(stack.get_interface_info(localhost.id)).context("failed to get interface info")?;
    assert_eq!(error, None);
    assert_eq!(
        info.ok_or(failure::err_msg("expected interface info to be present"))?
            .properties
            .administrative_status,
        fidl_fuchsia_net_stack::AdministrativeStatus::Disabled
    );
    Ok(())
}

// TODO(tamird): could this be done with a single stack and bridged interfaces?
#[fuchsia_async::run_singlethreaded(test)]
async fn acquire_dhcp() -> Result {
    let name = stringify!(acquire_dhcp);

    let sandbox = fuchsia_component::client::connect_to_service::<
        fidl_fuchsia_netemul_sandbox::SandboxMarker,
    >()
    .context("failed to connect to sandbox")?;
    let network_context = get_network_context(&sandbox).context("failed to get network context")?;
    let endpoint_manager =
        get_endpoint_manager(&network_context).context("failed to get endpoint manager")?;
    let server_environment = create_netstack_environment(&sandbox, format!("{}_server", name))
        .context("failed to create server environment")?;
    let server_endpoint_name = "server";
    let server_endpoint = await!(create_endpoint(server_endpoint_name, &endpoint_manager))
        .context("failed to create endpoint")?;
    let () =
        await!(server_endpoint.set_link_up(true)).context("failed to start server endpoint")?;
    {
        let server_device = await!(server_endpoint.get_ethernet_device())
            .context("failed to get server ethernet device")?;
        let server_stack =
            connect_to_service::<fidl_fuchsia_net_stack::StackMarker>(&server_environment)
                .context("failed to connect to server stack")?;
        let (error, id) = await!(server_stack.add_ethernet_interface(name, server_device))
            .context("failed to add server ethernet interface")?;
        assert_eq!(error, None);
        let error = await!(server_stack.add_interface_address(
            id,
            &mut fidl_fuchsia_net_stack::InterfaceAddress {
                ip_address: fidl_fuchsia_net::IpAddress::Ipv4(fidl_fuchsia_net::Ipv4Address {
                    addr: [192, 168, 0, 1]
                }),
                prefix_len: 24,
            }
        ))
        .context("failed to add interface address")?;
        assert_eq!(error, None);
        let error = await!(server_stack.enable_interface(id))
            .context("failed to enable server interface")?;
        assert_eq!(error, None);
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
        None,
    )
    .context("failed to start dhcpd")?;
    let client_environment = create_netstack_environment(&sandbox, format!("{}_client", name))
        .context("failed to create client environment")?;
    let client_endpoint_name = "client";
    let client_endpoint = await!(create_endpoint(client_endpoint_name, &endpoint_manager))
        .context("failed to create endpoint")?;
    let () =
        await!(client_endpoint.set_link_up(true)).context("failed to start client endpoint")?;

    let network_manager = {
        let (client, server) =
            fidl::endpoints::create_proxy::<fidl_fuchsia_netemul_network::NetworkManagerMarker>()
                .context("failed to create network manager proxy")?;
        let () =
            network_context.get_network_manager(server).context("failed to get network manager")?;
        client
    };

    let (status, network) = await!(network_manager.create_network(
        name,
        fidl_fuchsia_netemul_network::NetworkConfig {
            latency: None,
            packet_loss: None,
            reorder: None,
        },
    ))
    .context("failed to create network")?;
    let network = network
        .ok_or(failure::err_msg("failed to create network"))?
        .into_proxy()
        .context("failed to get network proxy")?;
    let () = fuchsia_zircon::Status::ok(status).context("failed to create network")?;
    let status = await!(network.attach_endpoint(server_endpoint_name))
        .context("failed to attach server endpoint")?;
    let () = fuchsia_zircon::Status::ok(status).context("failed to attach server endpoint")?;
    let status = await!(network.attach_endpoint(client_endpoint_name))
        .context("failed to attach client endpoint")?;
    let () = fuchsia_zircon::Status::ok(status).context("failed to attach client endpoint")?;

    {
        let client_device = await!(client_endpoint.get_ethernet_device())
            .context("failed to get client ethernet device")?;
        let client_stack =
            connect_to_service::<fidl_fuchsia_net_stack::StackMarker>(&client_environment)
                .context("failed to connect to client stack")?;
        let (error, id) = await!(client_stack.add_ethernet_interface(name, client_device))
            .context("failed to add client ethernet interface")?;
        assert_eq!(error, None);
        let error = await!(client_stack.enable_interface(id))
            .context("failed to enable client interface")?;
        assert_eq!(error, None);
        let client_netstack =
            connect_to_service::<fidl_fuchsia_netstack::NetstackMarker>(&client_environment)
                .context("failed to connect to client netstack")?;
        let error = await!(client_netstack.set_dhcp_client_status(id as u32, true))
            .context("failed to set DHCP client status")?;
        assert_eq!(error.status, fidl_fuchsia_netstack::Status::Ok, "{}", error.message);

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
            fuchsia_zircon::Time::after(fuchsia_zircon::Duration::from_seconds(60)),
            || None,
        );
        let (addr, netmask) = await!(address_change)
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
                addr: [255, 255, 255, 0]
            })
        );
    }
    Ok(())
}
