// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![cfg(test)]

extern crate network_manager_cli_lib as network_manager_cli;

use anyhow::{Context as _, Error};
use fidl::endpoints::{create_proxy, ServiceMarker};
use fidl_fuchsia_netemul_environment::{
    EnvironmentOptions, LaunchService, LoggerOptions, ManagedEnvironmentMarker,
    ManagedEnvironmentProxy,
};
use fidl_fuchsia_netemul_network::{
    EndpointConfig, EndpointManagerMarker, EndpointManagerProxy, EndpointProxy,
    NetworkContextMarker, NetworkContextProxy,
};
use fidl_fuchsia_netemul_sandbox::{SandboxMarker, SandboxProxy};
use fidl_fuchsia_router_config::{
    RouterAdminMarker, RouterAdminProxy, RouterStateMarker, RouterStateProxy,
};
use fuchsia_async as fasync;
use fuchsia_component::client::connect_to_service;
use fuchsia_component::fuchsia_single_component_package_url as component_url;
use network_manager_cli::cli::{make_cmd, run_cmd};
use network_manager_cli::printer::Printer;
use regex::Regex;
use std::future::Future;
use std::pin::Pin;
use std::str;

#[derive(Debug)]
struct Test {
    command: String,
    expected: String,
    description: String,
}

impl Test {
    fn new<A: Into<String>, B: Into<String>>(command: A, expected: B, description: A) -> Self {
        Self { command: command.into(), expected: expected.into(), description: description.into() }
    }
}

#[derive(Debug)]
struct TestSuite {
    tests: Vec<Test>,
}

macro_rules! test_suite {
    [$( ($cmd:expr,$expected:expr,$description:expr), )*] => {
        TestSuite {
            tests: vec![ $(Test::new($cmd,$expected,$description),)*]
        }
    };
}

fn generate_launch_services() -> Vec<LaunchService> {
    let names_and_urls = vec![
        ("fuchsia.net.stack.Stack", component_url!("netstack"), Vec::new()),
        ("fuchsia.net.NameLookup", component_url!("netstack"), Vec::new()),
        ("fuchsia.netstack.Netstack", component_url!("netstack"), Vec::new()),
        ("fuchsia.net.name.LookupAdmin", component_url!("netstack"), Vec::new()),
        ("fuchsia.posix.socket.Provider", component_url!("netstack"), Vec::new()),
        ("fuchsia.net.filter.Filter", component_url!("netstack"), Vec::new()),
        (
            "fuchsia.router.config.RouterAdmin",
            component_url!("network-manager"),
            vec!["--devicepath", "vdev"],
        ),
        (
            "fuchsia.router.config.RouterState",
            component_url!("network-manager"),
            vec!["--devicepath", "vdev"],
        ),
    ];
    names_and_urls
        .into_iter()
        .map(|(name, url, args)| LaunchService {
            name: name.to_string(),
            url: url.to_string(),
            arguments: args.into_iter().map(str::to_string).collect(),
        })
        .collect()
}

fn get_network_context(sandbox: &SandboxProxy) -> Result<NetworkContextProxy, Error> {
    let (client, server) = fidl::endpoints::create_proxy::<NetworkContextMarker>()
        .context("failed to create network context proxy")?;
    let () = sandbox.get_network_context(server).context("failed to get network context")?;
    Ok(client)
}

fn get_endpoint_manager(
    network_context: &NetworkContextProxy,
) -> Result<EndpointManagerProxy, anyhow::Error> {
    let (client, server) = fidl::endpoints::create_proxy::<EndpointManagerMarker>()
        .context("failed to create endpoint manager proxy")?;
    let () =
        network_context.get_endpoint_manager(server).context("failed to get endpoint manager")?;
    Ok(client)
}

async fn create_endpoint(
    name: &'static str,
    endpoint_manager: &EndpointManagerProxy,
) -> std::result::Result<EndpointProxy, anyhow::Error> {
    let (status, endpoint) = endpoint_manager
        .create_endpoint(
            name,
            &mut EndpointConfig {
                mtu: 1500,
                mac: None,
                backing: fidl_fuchsia_netemul_network::EndpointBacking::Ethertap,
            },
        )
        .await
        .context("failed to create endpoint")?;
    let () = fuchsia_zircon::Status::ok(status).context("failed to create endpoint")?;
    let endpoint = endpoint
        .ok_or(anyhow::format_err!("failed to create endpoint"))?
        .into_proxy()
        .context("failed to get endpoint proxy")?;
    endpoint.set_link_up(true).await?;
    Ok(endpoint)
}

fn create_managed_env(sandbox: &SandboxProxy) -> Result<ManagedEnvironmentProxy, Error> {
    let (env, env_server_end) = create_proxy::<ManagedEnvironmentMarker>()?;
    let services = generate_launch_services();
    sandbox.create_environment(
        env_server_end,
        EnvironmentOptions {
            name: Some(String::from("network_manager_env")),
            services: Some(services),
            devices: None,
            inherit_parent_launch_services: None,
            logger_options: Some(LoggerOptions {
                enabled: Some(true),
                klogs_enabled: Some(false),
                filter_options: None,
                syslog_output: Some(true),
            }),
        },
    )?;
    Ok(env)
}

fn connect_to_sandbox_service<S: fidl::endpoints::ServiceMarker>(
    managed_environment: &ManagedEnvironmentProxy,
) -> Result<S::Proxy, anyhow::Error> {
    let (proxy, server) = fuchsia_zircon::Channel::create()?;
    let () = managed_environment.connect_to_service(S::NAME, server)?;
    let proxy = fuchsia_async::Channel::from_channel(proxy)?;
    Ok(<S::Proxy as fidl::endpoints::Proxy>::from_channel(proxy))
}

fn connect_network_manager(
    env: &ManagedEnvironmentProxy,
) -> Result<(RouterAdminProxy, RouterStateProxy), Error> {
    let (router_admin, router_admin_server_end) = create_proxy::<RouterAdminMarker>()?;
    env.connect_to_service(RouterAdminMarker::NAME, router_admin_server_end.into_channel())
        .context("Can't connect to network_manager admin endpoint")?;
    let (router_state, router_state_server_end) = create_proxy::<RouterStateMarker>()?;
    env.connect_to_service(RouterStateMarker::NAME, router_state_server_end.into_channel())
        .context("Can't connect to network_manager state endpoint")?;
    Ok((router_admin, router_state))
}

async fn add_ethernet_device(
    netstack_proxy: &fidl_fuchsia_netstack::NetstackProxy,
    device: fidl::endpoints::ClientEnd<fidl_fuchsia_hardware_ethernet::DeviceMarker>,
    name: &str,
) -> Result<(), Error> {
    let device_name = format!("/mock/{}", name);
    let id = netstack_proxy
        .add_ethernet_device(
            &device_name,
            &mut fidl_fuchsia_netstack::InterfaceConfig {
                name: device_name.to_string(),
                filepath: format!("/fake/filepath/for_test/{}", name),
                metric: 0,
            },
            device,
        )
        .await
        .context("failed to add ethernet device")?
        .map_err(fuchsia_zircon::Status::from_raw)
        .context("add_ethernet_device failed")?;

    // Check that the newly added ethernet interface is present before continuing with the
    // actual tests.
    let interface = netstack_proxy
        .get_interfaces2()
        .await
        .expect("failed to get interfaces")
        .into_iter()
        .find(|interface| interface.id == id)
        .ok_or(anyhow::format_err!("failed to find added ethernet device"))
        .unwrap();
    assert!(
        !interface.features.contains(fidl_fuchsia_hardware_ethernet::Features::Loopback),
        "unexpected interface features: ({:b}).contains({:b})",
        interface.features,
        fidl_fuchsia_hardware_ethernet::Features::Loopback
    );
    assert_eq!(interface.flags & fidl_fuchsia_netstack::NET_INTERFACE_FLAG_UP, 0);
    Ok(())
}

async fn exec_cmd(env: &ManagedEnvironmentProxy, command: &str) -> String {
    let (router_admin, router_state) =
        connect_network_manager(&env).expect("Failed to connect from managed environment");

    let mut printer = Printer::new(Vec::new());
    let cmd = match make_cmd(command) {
        Ok(cmd) => cmd,
        Err(e) => return format!("{:?}", e),
    };

    let actual_output;
    actual_output = match run_cmd(cmd, router_admin, router_state, &mut printer).await {
        Ok(_) =>
        // TODO(cgibson): Figure out a way to add this as a helper method on `Printer`.
        {
            str::from_utf8(&printer.into_inner().unwrap()).unwrap().to_string()
        }

        Err(e) => format!("{:?}", e),
    };
    actual_output
}

fn execute_test_suite<'a>(
    router: &'a Device,
    commands: TestSuite,
) -> Pin<Box<dyn Future<Output = ()> + 'a>> {
    // Boxing is needed so we don't overflow the stack when running in
    // panic=abort mode with the default Fuchsia stack size.
    Box::pin(async move {
        for test in commands.tests {
            let actual_output = exec_cmd(&router.env, &test.command).await;
            println!("command: {} - {}", test.command, test.description);
            let re = Regex::new(&test.expected)
                .unwrap_or_else(|e| panic!("Error parsing expected regex {:#?}: {}", test, e));
            assert!(
                re.is_match(&actual_output),
                "\nTest suite failed.\nStep: {}\nCommand: {}\nExpected regex: {:?}\nActual output: {:?}\n",
                test.description,
                test.command,
                test.expected,
                actual_output
            );
        }
    })
}

struct Device {
    env: ManagedEnvironmentProxy,
    _sandbox: SandboxProxy,
    endpoints: Vec<EndpointProxy>,
}

async fn test_device() -> Device {
    let sandbox = connect_to_service::<SandboxMarker>().expect("Can't connect to sandbox");
    let network_context = get_network_context(&sandbox).expect("failed to get network context");
    let endpoint_manager =
        get_endpoint_manager(&network_context).expect("failed to get endpoint manager");
    let env = create_managed_env(&sandbox).expect("Failed to create environment with services");
    let netstack_proxy = connect_to_sandbox_service::<fidl_fuchsia_netstack::NetstackMarker>(&env)
        .expect("failed to connect to netstack");

    let mut endpoints = Vec::new();

    for name in [stringify!(port1), stringify!(port2), stringify!(port3)].iter() {
        let endpoint =
            create_endpoint(name, &endpoint_manager).await.expect("failed to create endpoint");
        match endpoint.get_device().await.expect("failed to get ethernet device") {
            fidl_fuchsia_netemul_network::DeviceConnection::Ethernet(device) => {
                add_ethernet_device(&netstack_proxy, device, name)
                    .await
                    .expect("error adding ethernet device")
            }
            fidl_fuchsia_netemul_network::DeviceConnection::NetworkDevice(device) => {
                // We specifically create an Ethertap endpoint in `create_endpoint`.
                panic!("Got unexpected NetworkDevice connection {:?}", device);
            }
        };
        endpoints.push(endpoint);
    }
    Device { env, _sandbox: sandbox, endpoints }
}

