// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context as _, Error},
    fidl_fuchsia_net,
    fidl_fuchsia_netemul_network::{EndpointManagerMarker, NetworkContextMarker},
    fidl_fuchsia_netstack::{
        InterfaceConfig, NetstackMarker, RouteTableEntry2, RouteTableTransactionMarker,
    },
    fuchsia_async as fasync,
    fuchsia_component::client,
    fuchsia_zircon as zx,
    std::convert::TryFrom as _,
    structopt::StructOpt,
};

#[derive(StructOpt, Debug)]
#[structopt(name = "netstack_cfg")]
/// Configure netstack from emulated environments.
struct Opt {
    #[structopt(long, short = "e")]
    /// Endpoint name to retrieve from network.EndpointManager
    endpoint: String,
    #[structopt(long, short = "i")]
    /// Static ip addresses to assign (don't forget /prefix termination). Omit to use DHCP.
    ips: Vec<String>,
    #[structopt(long, short = "g")]
    /// Ip address of the default gateway (useful when DHCP is not used).
    gateway: Option<String>,
    #[structopt(long = "skip-up-check")]
    /// netstack_cfg will by default wait until it sees the interface be reported as "Up",
    /// skip-up-check will override that behavior.
    skip_up_check: bool,
}

const DEFAULT_METRIC: u32 = 100;

