// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod integration_tests;

use fidl::endpoints::DiscoverableService as _;
use fidl_fuchsia_net as fnet;
use fidl_fuchsia_net_icmp as fnet_icmp;
use fidl_fuchsia_net_stack as fnet_stack;
use fidl_fuchsia_net_stack_ext::FidlReturn as _;
use fidl_fuchsia_netemul_environment as netemul_environment;
use fuchsia_component::fuchsia_single_component_package_url;
use fuchsia_zircon as zx;

use anyhow::Context as _;

const NETSTACK_URL: &'static str = fuchsia_single_component_package_url!("netstack3");
const PING_URL: &'static str = fuchsia_single_component_package_url!("ping3");

/// Configuration for an environment.
struct EnvConfig {
    /// Name for the environment and endpoint.
    name: String,

    /// The static IP to add to the created interface.
    static_ip: fnet::Subnet,
}

/// Creates an environment for each configuration in `configs`.
///
/// The environments will each join a network named `test_name` with a single interface.
/// `on_link_subnet` is the subnet that is considered on-link; the netstack in each
/// environment will be configured with an on-link route to the subnet out the interface.
async fn create_environments<'a>(
    sandbox: &'a netemul::TestSandbox,
    test_name: String,
    on_link_subnet: fnet::Subnet,
    configs: Vec<EnvConfig>,
) -> Result<
    (Vec<(netemul::TestEnvironment<'a>, netemul::TestInterface<'a>)>, netemul::TestNetwork<'a>),
    anyhow::Error,
> {
    let net = sandbox.create_network(test_name.clone()).await.context("create network")?;

    let mut envs = Vec::with_capacity(configs.len());
    for config in configs.into_iter() {
        let name = format!("{}_{}", test_name, config.name);
        let env = sandbox
            .create_environment(
                name.clone(),
                vec![
                    netemul_environment::LaunchService {
                        name: fnet_stack::StackMarker::SERVICE_NAME.to_string(),
                        url: NETSTACK_URL.to_string(),
                        arguments: Vec::new(),
                    },
                    netemul_environment::LaunchService {
                        name: fnet_icmp::ProviderMarker::SERVICE_NAME.to_string(),
                        url: NETSTACK_URL.to_string(),
                        arguments: Vec::new(),
                    },
                ],
            )
            .with_context(|| format!("create {} environment", name))?;

        let ep = net
            .create_endpoint::<netemul::Ethernet, _>(config.name.clone())
            .await
            .with_context(|| format!("create {} endpoint", config.name))?;
        let iface =
            ep.into_interface_in_environment(&env).await.context("into interface in env")?;
        let () = iface.set_link_up(true).await.context("set link up")?;
        let () = iface.enable_interface().await.context("enable interface")?;

        // Wait for the interface to become online
        //
        // TODO(fxbug.dev/21154): Replace this loop with a hanging get on `WatchInterfaces`.
        //
        // Note, netstack3 does not implement `fuchsia.netstack.Netstack/OnInterfacesChanged`
        // or `fuchsia.net.stack.Stack/OnInterfaceStatusChange`.
        loop {
            let props = iface.get_info().await.context("get interface info")?.properties;
            if props.physical_status == fnet_stack::PhysicalStatus::Up
                && props.administrative_status == fnet_stack::AdministrativeStatus::Enabled
            {
                break;
            }
            let sleep_ms = 100;
            eprintln!("interface not ready, waiting {}ms...", sleep_ms);
            let () = zx::Duration::from_millis(sleep_ms).sleep();
        }

        let () = iface.add_ip_addr(config.static_ip).await.context("add ip addr")?;

        // Wait for the address to be assigned.
        //
        // This is required for IPv6 since we need to wait for DAD to complete before an address
        // is bound to an interface.
        //
        // TODO(fxbug.dev/21154): Replace this loop with a hanging get on `WatchInterfaces`.
        //
        // Note, netstack3 does not implement `fuchsia.netstack.Netstack/OnInterfacesChanged`.
        loop {
            if iface.get_addrs().await.context("get addrs")?.contains(&config.static_ip) {
                break;
            }
            let sleep_ms = 500;
            eprintln!("address not ready, waiting {}ms...", sleep_ms);
            let () = zx::Duration::from_millis(sleep_ms).sleep();
        }

        let stack =
            env.connect_to_service::<fnet_stack::StackMarker>().context("connect to net stack")?;
        let () = stack
            .add_forwarding_entry(&mut fnet_stack::ForwardingEntry {
                subnet: on_link_subnet,
                destination: fnet_stack::ForwardingDestination::DeviceId(iface.id()),
            })
            .await
            .squash_result()
            .context("Failed to add forwarding entry")?;

        let () = envs.push((env, iface));
    }

    Ok((envs, net))
}

/// Launch `ping3` and wait for it to terminate.
async fn ping3(
    env: &netemul::TestEnvironment<'_>,
    args: &str,
) -> Result<fuchsia_component::client::ExitStatus, anyhow::Error> {
    let launcher = env.get_launcher().context("get launcher").context("get launcher")?;
    fuchsia_component::client::launch(
        &launcher,
        PING_URL.to_string(),
        Some(args.split(" ").map(String::from).collect()),
    )
    .context("launch ping3 component")?
    .wait()
    .await
}