#[fasync::run_singlethreaded]
#[test]
async fn test_commands() {
    let device = test_device().await;
    execute_test_suite(&device, test_suite![
        // ("command to test", "expected regex match statement"),
        ("show dhcpconfig 0",
         "Id \\{ uuid: \\[0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}",""),
         ("show dnsconfig",
          "DnsResolverConfig \\{ element: Id \\{ uuid: \\[0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, search: DnsSearch \\{ servers: \\[\\], domain_name: None \\}, policy: Static \\}",
          "Get DNS config"),
         ("show filterstate", "0 filters installed.*", ""),
         ("show forwardstate 0",
          "Id \\{ uuid: \\[0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}",""),
          ("show lanconfig 0",
           "Id \\{ uuid: \\[0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\} Response: \\(LanProperties \\{ address_v4: None, enable_dhcp_server: None, dhcp_config: None, address_v6: None, enable_dns_forwarder: None, enable: None \\}, Some\\(Error \\{ code: NotSupported, description: None \\}\\)",""),
           ("show ports",
            "Port \\{ element: Id \\{ uuid: \\[2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 2, path: \"/mock/port1\" \\}\nPort \\{ element: Id \\{ uuid: \\[3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 3, path: \"/mock/port2\" \\}\nPort \\{ element: Id \\{ uuid: \\[4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 4, path: \"/mock/port3\" \\}\nPort \\{ element: Id \\{ uuid: \\[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 1, path: \"loopback\" \\}\n",
            ""),
            ("show routes", "Response: \\[\\]",""),
            ("show wans", "Response: \\[\\]",""),
    ]).await;
}

#[fasync::run_singlethreaded]
#[test]
async fn test_add_wan() {
    // The pair is constructed of the CLI command to test, and it's expected output as a
    // regular expression parsable by the Regex crate.
    let error_message =
        "Response: \\(None, Some\\(Error \\{ code: AlreadyExists, description: None \\}\\)\\)";

    let device = test_device().await;
    execute_test_suite(&device, test_suite![
        // ("command to test", "expected regex match statement"),
        // There should be two ports.
        ("show ports",
         "Port \\{ element: Id \\{ uuid: \\[2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 2, path: \"/mock/port1\" \\}\nPort \\{ element: Id \\{ uuid: \\[3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 3, path: \"/mock/port2\" \\}\nPort \\{ element: Id \\{ uuid: \\[4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 4, path: \"/mock/port3\" \\}\nPort \\{ element: Id \\{ uuid: \\[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 1, path: \"loopback\" \\}\n",
         "test starts with three ports present."),
        ("show wans", "Response: \\[\\]\n","There should be no WANs created yet."),
        (" add wan wan1 --ports 2 3", error_message,"add wan interface with two ports; should fail."),
        (" add wan wan1 --ports 2", "Response: \\(Some\\(Id \\{ uuid: \\[5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 1 \\}\\), None\\)\n","add wan interface using existing port; should succeed."),
        (" add wan wan1 --ports 5", error_message,"add wan interface using not existing port; should fail."),
        (" add wan wan1 --ports 2", error_message,"add wan interface using already used port; should fail."),
        (" add wan wan1 --ports 3", error_message,"add wan interface using name currently in use; should fail."),
        (" add wan wan2 --ports 3", "Response: \\(Some\\(Id \\{ uuid: \\[7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 2 \\}\\), None\\)\n","add second wan interface using existing port; should succeed."),
        ("show wans",
         "Response: \\[Lif \\{ element: Some\\(Id \\{ uuid: \\[5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 1 \\}\\), type_: Some\\(Wan\\), name: Some\\(\"wan1\"\\), port_ids: Some\\(\\[2\\]\\), vlan: Some\\(0\\), properties: Some\\(Wan\\(WanProperties \\{ connection_type: Some\\(Direct\\), connection_parameters: None, address_method: Some\\(Manual\\), address_v4: None, gateway_v4: None, connection_v6_mode: Some\\(Passthrough\\), address_v6: None, gateway_v6: None, hostname: None, clone_mac: None, mtu: None, enable: Some\\(false\\), metric: None \\}\\)\\) \\}, Lif \\{ element: Some\\(Id \\{ uuid: \\[7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 2 \\}\\), type_: Some\\(Wan\\), name: Some\\(\"wan2\"\\), port_ids: Some\\(\\[3\\]\\), vlan: Some\\(0\\), properties: Some\\(Wan\\(WanProperties \\{ connection_type: Some\\(Direct\\), connection_parameters: None, address_method: Some\\(Manual\\), address_v4: None, gateway_v4: None, connection_v6_mode: Some\\(Passthrough\\), address_v6: None, gateway_v6: None, hostname: None, clone_mac: None, mtu: None, enable: Some\\(false\\), metric: None \\}\\)\\) \\}\\]\n",
         "There should be two WAN interfaces."),
    ]).await;
}

#[fasync::run_singlethreaded]
#[test]
async fn test_remove_wan() {
    let device = test_device().await;
    execute_test_suite(&device, test_suite![
        // ("command to test", "expected regex match statement", "comment"),
        ("show ports",
         "Port \\{ element: Id \\{ uuid: \\[2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 2, path: \"/mock/port1\" \\}\nPort \\{ element: Id \\{ uuid: \\[3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 3, path: \"/mock/port2\" \\}\nPort \\{ element: Id \\{ uuid: \\[4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 4, path: \"/mock/port3\" \\}\nPort \\{ element: Id \\{ uuid: \\[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 1, path: \"loopback\" \\}\n",
         "test starts with three ports present."),
        ("show wans", "Response: \\[\\]\n","There should be no WANs created yet."),
        (" add wan wan1 --ports 2", "Response: \\(Some\\(Id \\{ uuid: \\[5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 1 \\}\\), None\\)\n","add wan interface using existing port; should succeed."),
        (" add wan wan2 --ports 4", "Response: \\(Some\\(Id \\{ uuid: \\[6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 2 \\}\\), None\\)\n","add wan interface using existing port; should succeed."),
        ("show wans",
         "Response: \\[Lif \\{ element: Some\\(Id \\{ uuid: \\[5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 1 \\}\\), type_: Some\\(Wan\\), name: Some\\(\"wan1\"\\), port_ids: Some\\(\\[2\\]\\), vlan: Some\\(0\\), properties: Some\\(Wan\\(WanProperties \\{ connection_type: Some\\(Direct\\), connection_parameters: None, address_method: Some\\(Manual\\), address_v4: None, gateway_v4: None, connection_v6_mode: Some\\(Passthrough\\), address_v6: None, gateway_v6: None, hostname: None, clone_mac: None, mtu: None, enable: Some\\(false\\), metric: None \\}\\)\\) \\}, Lif \\{ element: Some\\(Id \\{ uuid: \\[6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 2 \\}\\), type_: Some\\(Wan\\), name: Some\\(\"wan2\"\\), port_ids: Some\\(\\[4\\]\\), vlan: Some\\(0\\), properties: Some\\(Wan\\(WanProperties \\{ connection_type: Some\\(Direct\\), connection_parameters: None, address_method: Some\\(Manual\\), address_v4: None, gateway_v4: None, connection_v6_mode: Some\\(Passthrough\\), address_v6: None, gateway_v6: None, hostname: None, clone_mac: None, mtu: None, enable: Some\\(false\\), metric: None \\}\\)\\) \\}\\]\n",
         "There should be two WAN interfaces."),
        (" remove wan 7",
         "NotFound",
         "remove non existing wan"),
        (" remove wan 5",
         "Response: None\n",
          "remove first wan"),
        ("show wans",
         "Response: \\[Lif \\{ element: Some\\(Id \\{ uuid: \\[6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 2 \\}\\), type_: Some\\(Wan\\), name: Some\\(\"wan2\"\\), port_ids: Some\\(\\[4\\]\\), vlan: Some\\(0\\), properties: Some\\(Wan\\(WanProperties \\{ connection_type: Some\\(Direct\\), connection_parameters: None, address_method: Some\\(Manual\\), address_v4: None, gateway_v4: None, connection_v6_mode: Some\\(Passthrough\\), address_v6: None, gateway_v6: None, hostname: None, clone_mac: None, mtu: None, enable: Some\\(false\\), metric: None \\}\\)\\) \\}\\]\n",
          "verify there is only one wan"),
    ]).await;
}

#[fasync::run_singlethreaded]
#[test]
async fn test_add_lan() {
    let error_message =
        "Response: \\(None, Some\\(Error \\{ code: AlreadyExists, description: None \\}\\)\\)";

    let device = test_device().await;
    execute_test_suite(&device, test_suite![
        // ("command to test", "expected regex match statement", "comment"),
        ("show ports",
         "Port \\{ element: Id \\{ uuid: \\[2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 2, path: \"/mock/port1\" \\}\nPort \\{ element: Id \\{ uuid: \\[3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 3, path: \"/mock/port2\" \\}\nPort \\{ element: Id \\{ uuid: \\[4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 4, path: \"/mock/port3\" \\}\nPort \\{ element: Id \\{ uuid: \\[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 1, path: \"loopback\" \\}\n",
         "test starts with three ports present."),
        ("show lans", "Response: \\[\\]\n","There should be no LANs created yet."),
        (" add lan lan1 --ports 2", "Response: \\(Some\\(Id \\{ uuid: \\[5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 1 \\}\\), None\\)\n","add lan interface using existing port; should succeed."),
        (" add lan lan11 --ports 2", error_message,"add lan interface using port in use; should fail."),
        (" add lan lan12 --ports 2", error_message,"add lan interface using same used port; should still fail."),
        (" add lan lan1 --ports 3", error_message,"add lan interface using name currently in use; should fail."),
        (" add lan lan2 --ports 2 3", error_message,"add lan interface with two ports, one of them in use; should fail."),
        (" add lan lan2 --ports 2 3", error_message,"again add lan interface with two ports, one of them in use; should still fail."),
        (" add lan lan2 --ports 3 5", error_message,"add lan interface with two ports, one of them invalid; should fail."),
        (" add lan lan2 --ports 3 4", "Response: \\(Some\\(Id \\{ uuid: \\[7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 2 \\}\\), None\\)\n","add lan interface with two valid ports; should succeed."),
        (" add lan lan3 --ports 3", error_message,"port3 is in use, it should fail."),
        ("show lans",
         "Response: \\[Lif \\{ element: Some\\(Id \\{ uuid: \\[5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 1 \\}\\), type_: Some\\(Lan\\), name: Some\\(\"lan1\"\\), port_ids: Some\\(\\[2\\]\\), vlan: Some\\(0\\), properties: Some\\(Lan\\(LanProperties \\{ address_v4: None, enable_dhcp_server: Some\\(false\\), dhcp_config: None, address_v6: None, enable_dns_forwarder: Some\\(false\\), enable: Some\\(false\\) \\}\\)\\) \\}, Lif \\{ element: Some\\(Id \\{ uuid: \\[7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 2 \\}\\), type_: Some\\(Lan\\), name: Some\\(\"lan2\"\\), port_ids: Some\\(\\[3, 4\\]\\), vlan: Some\\(0\\), properties: Some\\(Lan\\(LanProperties \\{ address_v4: None, enable_dhcp_server: Some\\(false\\), dhcp_config: None, address_v6: None, enable_dns_forwarder: Some\\(false\\), enable: Some\\(false\\) \\}\\)\\) \\}\\]\n",
         "There should be two LAN interfaces."),
        ("show ports",
         "Port \\{ element: Id \\{ uuid: \\[2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 2, path: \"/mock/port1\" \\}\nPort \\{ element: Id \\{ uuid: \\[3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 3, path: \"/mock/port2\" \\}\nPort \\{ element: Id \\{ uuid: \\[4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 4, path: \"/mock/port3\" \\}\nPort \\{ element: Id \\{ uuid: \\[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 1, path: \"loopback\" \\}\n",
         "test starts with three ports present."),
    ]).await;
}

