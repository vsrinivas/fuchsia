// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(futures_api, pin, arbitrary_self_types)]
#![deny(warnings)]

extern crate failure;
extern crate fidl;
extern crate fidl_fuchsia_net as net;
extern crate fidl_fuchsia_net_stack as netstack;
extern crate fuchsia_app as component;
#[macro_use]
extern crate fuchsia_async as async;
extern crate fuchsia_zircon as zx;
extern crate futures;
#[macro_use]
extern crate structopt;

use component::client::connect_to_service;
use failure::{Error, Fail, ResultExt};
use futures::prelude::*;
use netstack::{StackMarker, StackProxy};
use structopt::StructOpt;

mod opts;
mod pretty;

use opts::*;

fn main() -> Result<(), Error> {
    let opt = Opt::from_args();
    let mut exec = async::Executor::new().context("error creating event loop")?;
    let stack = connect_to_service::<StackMarker>().context("failed to connect to netstack")?;

    match opt {
        Opt::If(cmd) => exec.run_singlethreaded(do_if(cmd, stack)),
        Opt::Fwd(cmd) => exec.run_singlethreaded(do_fwd(cmd, stack)),
    }
}

unsafe_many_futures!(
    IfFut,
    [List, GetInfo, Enable, Disable, AddrAdd, AddrDel, Error]
);

fn do_if(cmd: opts::IfCmd, stack: StackProxy) -> impl Future<Output = Result<(), Error>> {
    match cmd {
        IfCmd::List => IfFut::List({
            stack
                .list_interfaces()
                .map_err(|e| e.context("error getting response").into())
                .map_ok(|response| {
                    for info in response {
                        println!("{}", pretty::InterfaceInfo::from(info));
                    }
                })
        }),
        IfCmd::Get { id } => IfFut::GetInfo({
            stack
                .get_interface_info(id)
                .map_err(|e| e.context("error getting response").into())
                .map_ok(move |response| {
                    if let Some(e) = response.1 {
                        println!("Error getting interface {}: {:?}", id, e)
                    } else {
                        println!("{}", pretty::InterfaceInfo::from(*response.0.unwrap()))
                    }
                })
        }),
        IfCmd::Enable { id } => IfFut::Enable({
            stack
                .enable_interface(id)
                .map_err(|e| e.context("error getting response").into())
                .map_ok(move |response| {
                    if let Some(e) = response {
                        println!("Error enabling interface {}: {:?}", id, e)
                    } else {
                        println!("Interface {} enabled", id)
                    }
                })
        }),
        IfCmd::Disable { id } => IfFut::Disable({
            stack
                .disable_interface(id)
                .map_err(|e| e.context("error getting response").into())
                .map_ok(move |response| {
                    if let Some(e) = response {
                        println!("Error disabling interface {}: {:?}", id, e)
                    } else {
                        println!("Interface {} disabled", id)
                    }
                })
        }),
        IfCmd::Addr(AddrCmd::Add { id, addr, prefix }) => IfFut::AddrAdd({
            let parsed_addr = match parse_ip_addr(&addr) {
                Ok(a) => a,
                Err(e) => return IfFut::Error({ futures::future::ready(Err(e)) }),
            };
            let mut fidl_addr = netstack::InterfaceAddress {
                ip_address: parsed_addr,
                prefix_len: prefix,
                peer_address: None,
            };
            stack
                .add_interface_address(id, &mut fidl_addr)
                .map_err(|e| e.context("error setting interface address").into())
                .map_ok(move |response| {
                    if let Some(e) = response {
                        println!("Error adding interface address {}: {:?}", id, e)
                    } else {
                        println!(
                            "Address {} added to interface {}",
                            pretty::InterfaceAddress::from(&fidl_addr),
                            id
                        )
                    }
                })
        }),
        IfCmd::Addr(AddrCmd::Del { .. }) => IfFut::AddrDel({
            println!("{:?} not implemented!", cmd);
            futures::future::ready(Ok(()))
        }),
    }
}

unsafe_many_futures!(FwdFut, [List,]);

fn do_fwd(cmd: opts::FwdCmd, stack: StackProxy) -> impl Future<Output = Result<(), Error>> {
    match cmd {
        FwdCmd::List => FwdFut::List({
            stack
                .get_forwarding_table()
                .map_err(|e| e.context("error retrieving forwarding table").into())
                .map_ok(move |response| {
                    for entry in response {
                        println!("{}", pretty::ForwardingEntry::from(entry));
                    }
                })
        }),
    }
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
