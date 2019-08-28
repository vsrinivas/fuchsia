// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await)]
#[cfg(test)]
use failure::{Error, ResultExt};
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
use fuchsia_async::{self as fasync};
use fuchsia_component::client::connect_to_service;
use fuchsia_component::fuchsia_single_component_package_url as component_url;
use network_manager_cli::cli::{make_cmd, run_cmd};
use network_manager_cli::printer::Printer;
use regex::Regex;
use std::str;

#[derive(Debug)]
struct Test {
    command: String,
    expected: String,
}

impl Test {
    fn new<A: Into<String>, B: Into<String>>(command: A, expected: B) -> Self {
        Self { command: command.into(), expected: expected.into() }
    }
}

#[derive(Debug)]
struct TestSuite {
    tests: Vec<Test>,
}

macro_rules! test_suite {
    [$( ($cmd:expr,$expected:expr), )*] => {
        TestSuite {
            tests: vec![ $(Test::new($cmd,$expected),)*]
        }
    };
}

fn generate_launch_services() -> Vec<LaunchService> {
    let names_and_urls = vec![
        ("fuchsia.net.stack.Stack", component_url!("netstack")),
        ("fuchsia.netstack.Netstack", component_url!("netstack")),
        ("fuchsia.net.NameLookup", component_url!("netstack")),
        ("fuchsia.posix.socket.Provider", component_url!("netstack")),
        ("fuchsia.net.filter.Filter", component_url!("netstack")),
        ("fuchsia.router.config.RouterAdmin", component_url!("network_manager")),
        ("fuchsia.router.config.RouterState", component_url!("network_manager")),
    ];
    names_and_urls
        .into_iter()
        .map(|(name, url)| LaunchService {
            name: name.to_string(),
            url: url.to_string(),
            arguments: None,
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
) -> Result<EndpointManagerProxy, failure::Error> {
    let (client, server) = fidl::endpoints::create_proxy::<EndpointManagerMarker>()
        .context("failed to create endpoint manager proxy")?;
    let () =
        network_context.get_endpoint_manager(server).context("failed to get endpoint manager")?;
    Ok(client)
}

async fn create_endpoint<'a>(
    name: &'static str,
    endpoint_manager: &'a EndpointManagerProxy,
) -> std::result::Result<EndpointProxy, failure::Error> {
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
        .ok_or(failure::err_msg("failed to create endpoint"))?
        .into_proxy()
        .context("failed to get endpoint proxy")?;
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
) -> Result<S::Proxy, failure::Error> {
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
    netstack_proxy: fidl_fuchsia_netstack::NetstackProxy,
    device: fidl::endpoints::ClientEnd<fidl_fuchsia_hardware_ethernet::DeviceMarker>,
) -> Result<(), Error> {
    let name = "/mock_device";
    let id = netstack_proxy
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

    // Check that the newly added ethernet interface is present before continuing with the
    // actual tests.
    let interface = netstack_proxy
        .get_interfaces2()
        .await
        .expect("failed to get interfaces")
        .into_iter()
        .find(|interface| interface.id == id)
        .ok_or(failure::err_msg("failed to find added ethernet device"))
        .unwrap();
    assert_eq!(interface.features & fidl_fuchsia_hardware_ethernet::INFO_FEATURE_LOOPBACK, 0);
    assert_eq!(interface.flags & fidl_fuchsia_netstack::NET_INTERFACE_FLAG_UP, 0);
    Ok(())
}

#[fasync::run_singlethreaded]
#[test]
async fn test_show_commands() {
    // The pair is constructed of the CLI command to test, and it's expected output as a
    // regular expression parsable by the Regex crate.
    let show_commands = test_suite![
        // ("command to test", "expected regex match statement"),
        ("show dhcpconfig 0",
         "Id \\{ uuid: \\[0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}"),
         ("show dnsconfig", "Get DNS config"),
         ("show filterstate", "0 filters installed.*"),
         ("show forwardstate 0",
          "Id \\{ uuid: \\[0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}"),
          ("show lanconfig 0",
           "Id \\{ uuid: \\[0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\} Response: \\(LanProperties \\{ address_v4: None, enable_dhcp_server: None, dhcp_config: None, address_v6: None, enable_dns_forwarder: None, enable: None \\}, Some\\(Error \\{ code: NotSupported, description: None \\}\\)"),
           ("show ports",
            "Port \\{ element: Id \\{ uuid: \\[2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 2, path: \"/mock_device\" \\}\nPort \\{ element: Id \\{ uuid: \\[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, id: 1, path: \"loopback\" }"),
            ("show routes", "Response: \\[\\]"),
            ("show wans", "Response: \\[\\]"),
    ];

    for test in show_commands.tests {
        let sandbox = connect_to_service::<SandboxMarker>().expect("Can't connect to sandbox");
        let network_context = get_network_context(&sandbox).expect("failed to get network context");
        let endpoint_manager =
            get_endpoint_manager(&network_context).expect("failed to get endpoint manager");
        let endpoint = create_endpoint(stringify!(test_interface), &endpoint_manager)
            .await
            .expect("failed to create endpoint");
        let device = endpoint.get_ethernet_device().await.expect("failed to get ethernet device");
        let env = create_managed_env(&sandbox).expect("Failed to create environment with services");
        let netstack_proxy =
            connect_to_sandbox_service::<fidl_fuchsia_netstack::NetstackMarker>(&env)
                .expect("failed to connect to netstack");
        let _ = add_ethernet_device(netstack_proxy, device).await;
        let (router_admin, router_state) =
            connect_network_manager(&env).expect("Failed to connect from managed environment");

        let mut printer = Printer::new(Vec::new());
        let cmd = match make_cmd(&test.command) {
            Ok(cmd) => cmd,
            Err(e) => panic!("Failed to parse command: '{}', because: {}", test.command, e),
        };

        let actual_output;
        match run_cmd(cmd, router_admin, router_state, &mut printer).await {
            Ok(_) => {
                // TODO(cgibson): Figure out a way to add this as a helper method on `Printer`.
                actual_output = str::from_utf8(&printer.into_inner().unwrap()).unwrap().to_string();
            }
            Err(e) => panic!("Running command '{}' failed: {:?}", test.command, e),
        }
        println!("command: {}", test.command);
        println!("actual: {:?}", actual_output);
        let re = Regex::new(&test.expected).unwrap();
        assert!(re.is_match(&actual_output));
    }
}

#[fasync::run_singlethreaded]
#[test]
async fn test_filters() {
    let commands = test_suite![
        ("show filterstate", "0 filters installed"),
        ("set filter allow 0.0.0.0/0 22-22 0.0.0.0/0 22-22", "Response: \\(None, None\\)"),
        ("show filterstate", "2 filters installed\n\\[FilterRule \\{ element: Id \\{ uuid: \\[0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, action: Allow, selector: FlowSelector \\{ src_address: Some\\(CidrAddress \\{ address: Some\\(Ipv4\\(Ipv4Address \\{ addr: \\[0, 0, 0, 0\\] \\}\\)\\), prefix_length: Some\\(0\\) \\}\\), src_ports: Some\\(\\[PortRange \\{ from: 22, to: 22 \\}\\]\\), dst_address: Some\\(CidrAddress \\{ address: Some\\(Ipv4\\(Ipv4Address \\{ addr: \\[0, 0, 0, 0\\] \\}\\)\\), prefix_length: Some\\(0\\) \\}\\), dst_ports: Some\\(\\[PortRange \\{ from: 22, to: 22 \\}\\]\\), protocol: Some\\(Tcp\\) \\} \\}, FilterRule \\{ element: Id \\{ uuid: \\[0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\], version: 0 \\}, action: Allow, selector: FlowSelector \\{ src_address: Some\\(CidrAddress \\{ address: Some\\(Ipv4\\(Ipv4Address \\{ addr: \\[0, 0, 0, 0\\] \\}\\)\\), prefix_length: Some\\(0\\) \\}\\), src_ports: Some\\(\\[PortRange \\{ from: 22, to: 22 \\}\\]\\), dst_address: Some\\(CidrAddress \\{ address: Some\\(Ipv4\\(Ipv4Address \\{ addr: \\[0, 0, 0, 0\\] \\}\\)\\), prefix_length: Some\\(0\\) \\}\\), dst_ports: Some\\(\\[PortRange \\{ from: 22, to: 22 \\}\\]\\), protocol: Some\\(Udp\\) \\} \\}\\]"),
    ];

    let sandbox = connect_to_service::<SandboxMarker>().expect("Can't connect to sandbox");
    let env = create_managed_env(&sandbox).expect("Failed to create environment with services");
    for test in commands.tests {
        let (router_admin, router_state) =
            connect_network_manager(&env).expect("Failed to connect from managed environment");
        let mut printer = Printer::new(Vec::new());
        let cmd = match make_cmd(&test.command) {
            Ok(cmd) => cmd,
            Err(e) => panic!("Failed to parse command: '{}', because: {}", test.command, e),
        };

        let actual_output;
        match run_cmd(cmd, router_admin, router_state, &mut printer).await {
            Ok(_) => {
                // TODO(cgibson): Figure out a way to add this as a helper method on `Printer`.
                actual_output = str::from_utf8(&printer.into_inner().unwrap()).unwrap().to_string();
            }
            Err(e) => panic!("Running command '{}' failed: {:?}", test.command, e),
        }
        println!("command: {}", test.command);
        println!("actual: {:?}", actual_output);
        let re = Regex::new(&test.expected).unwrap();
        assert!(re.is_match(&actual_output));
    }
}