async fn config_netstack(opt: Opt) -> Result<(), Error> {
    log::info!("Configuring endpoint {}", opt.endpoint);

    // get the network context service:
    let netctx = client::connect_to_service::<NetworkContextMarker>()?;
    // get the endpoint manager
    let (epm, epmch) = fidl::endpoints::create_proxy::<EndpointManagerMarker>()?;
    netctx.get_endpoint_manager(epmch)?;

    // retrieve the created endpoint:
    let ep = epm.get_endpoint(&opt.endpoint).await?;
    let ep = ep.ok_or_else(|| format_err!("can't find endpoint {}", opt.endpoint))?.into_proxy()?;
    log::info!("Got endpoint.");
    // and the device connection:
    let device_connection = ep.get_device().await?;
    log::info!("Got device connection.");

    // connect to netstack:
    let netstack = client::connect_to_service::<NetstackMarker>()?;

    let mut cfg = InterfaceConfig {
        name: opt.endpoint.clone(),
        filepath: format!("/vdev/{}", opt.endpoint),
        metric: DEFAULT_METRIC,
    };
    let nicid = match device_connection {
        fidl_fuchsia_netemul_network::DeviceConnection::Ethernet(e) => netstack
            .add_ethernet_device(&format!("/vdev/{}", opt.endpoint), &mut cfg, e)
            .await
            .with_context(|| format!("add_ethernet_device FIDL error ({:?})", cfg))?
            .map_err(fuchsia_zircon::Status::from_raw)
            .with_context(|| format!("add_ethernet_device error ({:?})", cfg))?,
        fidl_fuchsia_netemul_network::DeviceConnection::NetworkDevice(device) => todo!(
            "(48860) Support NetworkDevice configuration. Got unexpected NetworkDevice {:?}",
            device
        ),
    };
    let nicid_u32 =
        u32::try_from(nicid).with_context(|| format!("{} does not fit in a u32", nicid))?;

    let () =
        netstack.set_interface_status(nicid_u32, true).context("error setting interface status")?;
    log::info!("Added ethernet to stack.");

    let mut subnets: Vec<fidl_fuchsia_net::Subnet> = opt
        .ips
        .iter()
        .map(|ip| {
            ip.parse::<fidl_fuchsia_net_ext::Subnet>()
                .with_context(|| format!("can't parse provided ip {}", ip))
                .map(|subnet| subnet.into())
        })
        .collect::<Result<Vec<_>, _>>()?;

    if !subnets.is_empty() {
        for fidl_fuchsia_net::Subnet { addr, prefix_len } in &mut subnets {
            let fidl_fuchsia_netstack::NetErr { status, message } = netstack
                .set_interface_address(nicid_u32, addr, *prefix_len)
                .await
                .context("set interface address error")?;
            if status != fidl_fuchsia_netstack::Status::Ok {
                return Err(anyhow::anyhow!(
                    "failed to set interface (id={}) address to {:?} with status = {:?}: {}",
                    nicid_u32,
                    addr,
                    status,
                    message
                ));
            }
        }
    } else {
        let (dhcp_client, server_end) =
            fidl::endpoints::create_proxy::<fidl_fuchsia_net_dhcp::ClientMarker>()
                .context("failed to create fidl endpoints")?;
        netstack
            .get_dhcp_client(nicid, server_end)
            .await
            .context("failed to call get_dhcp_client")?
            .map_err(fuchsia_zircon::Status::from_raw)
            .context("failed to get dhcp client")?;
        dhcp_client
            .start()
            .await
            .context("failed to call dhcp_client.start")?
            .map_err(fuchsia_zircon::Status::from_raw)
            .context("failed to start dhcp client")?;
    };

    log::info!("Configured nic address.");

    if let Some(gateway) = &opt.gateway {
        let gw_addr: fidl_fuchsia_net::IpAddress = fidl_fuchsia_net_ext::IpAddress(
            gateway.parse::<std::net::IpAddr>().context("failed to parse gateway address")?,
        )
        .into();
        let unspec_addr: fidl_fuchsia_net::IpAddress = match gw_addr {
            fidl_fuchsia_net::IpAddress::Ipv4(..) => fidl_fuchsia_net_ext::IpAddress(
                std::net::IpAddr::V4(std::net::Ipv4Addr::UNSPECIFIED),
            ),
            fidl_fuchsia_net::IpAddress::Ipv6(..) => fidl_fuchsia_net_ext::IpAddress(
                std::net::IpAddr::V6(std::net::Ipv6Addr::UNSPECIFIED),
            ),
        }
        .into();

        let (route_proxy, server_end) =
            fidl::endpoints::create_proxy::<RouteTableTransactionMarker>()
                .context("error creating route proxy")?;
        let () = netstack
            .start_route_table_transaction(server_end)
            .await
            .map(zx::Status::ok)
            .context("fidl error calling route_proxy.start_route_table_transaction")?
            .context("error starting route table transaction")?;

        let mut entry = RouteTableEntry2 {
            destination: unspec_addr,
            netmask: unspec_addr,
            gateway: Some(Box::new(gw_addr)),
            nicid: nicid_u32,
            metric: 0,
        };
        let () = route_proxy
            .add_route(&mut entry)
            .await
            .map(zx::Status::ok)
            .with_context(|| format!("fidl error calling route_proxy.add_route {:?}", entry))?
            .with_context(|| format!("error adding route {:?}", entry))?;

        log::info!("Configured the default route with gateway address.");
    }

    log::info!("Waiting for interface up...");
    let interface_state = client::connect_to_service::<fidl_fuchsia_net_interfaces::StateMarker>()?;
    let () = fidl_fuchsia_net_interfaces_ext::wait_interface_with_id(
        fidl_fuchsia_net_interfaces_ext::event_stream_from_state(&interface_state)?,
        &mut fidl_fuchsia_net_interfaces_ext::InterfaceState::Unknown(nicid.into()),
        |fidl_fuchsia_net_interfaces_ext::Properties { online, addresses, .. }| {
            if !opt.skip_up_check && !online {
                log::info!("Found interface with id {}, but it's down. waiting.", nicid);
                return None;
            }
            // If configuring static addresses, make sure the addresses are present (this ensures
            // that DAD has resolved for IPv6 addresses).
            if subnets.iter().all(|subnet| {
                let present = addresses
                    .iter()
                    .any(|fidl_fuchsia_net_interfaces_ext::Address { addr }| addr == subnet);
                if !present {
                    log::info!(
                        "Found interface with id {}, but address {} not yet present. waiting.",
                        nicid,
                        fidl_fuchsia_net_ext::Subnet::from(*subnet),
                    );
                }
                present
            }) {
                Some(())
            } else {
                None
            }
        },
    )
    .await
    .context("wait for interface")?;

    log::info!("Found ethernet with id {}", nicid);

    Ok(())
}

fn main() -> Result<(), Error> {
    let () = fuchsia_syslog::init().context("cannot init logger")?;

    let opt = Opt::from_args();
    let mut executor = fasync::Executor::new().context("Error creating executor")?;
    executor.run_singlethreaded(config_netstack(opt))
}
