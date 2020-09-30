// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Context as _, Error};
use fidl::endpoints::ServiceMarker;
use fidl_fuchsia_net_interfaces::StateMarker;
use fidl_fuchsia_netemul_environment::{
    EnvironmentOptions, LaunchService, LoggerOptions, ManagedEnvironmentMarker, VirtualDevice,
};
use fidl_fuchsia_netemul_network::{
    DeviceProxy_Marker, EndpointBacking, EndpointConfig, EndpointManagerMarker, EndpointProxy,
    NetworkConfig, NetworkContextMarker, NetworkManagerMarker, NetworkProxy,
};
use fidl_fuchsia_netstack::NetstackMarker;
use fidl_fuchsia_posix_socket::ProviderMarker;
use fidl_fuchsia_sys::{
    ComponentControllerEvent, ComponentControllerMarker, ComponentControllerProxy, LaunchInfo,
    LauncherMarker, TerminationReason,
};
use fuchsia_async as fasync;
use fuchsia_component::client;
use fuchsia_zircon as zx;
use futures::TryStreamExt;
use structopt::StructOpt;

mod child;
mod common;

const MY_PACKAGE: &str = "fuchsia-pkg://fuchsia.com/netemul-sandbox-test#meta/netstack-socks.cmx";
const NETSTACK_URL: &str = "fuchsia-pkg://fuchsia.com/netstack#meta/netstack.cmx";
const NETWORK_NAME: &str = "test-network";

struct Env {
    controller: ComponentControllerProxy,
    _endpoint: EndpointProxy,
}

struct SpawnOptions {
    env_name: &'static str,
    ip: &'static str,
    remote: Option<&'static str>,
}

async fn spawn_env(network: &NetworkProxy, options: SpawnOptions) -> Result<Env, Error> {
    // connect to NetworkContext and ManagedEnvironment services
    let netctx = client::connect_to_service::<NetworkContextMarker>()?;
    let env = client::connect_to_service::<ManagedEnvironmentMarker>()?;

    let env_name = options.env_name;

    // get the endpoint manager
    let (epm, epm_server_end) = fidl::endpoints::create_proxy::<EndpointManagerMarker>()?;
    netctx.get_endpoint_manager(epm_server_end)?;

    let mut cfg = EndpointConfig {
        backing: EndpointBacking::Ethertap,
        mac: None, // let network service create a random mac for us
        mtu: 1500,
    };

    // create a network endpoint
    let (_, ep) = epm.create_endpoint(env_name, &mut cfg).await?;
    let ep = ep.unwrap().into_proxy()?;

    let stat = network.attach_endpoint(env_name).await?;
    let () = zx::Status::ok(stat)?;

    ep.set_link_up(true).await?;

    // get the endpoint proxy to pass to child environment
    let (ep_proxy_client, ep_proxy_server) =
        fidl::endpoints::create_endpoints::<DeviceProxy_Marker>()?;
    ep.get_proxy_(ep_proxy_server)?;

    // prepare a child managed environment
    let (child_env, child_env_server) =
        fidl::endpoints::create_proxy::<ManagedEnvironmentMarker>()?;

    let env_options = EnvironmentOptions {
        name: Some(String::from(env_name)),
        services: Some(vec![
            LaunchService {
                name: String::from(NetstackMarker::NAME),
                url: String::from(NETSTACK_URL),
                arguments: Vec::new(),
            },
            LaunchService {
                name: String::from(ProviderMarker::NAME),
                url: String::from(NETSTACK_URL),
                arguments: Vec::new(),
            },
            LaunchService {
                name: String::from(StateMarker::NAME),
                url: String::from(NETSTACK_URL),
                arguments: Vec::new(),
            },
        ]),
        // pass the endpoint's proxy to create a virtual device
        devices: Some(vec![VirtualDevice {
            path: String::from(format!("class/ethernet/{}", env_name)),
            device: ep_proxy_client,
        }]),
        inherit_parent_launch_services: Some(false),
        logger_options: Some(LoggerOptions {
            enabled: Some(true),
            klogs_enabled: Some(false),
            filter_options: None,
            syslog_output: None,
        }),
    };
    // launch the child env
    env.create_child_environment(child_env_server, env_options)?;

    // launch as a process in the created environment.
    let (launcher, launcher_req) = fidl::endpoints::create_proxy::<LauncherMarker>()?;
    child_env.get_launcher(launcher_req)?;

    let mut arguments = vec![
        String::from("-c"),
        String::from("-e"),
        String::from(env_name),
        String::from("-a"),
        String::from(options.ip),
    ];
    if let Some(con) = options.remote {
        arguments.push(String::from("-r"));
        arguments.push(String::from(con));
    }

    // launch info is our own package
    // plus the command line argument to run the child proc
    let mut linfo = LaunchInfo {
        url: String::from(MY_PACKAGE),
        arguments: Some(arguments),
        additional_services: None,
        directory_request: None,
        err: None,
        out: None,
        flat_namespace: None,
    };

    let (comp_controller, comp_controller_req) =
        fidl::endpoints::create_proxy::<ComponentControllerMarker>()?;
    launcher.create_component(&mut linfo, Some(comp_controller_req))?;
    Ok(Env { controller: comp_controller, _endpoint: ep })
}

