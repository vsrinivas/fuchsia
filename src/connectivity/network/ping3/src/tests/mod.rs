// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod integration_tests;

use anyhow::{format_err, Context as _, Error};
use fidl::endpoints::DiscoverableService;
use fidl_fuchsia_net::{IpAddress, Subnet};
use fidl_fuchsia_net_icmp::ProviderMarker;
use fidl_fuchsia_net_stack::{
    ForwardingDestination, ForwardingEntry, InterfaceAddress, PhysicalStatus, StackMarker,
};
use fidl_fuchsia_net_stack_ext::FidlReturn;
use fidl_fuchsia_netemul_environment::{
    EnvironmentOptions, LaunchService, LoggerOptions, ManagedEnvironmentMarker,
    ManagedEnvironmentProxy, VirtualDevice,
};
use fidl_fuchsia_netemul_network::{
    DeviceProxy_Marker, EndpointBacking, EndpointConfig, EndpointManagerMarker,
    EndpointManagerProxy, EndpointProxy, NetworkConfig, NetworkContextMarker, NetworkManagerMarker,
    NetworkProxy,
};
use fidl_fuchsia_netemul_sandbox::{SandboxMarker, SandboxProxy};
use fidl_fuchsia_sys::{
    ComponentControllerEvent, ComponentControllerMarker, LaunchInfo, LauncherMarker,
    TerminationReason,
};
use fuchsia_component::client::connect_to_service;
use fuchsia_component::fuchsia_single_component_package_url;
use fuchsia_zircon as zx;
use futures::StreamExt;
use std::vec::Vec;

const NETSTACK_URL: &'static str = fuchsia_single_component_package_url!("netstack3");
const PING_URL: &'static str = fuchsia_single_component_package_url!("ping3");

fn create_netstack_env(
    sandbox: &SandboxProxy,
    name: String,
    device: fidl::endpoints::ClientEnd<DeviceProxy_Marker>,
) -> Result<ManagedEnvironmentProxy, Error> {
    let (env, env_server_end) = fidl::endpoints::create_proxy::<ManagedEnvironmentMarker>()?;
    let services = vec![
        LaunchService {
            name: StackMarker::SERVICE_NAME.to_string(),
            url: NETSTACK_URL.to_string(),
            arguments: None,
        },
        LaunchService {
            name: ProviderMarker::SERVICE_NAME.to_string(),
            url: NETSTACK_URL.to_string(),
            arguments: None,
        },
    ];
    let devices = vec![VirtualDevice { path: "eth001".to_string(), device }];
    sandbox
        .create_environment(
            env_server_end,
            EnvironmentOptions {
                name: Some(name),
                services: Some(services),
                devices: Some(devices),
                inherit_parent_launch_services: None,
                logger_options: Some(LoggerOptions {
                    enabled: Some(true),
                    klogs_enabled: None,
                    filter_options: None,
                    syslog_output: Some(true),
                }),
            },
        )
        .context("Failed to create environment")?;

    Ok(env)
}

pub struct TestSetup {
    sandbox: SandboxProxy,
    network: NetworkProxy,
    endpoint_manager: EndpointManagerProxy,
    endpoints: Vec<EndpointProxy>,
    subnet_addr: IpAddress,
    subnet_prefix: u8,
}

impl TestSetup {
    pub async fn new(subnet_addr: IpAddress, subnet_prefix: u8) -> Result<Self, Error> {
        let sandbox =
            connect_to_service::<SandboxMarker>().context("Failed to connect to sandbox")?;

        let (netctx, netctx_server_end) = fidl::endpoints::create_proxy::<NetworkContextMarker>()?;
        sandbox.get_network_context(netctx_server_end).context("Failed to get network context")?;

        let (network_manager, network_manager_server_end) =
            fidl::endpoints::create_proxy::<NetworkManagerMarker>()?;
        netctx
            .get_network_manager(network_manager_server_end)
            .context("Failed to get network manager")?;

        let config = NetworkConfig { latency: None, packet_loss: None, reorder: None };
        let (status, network) = network_manager
            .create_network("test", config)
            .await
            .context("Failed to create network")?;
        assert_eq!(status, zx::sys::ZX_OK);
        let network = network.unwrap().into_proxy()?;

        let (endpoint_manager, endpoint_manager_server_end) =
            fidl::endpoints::create_proxy::<EndpointManagerMarker>()?;
        netctx
            .get_endpoint_manager(endpoint_manager_server_end)
            .context("Failed to get endpoint manager")?;

        Ok(Self {
            sandbox,
            network,
            endpoint_manager,
            endpoints: Vec::new(),
            subnet_addr,
            subnet_prefix,
        })
    }

