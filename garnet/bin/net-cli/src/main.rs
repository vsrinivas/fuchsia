// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro)]
#![deny(warnings)]

use failure::{Error, ResultExt};
use fdio;
use fidl_fuchsia_hardware_ethernet as zx_eth;
use fidl_fuchsia_net as net;
use fidl_fuchsia_net_filter::{self as filter, FilterMarker, FilterProxy};
use fidl_fuchsia_net_stack::{self as netstack, StackMarker, StackProxy};
use fidl_fuchsia_net_stack_ext as pretty;
use fuchsia_component::client::connect_to_service;
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use std::fs::File;
use std::os::unix::io::AsRawFd;
use structopt::StructOpt;

mod opts;

use crate::opts::*;

fn main() -> Result<(), Error> {
    let opt = Opt::from_args();
    let mut exec = fasync::Executor::new().context("error creating event loop")?;
    let stack = connect_to_service::<StackMarker>().context("failed to connect to netstack")?;
    let filter = connect_to_service::<FilterMarker>().context("failed to connect to netfilter")?;

    let fut = async {
        match opt {
            Opt::If(cmd) => await!(do_if(cmd, stack)),
            Opt::Fwd(cmd) => await!(do_fwd(cmd, stack)),
            Opt::Filter(cmd) => await!(do_filter(cmd, filter)),
        }
    };
    exec.run_singlethreaded(fut)
}

async fn do_if(cmd: opts::IfCmd, stack: StackProxy) -> Result<(), Error> {
    match cmd {
        IfCmd::List => {
            let response = await!(stack.list_interfaces()).context("error getting response")?;
            for info in response {
                println!("{}", pretty::InterfaceInfo::from(info));
            }
        }
        IfCmd::Add { path } => {
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
            let (err, id) = await!(stack.add_ethernet_interface(&topological_path, dev))
                .context("error adding device")?;
            if let Some(e) = err {
                println!("Error adding interface {}: {:?}", path, e)
            } else {
                println!("Added interface {}", id)
            }
        }
        IfCmd::Del { id } => {
            let response =
                await!(stack.del_ethernet_interface(id)).context("error removing device")?;
            if let Some(e) = response {
                println!("Error removing interface {}: {:?}", id, e)
            }
        }
        IfCmd::Get { id } => {
            let response =
                await!(stack.get_interface_info(id)).context("error getting response")?;
            if let Some(e) = response.1 {
                println!("Error getting interface {}: {:?}", id, e)
            } else {
                println!("{}", pretty::InterfaceInfo::from(*response.0.unwrap()))
            }
        }
        IfCmd::Enable { id } => {
            let response = await!(stack.enable_interface(id)).context("error getting response")?;
            if let Some(e) = response {
                println!("Error enabling interface {}: {:?}", id, e)
            } else {
                println!("Interface {} enabled", id)
            }
        }
        IfCmd::Disable { id } => {
            let response = await!(stack.disable_interface(id)).context("error getting response")?;
            if let Some(e) = response {
                println!("Error disabling interface {}: {:?}", id, e)
            } else {
                println!("Interface {} disabled", id)
            }
        }
        IfCmd::Addr(AddrCmd::Add { id, addr, prefix }) => {
            let parsed_addr = parse_ip_addr(&addr)?;
            let mut fidl_addr =
                netstack::InterfaceAddress { ip_address: parsed_addr, prefix_len: prefix };
            let response = await!(stack.add_interface_address(id, &mut fidl_addr))
                .context("error setting interface address")?;
            if let Some(e) = response {
                println!("Error adding interface address {}: {:?}", id, e)
            } else {
                println!(
                    "Address {} added to interface {}",
                    pretty::InterfaceAddress::from(fidl_addr),
                    id
                )
            }
        }
        IfCmd::Addr(AddrCmd::Del { .. }) => {
            println!("{:?} not implemented!", cmd);
        }
    }
    Ok(())
}