async fn wait_for_component(component: &ComponentControllerProxy) -> Result<(), Error> {
    let mut component_events = component.take_event_stream();
    // wait for child to exit and mimic the result code
    let result = loop {
        let event = component_events
            .try_next()
            .await
            .context("wait for child component to exit")?
            .ok_or_else(|| format_err!("Child didn't exit cleanly"))?;

        match event {
            ComponentControllerEvent::OnTerminated {
                return_code: code,
                termination_reason: reason,
            } => {
                println!("Child exited with code {}, reason {}", code, reason as u32);
                if code != 0 || reason != TerminationReason::Exited {
                    break Err(format_err!(
                        "Child exited with code {}, reason {}",
                        code,
                        reason as u32
                    ));
                } else {
                    break Ok(());
                }
            }
            _ => {
                continue;
            }
        }
    };
    result
}

async fn create_network() -> Result<NetworkProxy, Error> {
    let netctx = client::connect_to_service::<NetworkContextMarker>()?;
    let (netm, netm_server_end) = fidl::endpoints::create_proxy::<NetworkManagerMarker>()?;
    netctx.get_network_manager(netm_server_end)?;
    let config = NetworkConfig { latency: None, packet_loss: None, reorder: None };
    let (_, network) = netm.create_network(NETWORK_NAME, config).await?;
    let network = network.ok_or_else(|| format_err!("can't create network"))?.into_proxy()?;
    Ok(network)
}

async fn prepare_env() -> Result<(), Error> {
    let net = create_network().await?;
    let server_ip_cfg = "192.168.0.1/24";
    let server_ip = "192.168.0.1";
    let client_ip_cfg = "192.168.0.2/24";

    let bus = common::BusConnection::new("root")?;

    println!("Starting server...");
    let server =
        spawn_env(&net, SpawnOptions { env_name: "server", ip: server_ip_cfg, remote: None })
            .await?;

    let () = bus.wait_for_event(common::SERVER_READY).await?;
    println!("Server ready, starting client...");

    let client = spawn_env(
        &net,
        SpawnOptions { env_name: "client", ip: client_ip_cfg, remote: Some(server_ip) },
    )
    .await?;
    let () = wait_for_component(&client.controller).await?;
    let () = wait_for_component(&server.controller).await?;
    Ok(())
}

#[derive(StructOpt, Debug)]
struct Opt {
    #[structopt(short = "c")]
    is_child: bool,
    #[structopt(short = "e")]
    ep_name: Option<String>,
    #[structopt(short = "a")]
    ip: Option<String>,
    #[structopt(short = "r")]
    remote: Option<String>,
}

fn main() -> Result<(), Error> {
    let opt = Opt::from_args();
    let mut executor = fasync::Executor::new().context("Error creating executor")?;
    if opt.is_child {
        let child_opts = child::ChildOptions {
            endpoint: opt.ep_name.unwrap(),
            ip: opt.ip.unwrap(),
            connect_ip: opt.remote,
        };
        executor.run_singlethreaded(child::run_child(child_opts))
    } else {
        executor.run_singlethreaded(prepare_env())
    }
}
