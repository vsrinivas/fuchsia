// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro, futures_api)]

use {
    failure::{format_err, Error, ResultExt},
    fidl_fuchsia_net,
    fidl_fuchsia_netemul_network::{EndpointManagerMarker, NetworkContextMarker},
    fidl_fuchsia_netstack::{InterfaceConfig, IpAddressConfig, NetstackMarker},
    fuchsia_app::client,
    fuchsia_async::{self as fasync, TimeoutExt},
    fuchsia_zircon::DurationNum,
    futures::TryStreamExt,
    structopt::StructOpt,
};

#[derive(StructOpt, Debug)]
#[structopt(name = "netstack_cfg")]
/// Configure netstack from emulated environments.
struct Opt {
    #[structopt(short = "e")]
    /// Endpoint name to retrieve from network.EndpointManager
    endpoint: String,
    #[structopt(short = "i")]
    /// Static ip address to assign (don't forget /prefix termination). Omit to use DHCP.
    ip: Option<String>,
}

const TIMEOUT_SECS: i64 = 5;
const IGNORED_IP_ADDRESS_CONFIG: IpAddressConfig = IpAddressConfig::Dhcp(true);

async fn config_netstack(opt: Opt) -> Result<(), Error> {
    println!("Configuring endpoint {}", opt.endpoint);

    // get the network context service:
    let netctx = client::connect_to_service::<NetworkContextMarker>()?;
    // get the endpoint manager
    let (epm, epmch) = fidl::endpoints::create_proxy::<EndpointManagerMarker>()?;
    netctx.get_endpoint_manager(epmch)?;

    // retrieve the created endpoint:
    let ep = await!(epm.get_endpoint(&opt.endpoint))?;
    let ep = ep.ok_or_else(|| format_err!("can't find endpoint {}", opt.endpoint))?.into_proxy()?;
    // and the ethernet device:
    let eth = await!(ep.get_ethernet_device())?;

    let if_name = format!("eth-{}", opt.endpoint);
    // connect to netstack:
    let netstack = client::connect_to_service::<NetstackMarker>()?;

    let mut if_changed = netstack.take_event_stream().try_filter_map(
        |fidl_fuchsia_netstack::NetstackEvent::OnInterfacesChanged { interfaces }| {
            let iface = interfaces.iter().filter(|iface| iface.name == if_name).next();
            match iface {
                None => futures::future::ok(None),
                Some(a) => futures::future::ok(Some((a.id, a.hwaddr.clone()))),
            }
        },
    );
    let mut cfg =
        InterfaceConfig { name: if_name.to_string(), ip_address_config: IGNORED_IP_ADDRESS_CONFIG };
    let nicid =
        await!(netstack.add_ethernet_device(&format!("/vdev/{}", opt.endpoint), &mut cfg, eth))
            .context("can't add ethernet device")?;
    let () = netstack.set_interface_status(nicid as u32, true)?;

    if let Some(ip) = opt.ip {
        let mut subnet: fidl_fuchsia_net::Subnet =
            ip.parse::<fidl_fuchsia_net_ext::Subnet>().expect("Can't parse provided ip").into();
        let _ = await!(netstack.set_interface_address(
            nicid as u32,
            &mut subnet.addr,
            subnet.prefix_len
        ))?;
    } else {
        let _ = await!(netstack.set_dhcp_client_status(nicid as u32, true))?;
    };

    let (if_id, hwaddr) = await!(if_changed.try_next())
        .context("wait for interfaces")?
        .ok_or_else(|| format_err!("interface added"))?;

    println!("Found ethernet with id {} {:?}", if_id, hwaddr);

    Ok(())
}

fn main() -> Result<(), Error> {
    let opt = Opt::from_args();
    let mut executor = fasync::Executor::new().context("Error creating executor")?;
    executor.run_singlethreaded(
        config_netstack(opt).on_timeout(TIMEOUT_SECS.seconds().after_now(), || {
            Err(format_err!("Netstack setup timed out"))
        }),
    )
}