#[fasync::run_singlethreaded]
#[test]
async fn test_remove_lan() {
    let device = test_device().await;
    execute_test_suite(&device, test_suite![
        // ("command to test", "expected regex match statement", "comment"),
        ("show ports",
         "Port \\{ element: Id \\{ uuid: \\[2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 2, path: \"/mock/port1\" \\}\nPort \\{ element: Id \\{ uuid: \\[3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 3, path: \"/mock/port2\" \\}\nPort \\{ element: Id \\{ uuid: \\[4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 4, path: \"/mock/port3\" \\}\nPort \\{ element: Id \\{ uuid: \\[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 1, path: \"loopback\" \\}\n",
         "test starts with three ports present."),
        ("show lans", "Response: \\[\\]\n","There should be no LANs created yet."),
        (" add lan lan1 --ports 2", "Response: \\(Some\\(Id \\{ uuid: \\[5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 1 \\}\\), None\\)\n","add lan interface using existing port; should succeed."),
        (" add lan lan2 --ports 4", "Response: \\(Some\\(Id \\{ uuid: \\[6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 2 \\}\\), None\\)\n","add lan interface using existing port; should succeed."),
        ("show lans",
         "Response: \\[Lif \\{ element: Some\\(Id \\{ uuid: \\[5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 1 \\}\\), type_: Some\\(Lan\\), name: Some\\(\"lan1\"\\), port_ids: Some\\(\\[2\\]\\), vlan: Some\\(0\\), properties: Some\\(Lan\\(LanProperties \\{ address_v4: None, enable_dhcp_server: Some\\(false\\), dhcp_config: None, address_v6: None, enable_dns_forwarder: Some\\(false\\), enable: Some\\(false\\) \\}\\)\\) \\}, Lif \\{ element: Some\\(Id \\{ uuid: \\[6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 2 \\}\\), type_: Some\\(Lan\\), name: Some\\(\"lan2\"\\), port_ids: Some\\(\\[4\\]\\), vlan: Some\\(0\\), properties: Some\\(Lan\\(LanProperties \\{ address_v4: None, enable_dhcp_server: Some\\(false\\), dhcp_config: None, address_v6: None, enable_dns_forwarder: Some\\(false\\), enable: Some\\(false\\) \\}\\)\\) \\}\\]\n",
         "There should be two LAN interfaces."),
        (" remove lan 7",
         "NotFound",
         "remove non existing lan"),
        (" remove lan 5",
         "Response: None\n",
          "remove first lan"),
        ("show lans",
         "Response: \\[Lif \\{ element: Some\\(Id \\{ uuid: \\[6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 2 \\}\\), type_: Some\\(Lan\\), name: Some\\(\"lan2\"\\), port_ids: Some\\(\\[4\\]\\), vlan: Some\\(0\\), properties: Some\\(Lan\\(LanProperties \\{ address_v4: None, enable_dhcp_server: Some\\(false\\), dhcp_config: None, address_v6: None, enable_dns_forwarder: Some\\(false\\), enable: Some\\(false\\) \\}\\)\\) \\}\\]\n",
          "verify there is only one lan"),
    ]).await;
}

#[fasync::run_singlethreaded]
#[test]
async fn test_lan_dhcp() {
    let device = test_device().await;
    execute_test_suite(&device, test_suite![
        // ("command to test", "expected regex match statement", description),
        ("show ports",
         "Port \\{ element: Id \\{ uuid: \\[2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 2, path: \"/mock/port1\" \\}\nPort \\{ element: Id \\{ uuid: \\[3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 3, path: \"/mock/port2\" \\}\nPort \\{ element: Id \\{ uuid: \\[4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 4, path: \"/mock/port3\" \\}\nPort \\{ element: Id \\{ uuid: \\[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 1, path: \"loopback\" \\}\n",
         "test starts with three ports present."),
         (" add lan lan1 --ports 2",
    "Response: \\(Some\\(Id \\{ uuid: \\[5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 1 \\}\\), None\\)\n",
    "add lan interface using existing port; should succeed."),
        ("show lans",
         "Response: \\[Lif \\{ element: Some\\(Id \\{ uuid: \\[5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 1 \\}\\), type_: Some\\(Lan\\), name: Some\\(\"lan1\"\\), port_ids: Some\\(\\[2\\]\\), vlan: Some\\(0\\), properties: Some\\(Lan\\(LanProperties \\{ address_v4: None, enable_dhcp_server: Some\\(false\\), dhcp_config: None, address_v6: None, enable_dns_forwarder: Some\\(false\\), enable: Some\\(false\\) \\}\\)\\) \\}\\]\n",
         "interface up."),
        (" set lan-dhcp 5 up",
         "enable_dhcp_server: Some\\(true\\)",
         "server up."),
        ("show lan 5",
         // TODO(b/35640): fix state not saved.
         "enable_dhcp_server: Some\\(false\\)",
         "server shows up."),
        (" set lan-dhcp 5 down",
         "enable_dhcp_server: Some\\(false\\)",
         "change to down."),
        ("show lan 5",
         "enable_dhcp_server: Some\\(false\\)",
         "interface down."),
        (" set lan-dhcp 6 up",
         "NotFound",
         "change ip to non existing LAN."),
        ("show lan 6",
         "NotFound",
         "show non existing LAN."),
        (" set lan-dhcp 5 up",
         "enable_dhcp_server: Some\\(true\\)",
         "interface up."),
        ("show lan 5",
         "enable_dhcp_server: Some\\(false\\)",
         "interface up."),
        (" set lan-dhcp 5 up",
         "enable_dhcp_server: Some\\(true\\)",
         "interface up."),
        ("show lan 5",
         "enable_dhcp_server: Some\\(false\\)",
         "interface up."),
        ("show lans",
         "Response: \\[Lif \\{ element: Some\\(Id \\{ uuid: \\[5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 5 \\}\\), type_: Some\\(Lan\\), name: Some\\(\"lan1\"\\), port_ids: Some\\(\\[2\\]\\), vlan: Some\\(0\\), properties: Some\\(Lan\\(LanProperties \\{ address_v4: None, enable_dhcp_server: Some\\(false\\), dhcp_config: None, address_v6: None, enable_dns_forwarder: Some\\(false\\), enable: Some\\(false\\) \\}\\)\\) \\}\\]\n",
         "interface up."),
    ]).await;
}

#[fasync::run_singlethreaded]
#[test]
async fn test_lan_ip() {
    let device = test_device().await;
    execute_test_suite(&device, test_suite![
        // ("command to test", "expected regex match statement", description),
        ("show ports",
         "Port \\{ element: Id \\{ uuid: \\[2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 2, path: \"/mock/port1\" \\}\nPort \\{ element: Id \\{ uuid: \\[3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 3, path: \"/mock/port2\" \\}\nPort \\{ element: Id \\{ uuid: \\[4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 4, path: \"/mock/port3\" \\}\nPort \\{ element: Id \\{ uuid: \\[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 1, path: \"loopback\" \\}\n",
         "test starts with three ports present."),
         (" add lan lan1 --ports 2",
    "Response: \\(Some\\(Id \\{ uuid: \\[5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 1 \\}\\), None\\)\n",
    "add lan interface using existing port; should succeed."),
        (" set lan-ip 5 1.1.1.2/23",
         "address_v4: Some\\(CidrAddress \\{ address: Some\\(Ipv4\\(Ipv4Address \\{ addr: \\[1, 1, 1, 2\\] \\}\\)\\), prefix_length: Some\\(23\\) \\}\\), enable_dhcp_server: None, dhcp_config",
         "set ip and gateway."),
        ("show lan 5",
         "address_v4: Some\\(CidrAddress \\{ address: Some\\(Ipv4\\(Ipv4Address \\{ addr: \\[1, 1, 1, 2\\] \\}\\)\\), prefix_length: Some\\(23\\) \\}\\), enable_dhcp_server: Some\\(false\\), dhcp_config",
         "show ip."),
        (" set lan-ip 5 2.2.2.2/24",
         "address_v4: Some\\(CidrAddress \\{ address: Some\\(Ipv4\\(Ipv4Address \\{ addr: \\[2, 2, 2, 2\\] \\}\\)\\), prefix_length: Some\\(24\\) \\}\\), enable_dhcp_server: None, dhcp_config",
         "change ip."),
        ("show lan 5",
         "address_v4: Some\\(CidrAddress \\{ address: Some\\(Ipv4\\(Ipv4Address \\{ addr: \\[2, 2, 2, 2\\] \\}\\)\\), prefix_length: Some\\(24\\) \\}\\), enable_dhcp_server: Some\\(false\\), dhcp_config",
         "show new ip."),
        (" set lan-ip 6 3.3.3.2/24",
         "NotFound",
         "change ip to non existing LAN."),
        ("show lan 6",
         "NotFound",
         "show non existing LAN."),
        ("show lans",
         "Response: \\[Lif \\{ element: Some\\(Id \\{ uuid: \\[5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 3 \\}\\), type_: Some\\(Lan\\), name: Some\\(\"lan1\"\\), port_ids: Some\\(\\[2\\]\\), vlan: Some\\(0\\), properties: Some\\(Lan\\(LanProperties \\{ address_v4: Some\\(CidrAddress \\{ address: Some\\(Ipv4\\(Ipv4Address \\{ addr: \\[2, 2, 2, 2\\] \\}\\)\\), prefix_length: Some\\(24\\) \\}\\), enable_dhcp_server: Some\\(false\\), dhcp_config: None, address_v6: None, enable_dns_forwarder: Some\\(false\\), enable: Some\\(false\\) \\}\\)\\) \\}\\]",
         "lan should be manual config to 2.2.2.2/24."),
    ]).await;
}

#[fasync::run_singlethreaded]
#[test]
async fn test_lan_state() {
    let device = test_device().await;
    execute_test_suite(&device, test_suite![
        // ("command to test", "expected regex match statement", description),
        ("show ports",
         "Port \\{ element: Id \\{ uuid: \\[2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 2, path: \"/mock/port1\" \\}\nPort \\{ element: Id \\{ uuid: \\[3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 3, path: \"/mock/port2\" \\}\nPort \\{ element: Id \\{ uuid: \\[4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 4, path: \"/mock/port3\" \\}\nPort \\{ element: Id \\{ uuid: \\[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 1, path: \"loopback\" \\}\n",
         "test starts with three ports present."),
         (" add lan lan1 --ports 2",
    "Response: \\(Some\\(Id \\{ uuid: \\[5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 1 \\}\\), None\\)\n",
    "add lan interface using existing port; should succeed."),
        ("show lans",
         "Response: \\[Lif \\{ element: Some\\(Id \\{ uuid: \\[5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 1 \\}\\), type_: Some\\(Lan\\), name: Some\\(\"lan1\"\\), port_ids: Some\\(\\[2\\]\\), vlan: Some\\(0\\), properties: Some\\(Lan\\(LanProperties \\{ address_v4: None, enable_dhcp_server: Some\\(false\\), dhcp_config: None, address_v6: None, enable_dns_forwarder: Some\\(false\\), enable: Some\\(false\\) \\}\\)\\) \\}\\]\n",
         "interface up."),
        (" set lan-state 5 up",
         "enable: Some\\(true\\)",
         "interface up."),
        ("show lan 5",
         "enable: Some\\(true\\)",
         "show ip."),
        (" set lan-state 5 down",
         "enable: Some\\(false\\)",
         "change to down."),
        ("show lan 5",
         "enable: Some\\(false\\)",
         "interface down."),
        (" set lan-state 6 up",
         "NotFound",
         "change ip to non existing LAN."),
        ("show lan 6",
         "NotFound",
         "show non existing LAN."),
        (" set lan-state 5 up",
         "enable: Some\\(true\\)",
         "interface up."),
        ("show lan 5",
         "enable: Some\\(true\\)",
         "interface up."),
        (" set lan-state 5 up",
         "enable: Some\\(true\\)",
         "interface up."),
        ("show lan 5",
         "enable: Some\\(true\\)",
         "interface up."),
        ("show lans",
         "Response: \\[Lif \\{ element: Some\\(Id \\{ uuid: \\[5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 5 \\}\\), type_: Some\\(Lan\\), name: Some\\(\"lan1\"\\), port_ids: Some\\(\\[2\\]\\), vlan: Some\\(0\\), properties: Some\\(Lan\\(LanProperties \\{ address_v4: None, enable_dhcp_server: Some\\(false\\), dhcp_config: None, address_v6: None, enable_dns_forwarder: Some\\(false\\), enable: Some\\(true\\) \\}\\)\\) \\}\\]\n",
         "interface up."),
    ]).await;
}

