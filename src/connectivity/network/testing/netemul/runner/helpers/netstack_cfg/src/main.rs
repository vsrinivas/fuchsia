// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context as _, Error},
    fidl_fuchsia_net,
    fidl_fuchsia_net_stack::StackMarker,
    fidl_fuchsia_net_stack_ext::FidlReturn,
    fidl_fuchsia_netemul_network::{EndpointManagerMarker, NetworkContextMarker},
    fidl_fuchsia_netstack::{InterfaceConfig, NetstackMarker},
    fuchsia_async as fasync,
    fuchsia_component::client,
    futures::TryStreamExt,
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
    /// Static ip address to assign (don't forget /prefix termination). Omit to use DHCP.
    ip: Option<String>,
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

    let () = netstack.set_interface_status(nicid as u32, true)?;
    log::info!("Added ethernet to stack.");

    let subnet: Option<fidl_fuchsia_net::Subnet> = opt.ip.as_ref().map(|ip| {
        ip.parse::<fidl_fuchsia_net_ext::Subnet>().expect("Can't parse provided ip").into()
    });

    if let Some(mut subnet) = subnet {
        let _ = netstack
            .set_interface_address(nicid as u32, &mut subnet.addr, subnet.prefix_len)
            .await
            .context("set interface address error")?;
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

        let stack = client::connect_to_service::<StackMarker>()?;
        let () = stack
            .add_forwarding_entry(&mut fidl_fuchsia_net_stack::ForwardingEntry {
                subnet: fidl_fuchsia_net::Subnet { addr: unspec_addr, prefix_len: 0 },
                destination: fidl_fuchsia_net_stack::ForwardingDestination::NextHop(gw_addr),
            })
            .await
            .squash_result()
            .context("failed to add forwarding entry for gateway")?;

        log::info!("Configured the default route with gateway address.");
    }

    log::info!("Waiting for interface up...");
    let (if_id, hwaddr) = netstack
        .take_event_stream()
        .try_filter_map(|fidl_fuchsia_netstack::NetstackEvent::OnInterfacesChanged { interfaces }| {
            if let Some(iface) = interfaces.iter().find(|iface| iface.name == opt.endpoint) {
                if !opt.skip_up_check {
                    if !iface.flags.contains(fidl_fuchsia_netstack::Flags::Up) {
                        log::info!("Found interface, but it's down. waiting.");
                        return futures::future::ok(None);
                    }

                    // If subnet is an IPv6 address, make sure the interface has
                    // the address assigned so we know DAD has resolved.
                    if let Some(subnet) = subnet {
                        if let fidl_fuchsia_net::IpAddress::Ipv6(_) = subnet.addr {
                            if !iface.ipv6addrs.iter().any(|x| x.addr == subnet.addr)  {
                                log::info!("Found interface, but IPv6 address is not resolved. waiting.");
                                return futures::future::ok(None);
                            }
                        }
                    }
                }

                return futures::future::ok(Some((iface.id, iface.hwaddr.clone())));
            }

            futures::future::ok(None)
        })
        .try_next()
        .await
        .context("wait for interfaces")?
        .ok_or_else(|| format_err!("interface added"))?;

    log::info!("Found ethernet with id {} {:?}", if_id, hwaddr);

    Ok(())
}

fn main() -> Result<(), Error> {
    let () = fuchsia_syslog::init().context("cannot init logger")?;

    let opt = Opt::from_args();
    let mut executor = fasync::Executor::new().context("Error creating executor")?;
    executor.run_singlethreaded(config_netstack(opt))
}
