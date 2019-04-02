// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro, futures_api)]

use failure::{format_err, Error, ResultExt};
use fdio;
use fidl_fuchsia_hardware_ethernet as zx_eth;
use fidl_fuchsia_net as net;
use fidl_fuchsia_net_stack::{self as netstack, StackMarker, StackProxy};
use fuchsia_app::client;
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use std::fs::{self, File};
use std::os::unix::io::AsRawFd;
use structopt::StructOpt;

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

struct Netstack {
    stack: StackProxy,
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

fn copy_ip(ip: &net::IpAddress) -> net::IpAddress {
    match ip {
        net::IpAddress::Ipv4(addr) => {
            net::IpAddress::Ipv4(net::Ipv4Address { addr: addr.addr.clone() })
        }
        net::IpAddress::Ipv6(addr) => {
            net::IpAddress::Ipv6(net::Ipv6Address { addr: addr.addr.clone() })
        }
    }
}

impl Netstack {
    fn new() -> Result<Self, Error> {
        Ok(Self { stack: client::connect_to_service::<StackMarker>()? })
    }

    async fn add_ethernet(&self, path: String) -> Result<u64, Error> {
        let dev = File::open(&path).context("failed to open device")?;
        let topological_path =
            fdio::device_get_topo_path(&dev).context("failed to get topological path")?;
        let fd = dev.as_raw_fd();
        let mut client = 0;
        zx::Status::ok(unsafe { fdio::fdio_sys::fdio_get_service_handle(fd, &mut client) })
            .context("failed to get fdio service handle")?;
        let dev = fidl::endpoints::ClientEnd::<zx_eth::DeviceMarker>::new(
            // Safe because we checked the return status above.
            zx::Channel::from(unsafe { zx::Handle::from_raw(client) }),
        );
        let (err, id) = await!(self.stack.add_ethernet_interface(&topological_path, dev))
            .context("error adding device")?;

        match err {
            Some(e) => Err(format_err!("Error adding interface {}: {:?}", path, e)),
            None => Ok(id),
        }
    }

    async fn set_ip(
        &self,
        id: u64,
        ip_address: net::IpAddress,
        prefix_len: u8,
    ) -> Result<(), Error> {
        let mut fidl_addr =
            netstack::InterfaceAddress { ip_address: copy_ip(&ip_address), prefix_len };
        let response = await!(self.stack.add_interface_address(id, &mut fidl_addr))
            .context("error setting interface address")?;

        if let Some(e) = response {
            return Err(format_err!("Error adding interface address {}: {:?}", id, e));
        }

        let response = await!(self.stack.add_forwarding_entry(&mut netstack::ForwardingEntry {
            subnet: net::Subnet { addr: mask_with_prefix(ip_address, prefix_len), prefix_len },
            destination: netstack::ForwardingDestination::DeviceId(id),
        }))
        .context("error adding forwarding entry")?;

        match response {
            Some(e) => Err(format_err!("Error adding forwarding entry: {:?}", e)),
            None => Ok(()),
        }
    }
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let options = Opt::from_args();
    let stack = Netstack::new()?;
    if let Some(eth_path) = options.ethernet {
        // open ethernet device and send to stack.
        let id = await!(stack.add_ethernet(eth_path))?;
        println!("Created interface with id {}", id);

        if let Some(ip_prefix) = options.ip_prefix {
            let (addr, prefix) = parse_ip_addr_and_prefix(&ip_prefix)?;
            await!(stack.set_ip(id, addr, prefix))?;
            println!("Configured interface {} for address: {}", id, ip_prefix);
        }
    }

    let env_name = fs::read_to_string("/hub/name")?;
    println!("enclosed netstack is running...");
    println!("environment name: {}", env_name);
    println!("run:");
    println!("chrealm /hub/r/{}/[koid]", env_name);
    println!("to shell into the tailored environment (you can use tab completions for koid)");
    println!("to stop the netstack and this environment, run:");
    println!("killall enclosed_runner.cmx");
    println!("or CTRL^C out.");

    // Await forever on an empty future.
    // This will cause enclosed_runner to never exit, unless it's killed
    // from outside. We want enclosed_runner to keep alive so we will have
    // the test environment alive as well so we can chrealm into it, or just
    // observe netstack3.
    let () = await!(futures::future::empty());
    Ok(())
}
