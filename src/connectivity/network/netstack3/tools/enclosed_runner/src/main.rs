// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Context as _, Error};
use fidl_fuchsia_hardware_ethernet as zx_eth;
use fidl_fuchsia_net as net;
use fidl_fuchsia_net_stack::{self as netstack, StackMarker};
use fidl_fuchsia_net_stack_ext::FidlReturn;
use fidl_fuchsia_posix_socket;
use fuchsia_async as fasync;
use fuchsia_component::{client::AppBuilder, server::ServiceFs};
use fuchsia_zircon as zx;
use futures::prelude::*;
use std::fs::File;
use std::os::unix::io::AsRawFd;
use structopt::StructOpt;

const NETSTACK_URL: &'static str = "fuchsia-pkg://fuchsia.com/netstack3#meta/netstack3.cmx";

#[derive(StructOpt, Debug)]
struct Opt {
    /// Path to the ethernet device to add to netstack,
    /// usually somewhere in /dev/class/ethernet
    #[structopt(short = "e")]
    ethernet: Option<String>,
    /// If ethernet is provided, also set a fixed IP address and subnet prefix
    /// to it, including routing table information.
    /// IP address and subnet prefix  MUST be in the form [IP]/[prefix].
    #[structopt(short = "i")]
    ip_prefix: Option<String>,
}

fn parse_ip_addr_and_prefix(addr: &str) -> Result<(net::IpAddress, u8), Error> {
    let (addr, prefix) = if let [addr, prefix] = addr.split("/").collect::<Vec<_>>()[..] {
        (addr, prefix)
    } else {
        return Err(format_err!("Missing prefix value on {}", addr));
    };

    let prefix: u8 = prefix.parse()?;
    let addr = match addr.parse()? {
        ::std::net::IpAddr::V4(ipv4) => {
            net::IpAddress::Ipv4(net::Ipv4Address { addr: ipv4.octets() })
        }
        ::std::net::IpAddr::V6(ipv6) => {
            net::IpAddress::Ipv6(net::Ipv6Address { addr: ipv6.octets() })
        }
    };

    Ok((addr, prefix))
}

fn mask_bits(bits: &mut [u8], mut prefix: u8) {
    for i in 0..bits.len() {
        bits[i] &= if prefix == 0 { 0 } else { 255u8 << 8u8.saturating_sub(prefix) };
        prefix = prefix.saturating_sub(8);
    }
}

fn mask_with_prefix(mut ip: net::IpAddress, prefix: u8) -> net::IpAddress {
    match ip {
        net::IpAddress::Ipv4(ref mut addr) => {
            mask_bits(&mut addr.addr, prefix);
        }
        net::IpAddress::Ipv6(ref mut addr) => {
            mask_bits(&mut addr.addr, prefix);
        }
    }
    ip
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let Opt { ethernet, ip_prefix } = Opt::from_args();

    let mut ns_builder = AppBuilder::new(NETSTACK_URL.to_string());
    let directory_request = ns_builder.directory_request().context("directory request")?;

    let mut services = ServiceFs::new_local();
    let _: &mut ServiceFs<_> = services
        .add_proxy_service_to::<StackMarker, _>(directory_request.clone())
        .add_proxy_service_to::<fidl_fuchsia_posix_socket::ProviderMarker, _>(
            directory_request.clone(),
        );
    let env = services.create_nested_environment("netstack3-env").context("create environment")?;
    let services = services.for_each_concurrent(None, futures::future::ready);

    let netstack = ns_builder.spawn(env.launcher()).context("spawn")?;

    if let Some(path) = ethernet {
        let stack = netstack.connect_to_protocol::<StackMarker>().context("connect to Stack")?;

        let dev = File::open(&path).context("open device")?;
        let topological_path = fdio::device_get_topo_path(&dev).context("get topological path")?;
        let fd = dev.as_raw_fd();
        let mut client = 0;
        zx::Status::ok(unsafe { fdio::fdio_sys::fdio_get_service_handle(fd, &mut client) })
            .context("get fdio service handle")?;
        let dev = fidl::endpoints::ClientEnd::<zx_eth::DeviceMarker>::new(
            // Safe because we checked the return status above.
            zx::Channel::from(unsafe { zx::Handle::from_raw(client) }),
        );
        let id = stack
            .add_ethernet_interface(&topological_path, dev)
            .await
            .squash_result()
            .context("add ethernet device")?;
        println!("Created interface with id {}", id);

        if let Some(ip_prefix) = ip_prefix {
            let (ip_address, prefix_len) =
                parse_ip_addr_and_prefix(&ip_prefix).context("parse IP")?;
            let () = stack
                .add_interface_address(
                    id,
                    &mut net::Subnet { addr: ip_address.clone(), prefix_len },
                )
                .await
                .squash_result()
                .context("add interface address")?;
            let () = stack
                .add_forwarding_entry(&mut netstack::ForwardingEntry {
                    subnet: net::Subnet {
                        addr: mask_with_prefix(ip_address, prefix_len),
                        prefix_len,
                    },
                    destination: netstack::ForwardingDestination::DeviceId(id),
                })
                .await
                .squash_result()
                .context("add forwarding entry")?;
            println!("Configured interface {} for address: {}", id, ip_prefix);
        }
    }

    println!("enclosed netstack is running...");
    println!("run:");
    println!("chrealm /hub/r/netstack3-env/[koid]");
    println!("to shell into the tailored environment (you can use tab completions for koid)");
    println!("to stop the netstack and this environment, run:");
    println!("killall enclosed-runner.cmx");
    println!("or CTRL^C out.");

    Ok(services.await)
}
