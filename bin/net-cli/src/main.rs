// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro, futures_api, pin, arbitrary_self_types)]
#![deny(warnings)]

use failure::{Error, ResultExt};
use fidl_fuchsia_net as net;
use fidl_fuchsia_net_stack::{self as netstack, StackMarker, StackProxy};
use fuchsia_app::client::connect_to_service;
use fuchsia_async as fasync;
use structopt::StructOpt;

mod opts;
mod pretty;

use crate::opts::*;

fn main() -> Result<(), Error> {
    let opt = Opt::from_args();
    let mut exec = fasync::Executor::new().context("error creating event loop")?;
    let stack = connect_to_service::<StackMarker>().context("failed to connect to netstack")?;

    let fut = async {
        match opt {
            Opt::If(cmd) => await!(do_if(cmd, stack)),
            Opt::Fwd(cmd) => await!(do_fwd(cmd, stack)),
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
        IfCmd::Get { id } => {
            let response = await!(stack.get_interface_info(id)).context("error getting response")?;
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
            let mut fidl_addr = netstack::InterfaceAddress {
                ip_address: parsed_addr,
                prefix_len: prefix,
                peer_address: None,
            };
            let response = await!(stack.add_interface_address(id, &mut fidl_addr))
                .context("error setting interface address")?;
            if let Some(e) = response {
                println!("Error adding interface address {}: {:?}", id, e)
            } else {
                println!("Address {} added to interface {}",
                         pretty::InterfaceAddress::from(&fidl_addr),
                         id)
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
    }
    Ok(())
}

fn parse_ip_addr(addr: &str) -> Result<net::IpAddress, Error> {
    match addr.parse()? {
        ::std::net::IpAddr::V4(ipv4) => Ok(net::IpAddress::Ipv4(net::IPv4Address {
            addr: ipv4.octets(),
        })),
        ::std::net::IpAddr::V6(ipv6) => Ok(net::IpAddress::Ipv6(net::IPv6Address {
            addr: ipv6.octets(),
        })),
    }
}