async fn do_fwd(cmd: opts::FwdCmd, stack: StackProxy) -> Result<(), Error> {
    match cmd {
        FwdCmd::List => {
            let response = await!(stack.get_forwarding_table())
                .context("error retrieving forwarding table")?;
            for entry in response {
                println!("{}", pretty::ForwardingEntry::from(entry));
            }
        }
        FwdCmd::AddDevice { id, addr, prefix } => {
            let response =
                await!(stack.add_forwarding_entry(&mut fidl_fuchsia_net_stack::ForwardingEntry {
                    subnet: net::Subnet { addr: parse_ip_addr(&addr)?, prefix_len: prefix },
                    destination: fidl_fuchsia_net_stack::ForwardingDestination::DeviceId(id),
                }))
                .context("error adding forwarding entry")?;
            if let Some(e) = response {
                println!("Error adding forwarding entry: {:?}", e);
            } else {
                println!("Added forwarding entry for {}/{} to device {}", addr, prefix, id);
            }
        }
        FwdCmd::AddHop { next_hop, addr, prefix } => {
            let response =
                await!(stack.add_forwarding_entry(&mut fidl_fuchsia_net_stack::ForwardingEntry {
                    subnet: net::Subnet { addr: parse_ip_addr(&addr)?, prefix_len: prefix },
                    destination: fidl_fuchsia_net_stack::ForwardingDestination::NextHop(
                        parse_ip_addr(&next_hop)?
                    ),
                }))
                .context("error adding forwarding entry")?;
            if let Some(e) = response {
                println!("Error adding forwarding entry: {:?}", e);
            } else {
                println!("Added forwarding entry for {}/{} to {}", addr, prefix, next_hop);
            }
        }
        FwdCmd::Del { addr, prefix } => {
            let response = await!(stack.del_forwarding_entry(&mut net::Subnet {
                addr: parse_ip_addr(&addr)?,
                prefix_len: prefix,
            }))
            .context("error removing forwarding entry")?;
            if let Some(e) = response {
                println!("Error removing forwarding entry: {:?}", e);
            } else {
                println!("Removed forwarding entry for {}/{}", addr, prefix);
            }
        }
    }
    Ok(())
}

fn parse_ip_addr(addr: &str) -> Result<net::IpAddress, Error> {
    match addr.parse()? {
        ::std::net::IpAddr::V4(ipv4) => {
            Ok(net::IpAddress::Ipv4(net::Ipv4Address { addr: ipv4.octets() }))
        }
        ::std::net::IpAddr::V6(ipv6) => {
            Ok(net::IpAddress::Ipv6(net::Ipv6Address { addr: ipv6.octets() }))
        }
    }
}

async fn do_filter(cmd: opts::FilterCmd, filter: FilterProxy) -> Result<(), Error> {
    match cmd {
        FilterCmd::Enable => {
            let status = await!(filter.enable(true)).context("error getting response")?;
            println!("{:?}", status);
        }
        FilterCmd::Disable => {
            let status = await!(filter.enable(false)).context("error getting response")?;
            println!("{:?}", status);
        }
        FilterCmd::IsEnabled => {
            let is_enabled = await!(filter.is_enabled()).context("error getting response")?;
            println!("{:?}", is_enabled);
        }
        FilterCmd::GetRules => {
            let (rules, generation, status) =
                await!(filter.get_rules()).context("error getting response")?;
            if status == filter::Status::Ok {
                println!("{:?} (generation {})", rules, generation);
            } else {
                eprintln!("{:?}", status);
            }
        }
        FilterCmd::SetRules { rules } => {
            let (_cur_rules, generation, status) =
                await!(filter.get_rules()).context("error getting response")?;
            if status != filter::Status::Ok {
                println!("{:?}", status);
                return Ok(());
            }
            match netfilter::parser::parse_str_to_rules(&rules) {
                Ok(mut rules) => {
                    let status = await!(filter.update_rules(&mut rules.iter_mut(), generation))
                        .context("error getting response")?;
                    println!("{:?}", status);
                }
                Err(e) => eprintln!("{:?}", e),
            }
        }
        FilterCmd::GetNatRules => {
            let (rules, generation, status) =
                await!(filter.get_nat_rules()).context("error getting response")?;
            if status == filter::Status::Ok {
                println!("{:?} (generation {})", rules, generation);
            } else {
                eprintln!("{:?}", status);
            }
        }
        FilterCmd::SetNatRules { rules } => {
            let (_cur_rules, generation, status) =
                await!(filter.get_nat_rules()).context("error getting response")?;
            if status != filter::Status::Ok {
                println!("{:?}", status);
                return Ok(());
            }
            match netfilter::parser::parse_str_to_nat_rules(&rules) {
                Ok(mut rules) => {
                    let status = await!(filter.update_nat_rules(&mut rules.iter_mut(), generation))
                        .context("error getting response")?;
                    println!("{:?}", status);
                }
                Err(e) => eprintln!("{:?}", e),
            }
        }
        FilterCmd::GetRdrRules => {
            let (rules, generation, status) =
                await!(filter.get_rdr_rules()).context("error getting response")?;
            if status == filter::Status::Ok {
                println!("{:?} (generation {})", rules, generation);
            } else {
                eprintln!("{:?}", status);
            }
        }
        FilterCmd::SetRdrRules { rules } => {
            let (_cur_rules, generation, status) =
                await!(filter.get_rdr_rules()).context("error getting response")?;
            if status != filter::Status::Ok {
                println!("{:?}", status);
                return Ok(());
            }
            match netfilter::parser::parse_str_to_rdr_rules(&rules) {
                Ok(mut rules) => {
                    let status = await!(filter.update_rdr_rules(&mut rules.iter_mut(), generation))
                        .context("error getting response")?;
                    println!("{:?}", status);
                }
                Err(e) => eprintln!("{:?}", e),
            }
        }
    }
    Ok(())
}