#[fasync::run_singlethreaded]
#[test]
async fn test_interface_config_persists() {
    let device = test_device().await;
    execute_test_suite(&device, test_suite![
        ("show ports",
         "Port \\{ element: Id \\{ uuid: \\[2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 2, path: \"/mock/port1\" \\}\nPort \\{ element: Id \\{ uuid: \\[3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 3, path: \"/mock/port2\" \\}\nPort \\{ element: Id \\{ uuid: \\[4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 4, path: \"/mock/port3\" \\}\nPort \\{ element: Id \\{ uuid: \\[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 1, path: \"loopback\" \\}\n",
         "test starts with three ports present."),
         (" add lan lan1 --ports 2",
    "Response: \\(Some\\(Id \\{ uuid: \\[5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 1 \\}\\), None\\)\n",
    "add lan interface using existing port; should succeed."),
        ("show lans",
         "Response: \\[Lif \\{ element: Some\\(Id \\{ uuid: \\[5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 1 \\}\\), type_: Some\\(Lan\\), name: Some\\(\"lan1\"\\), port_ids: Some\\(\\[2\\]\\), vlan: Some\\(0\\), properties: Some\\(Lan\\(LanProperties \\{ address_v4: None, enable_dhcp_server: Some\\(false\\), dhcp_config: None, address_v6: None, enable_dns_forwarder: Some\\(false\\), enable: Some\\(false\\) \\}\\)\\) \\}\\]\n",
         "interface up."),
        (" set lan-state 5 down",
         "enable: Some\\(false\\)",
         "change to down."),
        (" set lan-ip 5 2.2.2.2/24",
         "address_v4: Some\\(CidrAddress \\{ address: Some\\(Ipv4\\(Ipv4Address \\{ addr: \\[2, 2, 2, 2\\] \\}\\)\\), prefix_length: Some\\(24\\) \\}\\), enable_dhcp_server: None, dhcp_config",
         "change ip."),
        ("show lan 5",
         "address_v4: Some\\(CidrAddress \\{ address: Some\\(Ipv4\\(Ipv4Address \\{ addr: \\[2, 2, 2, 2\\] \\}\\)\\), prefix_length: Some\\(24\\) \\}\\), enable_dhcp_server: Some\\(false\\), dhcp_config",
         "show new ip."),
        (" set lan-state 5 up",
         "enable: Some\\(true\\)",
         "interface up."),
        ("show lan 5",
         "address_v4: Some\\(CidrAddress \\{ address: Some\\(Ipv4\\(Ipv4Address \\{ addr: \\[2, 2, 2, 2\\] \\}\\)\\), prefix_length: Some\\(24\\) \\}\\), enable_dhcp_server: Some\\(false\\), dhcp_config",
         "should still have same config."),
    ]).await;
}

#[fasync::run_singlethreaded]
#[test]
async fn test_wan_connection() {
    let device = test_device().await;
    execute_test_suite(&device, test_suite![
        // ("command to test", "expected regex match statement", description),
        ("show ports",
         "Port \\{ element: Id \\{ uuid: \\[2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 2, path: \"/mock/port1\" \\}\nPort \\{ element: Id \\{ uuid: \\[3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 3, path: \"/mock/port2\" \\}\nPort \\{ element: Id \\{ uuid: \\[4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 4, path: \"/mock/port3\" \\}\nPort \\{ element: Id \\{ uuid: \\[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 1, path: \"loopback\" \\}\n",
         "test starts with three ports present."),
         (" add wan wan1 --ports 2",
    "Response: \\(Some\\(Id \\{ uuid: \\[5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 1 \\}\\), None\\)\n",
    "add wan interface using existing port; should succeed."),
        ("show wans",
"Response: \\[Lif \\{ element: Some\\(Id \\{ uuid: \\[5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 1 \\}\\), type_: Some\\(Wan\\), name: Some\\(\"wan1\"\\), port_ids: Some\\(\\[2\\]\\), vlan: Some\\(0\\), properties: Some\\(Wan\\(WanProperties \\{ connection_type: Some\\(Direct\\), connection_parameters: None, address_method: Some\\(Manual\\), address_v4: None, gateway_v4: None, connection_v6_mode: Some\\(Passthrough\\), address_v6: None, gateway_v6: None, hostname: None, clone_mac: None, mtu: None, enable: Some\\(false\\), metric: None \\}\\)\\) \\}\\]\n",
         "wan1 is present."),
        (" set wan-connection 5 direct",
         "connection_type: Some\\(Direct\\).+\nResponse: None",
         "direct."),
        ("show wan 5",
         "connection_type: Some\\(Direct\\), connection_parameters: None",
         "pptp."),
        ("set wan-connection 5 pppoe user password 1.2.3.4",
         "connection_type: Some\\(PpPoE\\), connection_parameters: Some\\(Pppoe\\(Pppoe \\{ credentials: Some\\(Credentials \\{ user: Some\\(\"user\"\\), password: Some\\(\"password\"\\).+\n.+NotFound",
         "pppoe."),
        ("show wan 5",
         "connection_type: Some\\(Direct\\), connection_parameters: None",
         "pppoe."),
        ("set wan-connection 5 l2tp user password 1.2.3.4",
         "connection_type: Some\\(L2Tp\\), connection_parameters: Some\\(L2tp\\(L2tp \\{ credentials: Some\\(Credentials \\{ user: Some\\(\"user\"\\), password: Some\\(\"password\"\\).+\n.+NotFound",
         "l2tp."),
        ("show wan 5",
         "connection_type: Some\\(Direct\\), connection_parameters: None",
         "l2tp."),
        ("set wan-connection 5 pptp user password 1.2.3.4",
         "connection_type: Some\\(Pptp\\), connection_parameters: Some\\(Pptp\\(Pptp \\{ credentials: Some\\(Credentials \\{ user: Some\\(\"user\"\\), password: Some\\(\"password\"\\).+\n.+NotFound",
         "pptp."),
        ("show wan 5",
         "connection_type: Some\\(Direct\\), connection_parameters: None",
         "pptp."),
        (" set wan-connection 5 direct",
         "connection_type: Some\\(Direct\\).+\nResponse: None",
         "back to direct."),
        ("show wans",
"Response: \\[Lif \\{ element: Some\\(Id \\{ uuid: \\[5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 3 \\}\\), type_: Some\\(Wan\\), name: Some\\(\"wan1\"\\), port_ids: Some\\(\\[2\\]\\), vlan: Some\\(0\\), properties: Some\\(Wan\\(WanProperties \\{ connection_type: Some\\(Direct\\), connection_parameters: None, address_method: Some\\(Manual\\), address_v4: None, gateway_v4: None, connection_v6_mode: Some\\(Passthrough\\), address_v6: None, gateway_v6: None, hostname: None, clone_mac: None, mtu: None, enable: Some\\(false\\), metric: None \\}\\)\\) \\}\\]\n",
         "one direct connection wan."),
    ]).await;
}