    async fn add_env(
        &mut self,
        name: &str,
        addr: IpAddress,
    ) -> Result<ManagedEnvironmentProxy, Error> {
        let mut config =
            EndpointConfig { mtu: 1500, mac: None, backing: EndpointBacking::Ethertap };
        let (status, endpoint) = self
            .endpoint_manager
            .create_endpoint(name, &mut config)
            .await
            .context("Failed to create endpoint")?;
        assert_eq!(status, zx::sys::ZX_OK);
        let endpoint = endpoint.unwrap().into_proxy()?;
        endpoint.set_link_up(true).await.context("Failed to set link up")?;
        let status =
            self.network.attach_endpoint(name).await.context("Failed to attach endpoint")?;
        assert_eq!(status, zx::sys::ZX_OK);

        // Create device
        let (device, device_server_end) =
            fidl::endpoints::create_endpoints::<DeviceProxy_Marker>()?;
        endpoint.get_proxy_(device_server_end)?;

        // Create environment
        let env = create_netstack_env(&self.sandbox, name.to_string(), device)
            .context("Failed to create environment")?;

        // Add the ethernet interface
        let (stack, stack_server_end) = fidl::endpoints::create_proxy::<StackMarker>()?;
        env.connect_to_service("fuchsia.net.stack.Stack", stack_server_end.into_channel())
            .context("Can't connect to fuchsia.net.stack")?;

        let eth = endpoint.get_ethernet_device().await.context("Failed to get ethernet device")?;
        let interface_id = stack
            .add_ethernet_interface("fake_topo_path", eth)
            .await
            .squash_result()
            .map_err(|e| format_err!("Failed to get ethernet interface: {:?}", e))?;

        // Wait for interface to become online
        //
        // TODO(fxb/38311): Replace this loop with an event loop waiting for fuchsia.net.stack's
        // Stack.OnInterfaceStatusChange to return an ONLINE InterfaceStatusChange.
        loop {
            let info = stack
                .get_interface_info(interface_id)
                .await
                .squash_result()
                .map_err(|e| format_err!("Failed to get interface info: {:?}", e))?;
            if info.properties.physical_status == PhysicalStatus::Up {
                break;
            }
            eprintln!("Interface not ready, waiting 10ms...");
            zx::Duration::from_millis(100).sleep();
        }

        // Assign IP address and add routes
        stack
            .add_interface_address(
                interface_id,
                &mut InterfaceAddress { ip_address: addr, prefix_len: self.subnet_prefix },
            )
            .await
            .squash_result()
            .map_err(|e| format_err!("Failed to add interface address: {:?}", e))?;

        stack
            .add_forwarding_entry(&mut ForwardingEntry {
                subnet: Subnet { addr: self.subnet_addr, prefix_len: self.subnet_prefix },
                destination: ForwardingDestination::DeviceId(interface_id),
            })
            .await
            .map_err(|e| format_err!("{}", e))?
            .map_err(|e| format_err!("Failed to add forwarding entry: {:?}", e))?;

        self.endpoints.push(endpoint);
        Ok(env)
    }
}

async fn assert_ping(env: &ManagedEnvironmentProxy, args: &str, expected_return_code: i64) {
    let (launcher, launcher_server_end) =
        fidl::endpoints::create_proxy::<LauncherMarker>().unwrap();
    env.get_launcher(launcher_server_end).context("Failed to get launcher").unwrap();

    let (controller, controller_server_end) =
        fidl::endpoints::create_proxy::<ComponentControllerMarker>().unwrap();
    let mut ping_launch = LaunchInfo {
        url: PING_URL.to_string(),
        arguments: Some(args.split(" ").map(String::from).collect()),
        out: None,
        err: None,
        directory_request: None,
        flat_namespace: None,
        additional_services: None,
    };
    launcher
        .create_component(&mut ping_launch, Some(controller_server_end))
        .context("Failed to create component")
        .unwrap();

    let mut event_stream = controller.take_event_stream();
    while let Some(evt) = event_stream.next().await {
        match evt.unwrap() {
            ComponentControllerEvent::OnTerminated { return_code, termination_reason } => {
                assert_eq!(termination_reason, TerminationReason::Exited);
                assert!(
                    return_code == expected_return_code,
                    format!(
                        "Expected return code {}, but got {} instead",
                        expected_return_code, return_code
                    )
                );
            }
            _ => {}
        }
    }
}