#[fasync::run_singlethreaded]
#[test]
async fn test_wan_ip() {
    let device = test_device().await;
    execute_test_suite(&device, test_suite![
        // ("command to test", "expected regex match statement", description),
        ("show ports",
         "Port \\{ element: Id \\{ uuid: \\[2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 2, path: \"/mock/port1\" \\}\nPort \\{ element: Id \\{ uuid: \\[3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 3, path: \"/mock/port2\" \\}\nPort \\{ element: Id \\{ uuid: \\[4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 4, path: \"/mock/port3\" \\}\nPort \\{ element: Id \\{ uuid: \\[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 1, path: \"loopback\" \\}\n",
         "test starts with three ports present."),
        (" add wan wan1 --ports 2", "Response: \\(Some\\(Id \\{ uuid: \\[5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 1 \\}\\), None\\)\n","add wan interface using existing port; should succeed."),
        ("show wan 5",
         "address_method: Some\\(Manual\\), address_v4: None, gateway_v4: None",
         "no ip configured"),
        (" set wan-ip 5 dhcp",
         "address_method: Some\\(Automatic\\), address_v4: None, gateway_v4: None",
         "configure dhcp."),
        ("show wan 5",
         "address_method: Some\\(Automatic\\), address_v4: None, gateway_v4: None",
         "dhcp configured"),
        (" set wan-ip 5 manual 1.1.1.2/24 1.1.1.1",
          "address_method: Some\\(Manual\\), address_v4: Some\\(CidrAddress \\{ address: Some\\(Ipv4\\(Ipv4Address \\{ addr: \\[1, 1, 1, 2\\] \\}\\)\\), prefix_length: Some\\(24\\) \\}\\), gateway_v4: Some\\(Ipv4\\(Ipv4Address \\{ addr: \\[1, 1, 1, 1\\] \\}\\)\\)",
         "manual ip and gateway."),
        ("show wan 5",
         // TODO(b/35641): Fix, gateway not being set.
          //"address_method: Some\\(Manual\\), address_v4: Some\\(CidrAddress \\{ address: Some\\(Ipv4\\(Ipv4Address \\{ addr: \\[1, 1, 1, 2\\] \\}\\)\\), prefix_length: Some\\(24\\) \\}\\), gateway_v4: Some\\(Ipv4\\(Ipv4Address \\{ addr: \\[1, 1, 1, 1\\] \\}\\)\\)",
          "address_method: Some\\(Manual\\), address_v4: Some\\(CidrAddress \\{ address: Some\\(Ipv4\\(Ipv4Address \\{ addr: \\[1, 1, 1, 2\\] \\}\\)\\), prefix_length: Some\\(24\\) \\}\\), gateway_v4: ",
         "manual ip and gateway."),
        (" set wan-ip 5 manual 2.2.2.2/24 2.2.2.1",
          "address_method: Some\\(Manual\\), address_v4: Some\\(CidrAddress \\{ address: Some\\(Ipv4\\(Ipv4Address \\{ addr: \\[2, 2, 2, 2\\] \\}\\)\\), prefix_length: Some\\(24\\) \\}\\), gateway_v4: Some\\(Ipv4\\(Ipv4Address \\{ addr: \\[2, 2, 2, 1\\] \\}\\)\\)",
         "change manual ip and gateway."),
        ("show wan 5",
         // TODO(b/35641): Fix, gateway not being set.
          "address_method: Some\\(Manual\\), address_v4: Some\\(CidrAddress \\{ address: Some\\(Ipv4\\(Ipv4Address \\{ addr: \\[2, 2, 2, 2\\] \\}\\)\\), prefix_length: Some\\(24\\) \\}\\), gateway_v4: ",
         "manual ip and gateway."),
        (" set wan-ip 5 manual 1.1.1.2/24",
         "No gateway IP address provided for manual config",
         "incomplete command"),
        ("show wan 5",
         // TODO(b/35641): Fix, gateway not being set.
          "address_method: Some\\(Manual\\), address_v4: Some\\(CidrAddress \\{ address: Some\\(Ipv4\\(Ipv4Address \\{ addr: \\[2, 2, 2, 2\\] \\}\\)\\), prefix_length: Some\\(24\\) \\}\\), gateway_v4: ",
         "should still be 2.2.2.2."),
        (" set wan-ip 5 manual 2.2.2.2/24 2.2.2.5",
          "address_method: Some\\(Manual\\), address_v4: Some\\(CidrAddress \\{ address: Some\\(Ipv4\\(Ipv4Address \\{ addr: \\[2, 2, 2, 2\\] \\}\\)\\), prefix_length: Some\\(24\\) \\}\\), gateway_v4: Some\\(Ipv4\\(Ipv4Address \\{ addr: \\[2, 2, 2, 5\\] \\}\\)\\)",
         "modifying gateway."),
        (" set wan-ip 5 manual 2.2.2.2/24 1.1.1.1",
         // TODO(b/35642): FIX, this should fail as gw is on different subnet.
          "address_method: Some\\(Manual\\), address_v4: Some\\(CidrAddress \\{ address: Some\\(Ipv4\\(Ipv4Address \\{ addr: \\[2, 2, 2, 2\\] \\}\\)\\), prefix_length: Some\\(24\\) \\}\\), gateway_v4: Some\\(Ipv4\\(Ipv4Address \\{ addr: \\[1, 1, 1, 1\\] \\}\\)\\)",
         "manual ip and gateway."),
        ("show wans",
         // TODO(b/35642): FIX, this should fail as gw is on different subnet.
         "Response: \\[Lif \\{ element: Some\\(Id \\{ uuid: \\[5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 6 \\}\\), type_: Some\\(Wan\\), name: Some\\(\"wan1\"\\), port_ids: Some\\(\\[2\\]\\), vlan: Some\\(0\\), properties: Some\\(Wan\\(WanProperties \\{ connection_type: Some\\(Direct\\), connection_parameters: None, address_method: Some\\(Manual\\), address_v4: Some\\(CidrAddress \\{ address: Some\\(Ipv4\\(Ipv4Address \\{ addr: \\[2, 2, 2, 2\\] \\}\\)\\), prefix_length: Some\\(24\\) \\}\\), gateway_v4: None, connection_v6_mode: Some\\(Passthrough\\), address_v6: None, gateway_v6: None, hostname: None, clone_mac: None, mtu: None, enable: Some\\(false\\), metric: None \\}\\)\\) \\}\\]\n",
         "wan should be manual config to 2.2.2.2/24 gw 2.2.2.1."),
        (" set wan-ip 5 dhcp",
         "address_method: Some\\(Automatic\\), address_v4: None, gateway_v4: None",
         "set it back to dhcp."),
        ("show wan 5",
         "\\(Lif \\{ element: Some\\(Id \\{ uuid: \\[5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 7 \\}\\), type_: Some\\(Wan\\), name: Some\\(\"wan1\"\\), port_ids: Some\\(\\[2\\]\\), vlan: Some\\(0\\), properties: Some\\(Wan\\(WanProperties \\{ connection_type: Some\\(Direct\\), connection_parameters: None, address_method: Some\\(Automatic\\), address_v4: None, gateway_v4: None, connection_v6_mode: Some\\(Passthrough\\), address_v6: None, gateway_v6: None, hostname: None, clone_mac: None, mtu: None, enable: Some\\(false\\), metric: None \\}\\)\\) \\}, None\\)\n",
         "dhcp configured, no ip"),
    ]).await;
}

#[fasync::run_singlethreaded]
#[test]
async fn test_wan_mac() {
    let device = test_device().await;
    execute_test_suite(&device, test_suite![
        // ("command to test", "expected regex match statement", "comment"),
        ("show ports",
         "Port \\{ element: Id \\{ uuid: \\[2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 2, path: \"/mock/port1\" \\}\nPort \\{ element: Id \\{ uuid: \\[3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 3, path: \"/mock/port2\" \\}\nPort \\{ element: Id \\{ uuid: \\[4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 4, path: \"/mock/port3\" \\}\nPort \\{ element: Id \\{ uuid: \\[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 1, path: \"loopback\" \\}\n",
         "test starts with three ports present."),
        ("show wans", "Response: \\[\\]\n","There should be no WANs created yet."),
        (" add wan wan1 --ports 2", "Response: \\(Some\\(Id \\{ uuid: \\[5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 1 \\}\\), None\\)\n","add wan interface using existing port; should succeed."),
        (" add wan wan2 --ports 4", "Response: \\(Some\\(Id \\{ uuid: \\[6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 2 \\}\\), None\\)\n","add wan interface using existing port; should succeed."),
        ("show wans",
         "Response: \\[Lif \\{ element: Some\\(Id \\{ uuid: \\[5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 1 \\}\\), type_: Some\\(Wan\\), name: Some\\(\"wan1\"\\), port_ids: Some\\(\\[2\\]\\), vlan: Some\\(0\\), properties: Some\\(Wan\\(WanProperties \\{ connection_type: Some\\(Direct\\), connection_parameters: None, address_method: Some\\(Manual\\), address_v4: None, gateway_v4: None, connection_v6_mode: Some\\(Passthrough\\), address_v6: None, gateway_v6: None, hostname: None, clone_mac: None, mtu: None, enable: Some\\(false\\), metric: None \\}\\)\\) \\}, Lif \\{ element: Some\\(Id \\{ uuid: \\[6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 2 \\}\\), type_: Some\\(Wan\\), name: Some\\(\"wan2\"\\), port_ids: Some\\(\\[4\\]\\), vlan: Some\\(0\\), properties: Some\\(Wan\\(WanProperties \\{ connection_type: Some\\(Direct\\), connection_parameters: None, address_method: Some\\(Manual\\), address_v4: None, gateway_v4: None, connection_v6_mode: Some\\(Passthrough\\), address_v6: None, gateway_v6: None, hostname: None, clone_mac: None, mtu: None, enable: Some\\(false\\), metric: None \\}\\)\\) \\}\\]\n",
         "There should be two WAN interfaces."),
        (" set wan-mac 7 02:bb:10:22:3e:00",
         "NotFound",
         "non existing wan"),
        (" set wan-mac 5 02:gh:bb:10:22:3e",
         "Invalid character",
         "invalid address"),
        // TODO(dpradilla): FIX mcast MAC should fail
         (" set wan-mac 5 01:cd:bb:10:22:3e",
         "clone_mac: Some\\(MacAddress \\{ octets: \\[1, 205, 187, 16, 34, 62\\].+\n.+NotFound",
         "mcast address"),
        (" set wan-mac 6 02:bb:10:22:3e:00",
         "clone_mac: Some\\(MacAddress \\{ octets: \\[2, 187, 16, 34, 62, 0\\].+\n.+NotFound",
         "valid mac"),
         // TODO(idpradilla): fix changes are not saved.
        ("show wans",
         "Response: \\[Lif \\{ element: Some\\(Id \\{ uuid: \\[5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 1 \\}\\), type_: Some\\(Wan\\), name: Some\\(\"wan1\"\\), port_ids: Some\\(\\[2\\]\\), vlan: Some\\(0\\), properties: Some\\(Wan\\(WanProperties \\{ connection_type: Some\\(Direct\\), connection_parameters: None, address_method: Some\\(Manual\\), address_v4: None, gateway_v4: None, connection_v6_mode: Some\\(Passthrough\\), address_v6: None, gateway_v6: None, hostname: None, clone_mac: None, mtu: None, enable: Some\\(false\\), metric: None \\}\\)\\) \\}, Lif \\{ element: Some\\(Id \\{ uuid: \\[6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 2 \\}\\), type_: Some\\(Wan\\), name: Some\\(\"wan2\"\\), port_ids: Some\\(\\[4\\]\\), vlan: Some\\(0\\), properties: Some\\(Wan\\(WanProperties \\{ connection_type: Some\\(Direct\\), connection_parameters: None, address_method: Some\\(Manual\\), address_v4: None, gateway_v4: None, connection_v6_mode: Some\\(Passthrough\\), address_v6: None, gateway_v6: None, hostname: None, clone_mac: None, mtu: None, enable: Some\\(false\\), metric: None \\}\\)\\) \\}\\]\n",
          "verify only one WAN changed its MAC."),
    ]).await;
}

#[fasync::run_singlethreaded]
#[test]
async fn test_wan_state() {
    let device = test_device().await;
    execute_test_suite(&device, test_suite![
        // ("command to test", "expected regex match statement", description),
        ("show ports",
         "Port \\{ element: Id \\{ uuid: \\[2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 2, path: \"/mock/port1\" \\}\nPort \\{ element: Id \\{ uuid: \\[3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 3, path: \"/mock/port2\" \\}\nPort \\{ element: Id \\{ uuid: \\[4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 4, path: \"/mock/port3\" \\}\nPort \\{ element: Id \\{ uuid: \\[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 1, path: \"loopback\" \\}\n",
         "test starts with three ports present."),
         (" add wan wan1 --ports 2",
    "Response: \\(Some\\(Id \\{ uuid: \\[5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 1 \\}\\), None\\)\n",
    "add wan interface using existing port; should succeed."),
        ("show wans",
"Response: \\[Lif \\{ element: Some\\(Id \\{ uuid: \\[5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 1 \\}\\), type_: Some\\(Wan\\), name: Some\\(\"wan1\"\\), port_ids: Some\\(\\[2\\]\\), vlan: Some\\(0\\), properties: Some\\(Wan\\(WanProperties \\{ connection_type: Some\\(Direct\\), connection_parameters: None, address_method: Some\\(Manual\\), address_v4: None, gateway_v4: None, connection_v6_mode: Some\\(Passthrough\\), address_v6: None, gateway_v6: None, hostname: None, clone_mac: None, mtu: None, enable: Some\\(false\\), metric: None \\}\\)\\) \\}\\]\n",
         "interface up."),
        (" set wan-state 5 up",
         "enable: Some\\(true\\)",
         "interface up."),
        ("show wan 5",
         "enable: Some\\(true\\)",
         "show ip."),
        (" set wan-state 5 down",
         "enable: Some\\(false\\)",
         "change to down."),
        ("show wan 5",
         "enable: Some\\(false\\)",
         "interface down."),
        (" set wan-state 6 up",
         "NotFound",
         "change ip to non existing WAN."),
        ("show wan 6",
         "NotFound",
         "show non existing WAN."),
        (" set wan-state 5 up",
         "enable: Some\\(true\\)",
         "interface up."),
        ("show wan 5",
         "enable: Some\\(true\\)",
         "interface up."),
        (" set wan-state 5 up",
         "enable: Some\\(true\\)",
         "interface up again."),
        ("show wan 5",
         "enable: Some\\(true\\)",
         "interface still up."),
        ("show wans",
"Response: \\[Lif \\{ element: Some\\(Id \\{ uuid: \\[5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 5 \\}\\), type_: Some\\(Wan\\), name: Some\\(\"wan1\"\\), port_ids: Some\\(\\[2\\]\\), vlan: Some\\(0\\), properties: Some\\(Wan\\(WanProperties \\{ connection_type: Some\\(Direct\\), connection_parameters: None, address_method: Some\\(Manual\\), address_v4: None, gateway_v4: None, connection_v6_mode: Some\\(Passthrough\\), address_v6: None, gateway_v6: None, hostname: None, clone_mac: None, mtu: None, enable: Some\\(true\\), metric: None \\}\\)\\) \\}\\]\n",
         "interface up."),
    ]).await;
}

#[fasync::run_singlethreaded]
#[test]
async fn test_filters() {
    let commands = test_suite![
        ("show filterstate", "0 filters installed", "test starts with no filters installed yet."),
        ("set filter allow 0.0.0.0/0 22-22 0.0.0.0/0 22-22", "Response: \\(None, None\\)", ""),
        ("show filterstate", "2 filters installed\n\\[FilterRule \\{ element: Id \\{ uuid: \\[0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, action: Allow, selector: FlowSelector \\{ src_address: Some\\(CidrAddress \\{ address: Some\\(Ipv4\\(Ipv4Address \\{ addr: \\[0, 0, 0, 0\\] \\}\\)\\), prefix_length: Some\\(0\\) \\}\\), src_ports: Some\\(\\[PortRange \\{ from: 22, to: 22 \\}\\]\\), dst_address: Some\\(CidrAddress \\{ address: Some\\(Ipv4\\(Ipv4Address \\{ addr: \\[0, 0, 0, 0\\] \\}\\)\\), prefix_length: Some\\(0\\) \\}\\), dst_ports: Some\\(\\[PortRange \\{ from: 22, to: 22 \\}\\]\\), protocol: Some\\(Tcp\\) \\} \\}, FilterRule \\{ element: Id \\{ uuid: \\[0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, action: Allow, selector: FlowSelector \\{ src_address: Some\\(CidrAddress \\{ address: Some\\(Ipv4\\(Ipv4Address \\{ addr: \\[0, 0, 0, 0\\] \\}\\)\\), prefix_length: Some\\(0\\) \\}\\), src_ports: Some\\(\\[PortRange \\{ from: 22, to: 22 \\}\\]\\), dst_address: Some\\(CidrAddress \\{ address: Some\\(Ipv4\\(Ipv4Address \\{ addr: \\[0, 0, 0, 0\\] \\}\\)\\), prefix_length: Some\\(0\\) \\}\\), dst_ports: Some\\(\\[PortRange \\{ from: 22, to: 22 \\}\\]\\), protocol: Some\\(Udp\\) \\} \\}\\]\n", "A TCP and a UDP packet filter should be installed for port 22"),
    ];

    let sandbox = connect_to_service::<SandboxMarker>().expect("Can't connect to sandbox");
    let env = create_managed_env(&sandbox).expect("Failed to create environment with services");
    for test in commands.tests {
        let actual_output = exec_cmd(&env, &test.command).await;
        println!("command: {}", test.command);
        println!("actual: {:?}", actual_output);
        let re = Regex::new(&test.expected).unwrap();
        assert!(re.is_match(&actual_output));
    }
}

#[fasync::run_singlethreaded]
#[test]
async fn test_port_forward() {
    let device = test_device().await;
    execute_test_suite(&device, test_suite![
        // ("command to test", "expected regex match statement", description),
        ("show ports",
         "Port \\{ element: Id \\{ uuid: \\[2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 2, path: \"/mock/port1\" \\}\nPort \\{ element: Id \\{ uuid: \\[3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 3, path: \"/mock/port2\" \\}\nPort \\{ element: Id \\{ uuid: \\[4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 4, path: \"/mock/port3\" \\}\nPort \\{ element: Id \\{ uuid: \\[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 1, path: \"loopback\" \\}\n",
         "test starts with three ports present."),
         (" add wan wan1 --ports 2",
    "Response: \\(Some\\(Id \\{ uuid: \\[5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 1 \\}\\), None\\)\n",
    "add wan interface using existing port; should succeed."),
        ("show wans",
"Response: \\[Lif \\{ element: Some\\(Id \\{ uuid: \\[5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 1 \\}\\), type_: Some\\(Wan\\), name: Some\\(\"wan1\"\\), port_ids: Some\\(\\[2\\]\\), vlan: Some\\(0\\), properties: Some\\(Wan\\(WanProperties \\{ connection_type: Some\\(Direct\\), connection_parameters: None, address_method: Some\\(Manual\\), address_v4: None, gateway_v4: None, connection_v6_mode: Some\\(Passthrough\\), address_v6: None, gateway_v6: None, hostname: None, clone_mac: None, mtu: None, enable: Some\\(false\\), metric: None \\}\\)\\) \\}\\]\n",
         ""),
        ("set port-forward x",
         "Not Implemented",
         ""),
        ("show wans",
"Response: \\[Lif \\{ element: Some\\(Id \\{ uuid: \\[5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 1 \\}\\), type_: Some\\(Wan\\), name: Some\\(\"wan1\"\\), port_ids: Some\\(\\[2\\]\\), vlan: Some\\(0\\), properties: Some\\(Wan\\(WanProperties \\{ connection_type: Some\\(Direct\\), connection_parameters: None, address_method: Some\\(Manual\\), address_v4: None, gateway_v4: None, connection_v6_mode: Some\\(Passthrough\\), address_v6: None, gateway_v6: None, hostname: None, clone_mac: None, mtu: None, enable: Some\\(false\\), metric: None \\}\\)\\) \\}\\]\n",
         "no changes."),
    ]).await;
}

#[fasync::run_singlethreaded]
#[test]
async fn test_route() {
    let device = test_device().await;
    execute_test_suite(&device, test_suite![
        // ("command to test", "expected regex match statement", description),
        ("show ports",
         "Port \\{ element: Id \\{ uuid: \\[2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 2, path: \"/mock/port1\" \\}\nPort \\{ element: Id \\{ uuid: \\[3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 3, path: \"/mock/port2\" \\}\nPort \\{ element: Id \\{ uuid: \\[4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 4, path: \"/mock/port3\" \\}\nPort \\{ element: Id \\{ uuid: \\[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 1, path: \"loopback\" \\}\n",
         "test starts with three ports present."),
         (" add wan wan1 --ports 2",
    "Response: \\(Some\\(Id \\{ uuid: \\[5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 1 \\}\\), None\\)\n",
    "add wan interface using existing port; should succeed."),
        ("show wans",
"Response: \\[Lif \\{ element: Some\\(Id \\{ uuid: \\[5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 1 \\}\\), type_: Some\\(Wan\\), name: Some\\(\"wan1\"\\), port_ids: Some\\(\\[2\\]\\), vlan: Some\\(0\\), properties: Some\\(Wan\\(WanProperties \\{ connection_type: Some\\(Direct\\), connection_parameters: None, address_method: Some\\(Manual\\), address_v4: None, gateway_v4: None, connection_v6_mode: Some\\(Passthrough\\), address_v6: None, gateway_v6: None, hostname: None, clone_mac: None, mtu: None, enable: Some\\(false\\), metric: None \\}\\)\\) \\}\\]\n",
         ""),
        ("set route x",
         "Not Implemented",
         ""),
        ("show wans",
"Response: \\[Lif \\{ element: Some\\(Id \\{ uuid: \\[5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 1 \\}\\), type_: Some\\(Wan\\), name: Some\\(\"wan1\"\\), port_ids: Some\\(\\[2\\]\\), vlan: Some\\(0\\), properties: Some\\(Wan\\(WanProperties \\{ connection_type: Some\\(Direct\\), connection_parameters: None, address_method: Some\\(Manual\\), address_v4: None, gateway_v4: None, connection_v6_mode: Some\\(Passthrough\\), address_v6: None, gateway_v6: None, hostname: None, clone_mac: None, mtu: None, enable: Some\\(false\\), metric: None \\}\\)\\) \\}\\]\n",
         "no changes."),
    ]).await;
}

#[fasync::run_singlethreaded]
#[test]
async fn test_security_config() {
    let device = test_device().await;
    execute_test_suite(&device, test_suite![
        // ("command to test", "expected regex match statement", description),
        ("show ports",
         "Port \\{ element: Id \\{ uuid: \\[2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 2, path: \"/mock/port1\" \\}\nPort \\{ element: Id \\{ uuid: \\[3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 3, path: \"/mock/port2\" \\}\nPort \\{ element: Id \\{ uuid: \\[4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 4, path: \"/mock/port3\" \\}\nPort \\{ element: Id \\{ uuid: \\[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 1, path: \"loopback\" \\}\n",
         "test starts with three ports present."),
        ("set security-config nat enable",
         "Setting security feature: NAT, enabled: true",
         "NAT should be administratively enabled."),
        ("add wan wan1 --ports 2",
         "Response: \\(Some\\(Id \\{ uuid: \\[5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 1 \\}\\), None\\)\n",
         "add wan interface using existing port; should succeed."),
        ("show wans",
         "Response: \\[Lif \\{ element: Some\\(Id \\{ uuid: \\[5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 1 \\}\\), type_: Some\\(Wan\\), name: Some\\(\"wan1\"\\), port_ids: Some\\(\\[2\\]\\), vlan: Some\\(0\\), properties: Some\\(Wan\\(WanProperties \\{ connection_type: Some\\(Direct\\), connection_parameters: None, address_method: Some\\(Manual\\), address_v4: None, gateway_v4: None, connection_v6_mode: Some\\(Passthrough\\), address_v6: None, gateway_v6: None, hostname: None, clone_mac: None, mtu: None, enable: Some\\(false\\), metric: None \\}\\)\\) \\}\\]\n",
         ""),
        ("set wan-ip 5 manual 192.168.0.2/24 192.168.0.1",
         "Sending: WanProperties \\{ connection_type: None, connection_parameters: None, address_method: Some\\(Manual\\), address_v4: Some\\(CidrAddress \\{ address: Some\\(Ipv4\\(Ipv4Address \\{ addr: \\[192, 168, 0, 2\\] \\}\\)\\), prefix_length: Some\\(24\\) \\}\\), gateway_v4: Some\\(Ipv4\\(Ipv4Address \\{ addr: \\[192, 168, 0, 1\\] \\}\\)\\), connection_v6_mode: None, address_v6: None, gateway_v6: None, hostname: None, clone_mac: None, mtu: None, enable: None, metric: None \\}\nResponse: None\n",
         "set a wan ip address manually; should succeed."),
        ("show wans",
         "Response: \\[Lif \\{ element: Some\\(Id \\{ uuid: \\[5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 2 \\}\\), type_: Some\\(Wan\\), name: Some\\(\"wan1\"\\), port_ids: Some\\(\\[2\\]\\), vlan: Some\\(0\\), properties: Some\\(Wan\\(WanProperties \\{ connection_type: Some\\(Direct\\), connection_parameters: None, address_method: Some\\(Manual\\), address_v4: Some\\(CidrAddress \\{ address: Some\\(Ipv4\\(Ipv4Address \\{ addr: \\[192, 168, 0, 2\\] \\}\\)\\), prefix_length: Some\\(24\\) \\}\\), gateway_v4: None, connection_v6_mode: Some\\(Passthrough\\), address_v6: None, gateway_v6: None, hostname: None, clone_mac: None, mtu: None, enable: Some\\(false\\), metric: None \\}\\)\\) \\}\\]\n",
         "the new wan ip address should now be set."),
        ("show lans",
         "Response: \\[\\]",
         ""),
        ("add lan lan1 --ports 3",
         "Response: \\(Some\\(Id \\{ uuid: \\[6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 3 \\}\\), None\\)\n",
         "add lan interface using existing port; should succeed."),
        ("show lans",
         "Response: \\[Lif \\{ element: Some\\(Id \\{ uuid: \\[6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 3 \\}\\), type_: Some\\(Lan\\), name: Some\\(\"lan1\"\\), port_ids: Some\\(\\[3\\]\\), vlan: Some\\(0\\), properties: Some\\(Lan\\(LanProperties \\{ address_v4: None, enable_dhcp_server: Some\\(false\\), dhcp_config: None, address_v6: None, enable_dns_forwarder: Some\\(false\\), enable: Some\\(false\\)",
         ""),
        ("set lan-ip 6 172.31.0.55/32",
         "Sending: LanProperties \\{ address_v4: Some\\(CidrAddress \\{ address: Some\\(Ipv4\\(Ipv4Address \\{ addr: \\[172, 31, 0, 55\\] \\}\\)\\), prefix_length: Some\\(32\\) \\}\\), enable_dhcp_server: None, dhcp_config: None, address_v6: None, enable_dns_forwarder: None, enable: None \\} ID: Id \\{ uuid: \\[6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}\nResponse: None\n",
         "set a lan ip address; should succeed."),
        ("show security-config",
         "nat: Some\\(true\\),",
         "NAT should be enabled"),
        ("set security-config nat",
         "Setting security feature: NAT, enabled: false\nResponse: None\n",
         ""),
        ("show security-config",
         "nat: Some\\(false\\),",
         "NAT should be administratively disabled."),
        ("set security-config xxx",
         "error: Invalid value for \'<feature>\': Invalid security feature: \'xxx\'",
         "cannot set an unknown security-config setting"),
        ("show wans",
         "Response: \\[Lif \\{ element: Some\\(Id \\{ uuid: \\[5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 2 \\}\\), type_: Some\\(Wan\\), name: Some\\(\"wan1\"\\), port_ids: Some\\(\\[2\\]\\), vlan: Some\\(0\\), properties: Some\\(Wan\\(WanProperties \\{ connection_type: Some\\(Direct\\), connection_parameters: None, address_method: Some\\(Manual\\), address_v4: Some\\(CidrAddress \\{ address: Some\\(Ipv4\\(Ipv4Address \\{ addr: \\[192, 168, 0, 2\\] \\}\\)\\), prefix_length: Some\\(24\\) \\}\\), gateway_v4: None, connection_v6_mode: Some\\(Passthrough\\), address_v6: None, gateway_v6: None, hostname: None, clone_mac: None, mtu: None, enable: Some\\(false\\), metric: None \\}\\)\\) \\}\\]\n",
         "no changes."),
    ]).await;
}

#[fasync::run_singlethreaded]
#[test]
async fn test_dhcp_config() {
    let device = test_device().await;
    execute_test_suite(&device, test_suite![
        // ("command to test", "expected regex match statement", description),
        ("show ports",
         "Port \\{ element: Id \\{ uuid: \\[2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 2, path: \"/mock/port1\" \\}\nPort \\{ element: Id \\{ uuid: \\[3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 3, path: \"/mock/port2\" \\}\nPort \\{ element: Id \\{ uuid: \\[4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 4, path: \"/mock/port3\" \\}\nPort \\{ element: Id \\{ uuid: \\[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 1, path: \"loopback\" \\}\n",
         "test starts with three ports present."),
         (" add wan wan1 --ports 2",
    "Response: \\(Some\\(Id \\{ uuid: \\[5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 1 \\}\\), None\\)\n",
    "add wan interface using existing port; should succeed."),
        ("show wans",
"Response: \\[Lif \\{ element: Some\\(Id \\{ uuid: \\[5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 1 \\}\\), type_: Some\\(Wan\\), name: Some\\(\"wan1\"\\), port_ids: Some\\(\\[2\\]\\), vlan: Some\\(0\\), properties: Some\\(Wan\\(WanProperties \\{ connection_type: Some\\(Direct\\), connection_parameters: None, address_method: Some\\(Manual\\), address_v4: None, gateway_v4: None, connection_v6_mode: Some\\(Passthrough\\), address_v6: None, gateway_v6: None, hostname: None, clone_mac: None, mtu: None, enable: Some\\(false\\), metric: None \\}\\)\\) \\}\\]\n",
         ""),
        ("set dhcp-config 6  x",
         "Not Implemented",
         ""),
        ("show wans",
"Response: \\[Lif \\{ element: Some\\(Id \\{ uuid: \\[5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 1 \\}\\), type_: Some\\(Wan\\), name: Some\\(\"wan1\"\\), port_ids: Some\\(\\[2\\]\\), vlan: Some\\(0\\), properties: Some\\(Wan\\(WanProperties \\{ connection_type: Some\\(Direct\\), connection_parameters: None, address_method: Some\\(Manual\\), address_v4: None, gateway_v4: None, connection_v6_mode: Some\\(Passthrough\\), address_v6: None, gateway_v6: None, hostname: None, clone_mac: None, mtu: None, enable: Some\\(false\\), metric: None \\}\\)\\) \\}\\]\n",
         "no changes."),
    ]).await;
}

#[fasync::run_singlethreaded]
#[test]
async fn test_dns_config() {
    let device = test_device().await;
    execute_test_suite(&device, test_suite![
        // ("command to test", "expected regex match statement", description),
        ("show ports",
         "Port \\{ element: Id \\{ uuid: \\[2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 2, path: \"/mock/port1\" \\}\nPort \\{ element: Id \\{ uuid: \\[3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 3, path: \"/mock/port2\" \\}\nPort \\{ element: Id \\{ uuid: \\[4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 4, path: \"/mock/port3\" \\}\nPort \\{ element: Id \\{ uuid: \\[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 1, path: \"loopback\" \\}\n",
         "test starts with three ports present."),
         (" add wan wan1 --ports 2",
    "Response: \\(Some\\(Id \\{ uuid: \\[5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 1 \\}\\), None\\)\n",
    "add wan interface using existing port; should succeed."),
        ("show wans",
"Response: \\[Lif \\{ element: Some\\(Id \\{ uuid: \\[5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 1 \\}\\), type_: Some\\(Wan\\), name: Some\\(\"wan1\"\\), port_ids: Some\\(\\[2\\]\\), vlan: Some\\(0\\), properties: Some\\(Wan\\(WanProperties \\{ connection_type: Some\\(Direct\\), connection_parameters: None, address_method: Some\\(Manual\\), address_v4: None, gateway_v4: None, connection_v6_mode: Some\\(Passthrough\\), address_v6: None, gateway_v6: None, hostname: None, clone_mac: None, mtu: None, enable: Some\\(false\\), metric: None \\}\\)\\) \\}\\]\n",
         ""),
        ("set dns-config 8.8.8.8",
         "Ok\\(\\(Some\\(Id \\{ uuid: \\[0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 2 \\}\\), None\\)\\)",
         "server configured"),
         ("show dnsconfig",
          "DnsResolverConfig \\{ element: Id \\{ uuid: \\[0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 2 \\}, search: DnsSearch \\{ servers: \\[Ipv4\\(Ipv4Address \\{ addr: \\[8, 8, 8, 8\\] \\}\\)\\], domain_name: None \\}, policy: Static \\}",
          "Get DNS config , server 8.8.8.8"),
        ("set dns-config 8.8.8.266",
         "invalid IP address syntax",
         "invalid server address, it should fail"),
         ("show dnsconfig",
          "DnsResolverConfig \\{ element: Id \\{ uuid: \\[0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 2 \\}, search: DnsSearch \\{ servers: \\[Ipv4\\(Ipv4Address \\{ addr: \\[8, 8, 8, 8\\] \\}\\)\\], domain_name: None \\}, policy: Static \\}",
          "Get DNS config, server should still be 8.8.8.8"),
        ("set dns-config 8.8.8.8",
         "Ok\\(\\(Some\\(Id \\{ uuid: \\[0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 2 \\}\\), None\\)\\)",
         "server configured"),
         ("show dnsconfig",
          "DnsResolverConfig \\{ element: Id \\{ uuid: \\[0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 2 \\}, search: DnsSearch \\{ servers: \\[Ipv4\\(Ipv4Address \\{ addr: \\[8, 8, 8, 8\\] \\}\\)\\], domain_name: None \\}, policy: Static \\}",
          "Get DNS config , server still  8.8.8.8, no version increase"),
        ("set dns-config 8.8.4.4",
         "Ok\\(\\(Some\\(Id \\{ uuid: \\[0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 3 \\}\\), None\\)\\)",
         "server configured"),
         ("show dnsconfig",
          "DnsResolverConfig \\{ element: Id \\{ uuid: \\[0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 3 \\}, search: DnsSearch \\{ servers: \\[Ipv4\\(Ipv4Address \\{ addr: \\[8, 8, 4, 4\\] \\}\\)\\], domain_name: None \\}, policy: Static \\}",
          "Get DNS config , server should not be 8.8.4.4"),
        ("show wans",
"Response: \\[Lif \\{ element: Some\\(Id \\{ uuid: \\[5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 1 \\}\\), type_: Some\\(Wan\\), name: Some\\(\"wan1\"\\), port_ids: Some\\(\\[2\\]\\), vlan: Some\\(0\\), properties: Some\\(Wan\\(WanProperties \\{ connection_type: Some\\(Direct\\), connection_parameters: None, address_method: Some\\(Manual\\), address_v4: None, gateway_v4: None, connection_v6_mode: Some\\(Passthrough\\), address_v6: None, gateway_v6: None, hostname: None, clone_mac: None, mtu: None, enable: Some\\(false\\), metric: None \\}\\)\\) \\}\\]\n",
         "no changes."),
    ]).await;
}

#[fasync::run_singlethreaded]
#[test]
async fn test_dns_forwarder() {
    let device = test_device().await;
    execute_test_suite(&device, test_suite![
        // ("command to test", "expected regex match statement", description),
        ("show ports",
         "Port \\{ element: Id \\{ uuid: \\[2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 2, path: \"/mock/port1\" \\}\nPort \\{ element: Id \\{ uuid: \\[3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 3, path: \"/mock/port2\" \\}\nPort \\{ element: Id \\{ uuid: \\[4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 4, path: \"/mock/port3\" \\}\nPort \\{ element: Id \\{ uuid: \\[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 1, path: \"loopback\" \\}\n",
         "test starts with three ports present."),
        (" add wan wan1 --ports 2",
    "Response: \\(Some\\(Id \\{ uuid: \\[5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 1 \\}\\), None\\)\n",
    "add wan interface using existing port; should succeed."),
        ("show wans",
"Response: \\[Lif \\{ element: Some\\(Id \\{ uuid: \\[5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 1 \\}\\), type_: Some\\(Wan\\), name: Some\\(\"wan1\"\\), port_ids: Some\\(\\[2\\]\\), vlan: Some\\(0\\), properties: Some\\(Wan\\(WanProperties \\{ connection_type: Some\\(Direct\\), connection_parameters: None, address_method: Some\\(Manual\\), address_v4: None, gateway_v4: None, connection_v6_mode: Some\\(Passthrough\\), address_v6: None, gateway_v6: None, hostname: None, clone_mac: None, mtu: None, enable: Some\\(false\\), metric: None \\}\\)\\) \\}\\]\n",
         ""),
        ("set dns-forwarder 6 true",
         "Not Implemented",
         ""),
        ("show wans",
"Response: \\[Lif \\{ element: Some\\(Id \\{ uuid: \\[5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 1 \\}\\), type_: Some\\(Wan\\), name: Some\\(\"wan1\"\\), port_ids: Some\\(\\[2\\]\\), vlan: Some\\(0\\), properties: Some\\(Wan\\(WanProperties \\{ connection_type: Some\\(Direct\\), connection_parameters: None, address_method: Some\\(Manual\\), address_v4: None, gateway_v4: None, connection_v6_mode: Some\\(Passthrough\\), address_v6: None, gateway_v6: None, hostname: None, clone_mac: None, mtu: None, enable: Some\\(false\\), metric: None \\}\\)\\) \\}\\]\n",
         "no changes."),
    ]).await;
}

#[fasync::run_singlethreaded]
#[test]
async fn test_link_events() {
    let device = test_device().await;
    execute_test_suite(&device, test_suite![
        // ("command to test", "expected regex match statement", description),
        ("show ports",
         "Port \\{ element: Id \\{ uuid: \\[2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 2, path: \"/mock/port1\" \\}\nPort \\{ element: Id \\{ uuid: \\[3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 3, path: \"/mock/port2\" \\}\nPort \\{ element: Id \\{ uuid: \\[4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 4, path: \"/mock/port3\" \\}\nPort \\{ element: Id \\{ uuid: \\[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 1, path: \"loopback\" \\}\n",
         "test starts with three ports present."),
        (" add wan wan1 --ports 2",
    "Response: \\(Some\\(Id \\{ uuid: \\[5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 1 \\}\\), None\\)\n",
    "add wan interface using existing port; should succeed."),
        (" add lan lan1 --ports 3",
         "Response: \\(Some\\(Id \\{ uuid: \\[6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 2 \\}\\), None\\)\n",
         "add lan interface using existing port; should succeed."),
        ("show wans",
"Response: \\[Lif \\{ element: Some\\(Id \\{ uuid: \\[5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 1 \\}\\), type_: Some\\(Wan\\), name: Some\\(\"wan1\"\\), port_ids: Some\\(\\[2\\]\\), vlan: Some\\(0\\), properties: Some\\(Wan\\(WanProperties \\{ connection_type: Some\\(Direct\\), connection_parameters: None, address_method: Some\\(Manual\\), address_v4: None, gateway_v4: None, connection_v6_mode: Some\\(Passthrough\\), address_v6: None, gateway_v6: None, hostname: None, clone_mac: None, mtu: None, enable: Some\\(false\\), metric: None \\}\\)\\) \\}\\]\n",
         "There should be one LAN interfaces."),
        ("show lans",
          "Response: \\[Lif \\{ element: Some\\(Id \\{ uuid: \\[6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 2 \\}\\), type_: Some\\(Lan\\), name: Some\\(\"lan1\"\\), port_ids: Some\\(\\[3\\]\\), vlan: Some\\(0\\), properties: Some\\(Lan\\(LanProperties \\{ address_v4: None, enable_dhcp_server: Some\\(false\\), dhcp_config: None, address_v6: None, enable_dns_forwarder: Some\\(false\\), enable: Some\\(false\\) \\}\\)\\) \\}\\]\n",
         "There should be one LAN interfaces."),
    ]).await;

    device.endpoints[0].set_link_up(true).await.expect("failed setting interface link");
    device.endpoints[1].set_link_up(true).await.expect("failed setting interface link");
    device.endpoints[2].set_link_up(true).await.expect("failed setting interface link");
    execute_test_suite(&device, test_suite![
        // ("command to test", "expected regex match statement", description),
        ("show ports",
         "Port \\{ element: Id \\{ uuid: \\[2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 2, path: \"/mock/port1\" \\}\nPort \\{ element: Id \\{ uuid: \\[3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 3, path: \"/mock/port2\" \\}\nPort \\{ element: Id \\{ uuid: \\[4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 4, path: \"/mock/port3\" \\}\nPort \\{ element: Id \\{ uuid: \\[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 1, path: \"loopback\" \\}\n",
         "test starts with three ports present."),
        ("show wans",
"Response: \\[Lif \\{ element: Some\\(Id \\{ uuid: \\[5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 1 \\}\\), type_: Some\\(Wan\\), name: Some\\(\"wan1\"\\), port_ids: Some\\(\\[2\\]\\), vlan: Some\\(0\\), properties: Some\\(Wan\\(WanProperties \\{ connection_type: Some\\(Direct\\), connection_parameters: None, address_method: Some\\(Manual\\), address_v4: None, gateway_v4: None, connection_v6_mode: Some\\(Passthrough\\), address_v6: None, gateway_v6: None, hostname: None, clone_mac: None, mtu: None, enable: Some\\(false\\), metric: None \\}\\)\\) \\}\\]\n",
         "There should be one LAN interfaces."),
        ("show lans",
          "Response: \\[Lif \\{ element: Some\\(Id \\{ uuid: \\[6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 2 \\}\\), type_: Some\\(Lan\\), name: Some\\(\"lan1\"\\), port_ids: Some\\(\\[3\\]\\), vlan: Some\\(0\\), properties: Some\\(Lan\\(LanProperties \\{ address_v4: None, enable_dhcp_server: Some\\(false\\), dhcp_config: None, address_v6: None, enable_dns_forwarder: Some\\(false\\), enable: Some\\(false\\) \\}\\)\\) \\}\\]\n",
         "There should be one LAN interfaces."),
    ]).await;

    device.endpoints[0].set_link_up(false).await.expect("failed setting interface link");
    device.endpoints[1].set_link_up(false).await.expect("failed setting interface link");
    device.endpoints[2].set_link_up(false).await.expect("failed setting interface link");
    execute_test_suite(&device, test_suite![
        // ("command to test", "expected regex match statement", description),
        ("show ports",
         "Port \\{ element: Id \\{ uuid: \\[2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 2, path: \"/mock/port1\" \\}\nPort \\{ element: Id \\{ uuid: \\[3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 3, path: \"/mock/port2\" \\}\nPort \\{ element: Id \\{ uuid: \\[4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 4, path: \"/mock/port3\" \\}\nPort \\{ element: Id \\{ uuid: \\[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 1, path: \"loopback\" \\}\n",
         "test starts with three ports present."),
        ("show wans",
"Response: \\[Lif \\{ element: Some\\(Id \\{ uuid: \\[5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 1 \\}\\), type_: Some\\(Wan\\), name: Some\\(\"wan1\"\\), port_ids: Some\\(\\[2\\]\\), vlan: Some\\(0\\), properties: Some\\(Wan\\(WanProperties \\{ connection_type: Some\\(Direct\\), connection_parameters: None, address_method: Some\\(Manual\\), address_v4: None, gateway_v4: None, connection_v6_mode: Some\\(Passthrough\\), address_v6: None, gateway_v6: None, hostname: None, clone_mac: None, mtu: None, enable: Some\\(false\\), metric: None \\}\\)\\) \\}\\]\n",
         "There should be one LAN interfaces."),
        ("show lans",
          "Response: \\[Lif \\{ element: Some\\(Id \\{ uuid: \\[6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 2 \\}\\), type_: Some\\(Lan\\), name: Some\\(\"lan1\"\\), port_ids: Some\\(\\[3\\]\\), vlan: Some\\(0\\), properties: Some\\(Lan\\(LanProperties \\{ address_v4: None, enable_dhcp_server: Some\\(false\\), dhcp_config: None, address_v6: None, enable_dns_forwarder: Some\\(false\\), enable: Some\\(false\\) \\}\\)\\) \\}\\]\n",
         "There should be one LAN interfaces."),
    ]).await;

    device.endpoints[0].set_link_up(true).await.expect("failed setting interface link");
    device.endpoints[1].set_link_up(true).await.expect("failed setting interface link");
    device.endpoints[2].set_link_up(true).await.expect("failed setting interface link");
    execute_test_suite(&device, test_suite![
        // ("command to test", "expected regex match statement", description),
        ("show ports",
         "Port \\{ element: Id \\{ uuid: \\[2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 2, path: \"/mock/port1\" \\}\nPort \\{ element: Id \\{ uuid: \\[3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 3, path: \"/mock/port2\" \\}\nPort \\{ element: Id \\{ uuid: \\[4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 4, path: \"/mock/port3\" \\}\nPort \\{ element: Id \\{ uuid: \\[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 1, path: \"loopback\" \\}\n",
         "test starts with three ports present."),
        ("show wans",
"Response: \\[Lif \\{ element: Some\\(Id \\{ uuid: \\[5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 1 \\}\\), type_: Some\\(Wan\\), name: Some\\(\"wan1\"\\), port_ids: Some\\(\\[2\\]\\), vlan: Some\\(0\\), properties: Some\\(Wan\\(WanProperties \\{ connection_type: Some\\(Direct\\), connection_parameters: None, address_method: Some\\(Manual\\), address_v4: None, gateway_v4: None, connection_v6_mode: Some\\(Passthrough\\), address_v6: None, gateway_v6: None, hostname: None, clone_mac: None, mtu: None, enable: Some\\(false\\), metric: None \\}\\)\\) \\}\\]\n",
         "There should be one LAN interfaces."),
        ("show lans",
          "Response: \\[Lif \\{ element: Some\\(Id \\{ uuid: \\[6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 2 \\}\\), type_: Some\\(Lan\\), name: Some\\(\"lan1\"\\), port_ids: Some\\(\\[3\\]\\), vlan: Some\\(0\\), properties: Some\\(Lan\\(LanProperties \\{ address_v4: None, enable_dhcp_server: Some\\(false\\), dhcp_config: None, address_v6: None, enable_dns_forwarder: Some\\(false\\), enable: Some\\(false\\) \\}\\)\\) \\}\\]\n",
         "There should be one LAN interfaces."),
    ]).await;
}

#[fasync::run_singlethreaded]
#[test]
async fn test_flush_filter_rules() {
    let device = test_device().await;
    execute_test_suite(
        &device,
        test_suite![
            // ("command to test", "expected regex match statement", description),
            (
                "show filterstate",
                "0 filters installed.*",
                "test starts with no packet filters installed"
            ),
            (
                "set filter allow 127.0.0.1/32 31337-31337 0.0.0.0/0 31337-31337 tcp",
                "Response: \\(None, None\\)",
                "adds a new single packet filter rule"
            ),
            (
                "show filterstate",
                "1 filters installed.*",
                "test that the new filter is present in the ruleset"
            ),
            ("set delete-filter 0", "Response: None", "deletes the new filter rule"),
            // TODO(44183): Once we are tracking rule insertion and deletion we can make this clear
            // individual rules. But for the moment, this will clear all rules, despite what ID you
            // provide.
            ("show filterstate", "0 filters installed.*", "all filters have been deleted."),
        ],
    )
    .await;
}
