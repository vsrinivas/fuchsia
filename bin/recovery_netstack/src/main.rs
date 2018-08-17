// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A networking stack.

#![feature(async_await, await_macro)]
#![feature(const_fn)]
#![feature(never_type)]
#![feature(nll)]
#![feature(specialization)]
#![feature(try_from)]
// In case we roll the toolchain and something we're using as a feature has been
// stabilized.
#![allow(stable_features)]
// We use repr(packed) in some places (particularly in the wire module) to
// create structs whose layout matches the layout of network packets on the
// wire. This ensures that the compiler will stop us from using repr(packed) in
// an unsound manner without using unsafe code.
#![deny(safe_packed_borrows)]
#![deny(missing_docs)]
#![deny(unreachable_patterns)]

use ethernet as eth;
use fuchsia_async as fasync;
use fuchsia_zircon as zx;

mod eventloop;


use crate::eventloop::EventLoop;
use failure::ResultExt;
use futures::prelude::*;
use netstack_core;

use std::env;
use std::fs::File;

const DEFAULT_ETH: &str = "/dev/class/ethernet/000";
// Hardcoded IPv4 address: if you use something other than a 192.168.1/24, update the subnet below.
const FIXED_IPADDR: netstack_core::Ipv4Addr = netstack_core::Ipv4Addr::new([192, 168, 1, 39]);

fn main() -> Result<(), failure::Error> {
    fuchsia_syslog::init()?;
    // Severity is set to debug during development.
    fuchsia_syslog::set_severity(-1);

    let event_loop = EventLoop {};
    let mut state = netstack_core::StackState::default();

    let vmo = zx::Vmo::create_with_opts(
        zx::VmoOptions::NON_RESIZABLE,
        256 * eth::DEFAULT_BUFFER_SIZE as u64,
    )?;

    let mut executor = fasync::Executor::new().context("could not create executor")?;

    let path = env::args()
        .nth(1)
        .unwrap_or_else(|| String::from(DEFAULT_ETH));
    let dev = File::open(path)?;
    let client = eth::Client::new(dev, vmo, eth::DEFAULT_BUFFER_SIZE, "recovery-ns")?;
    let mac = client.info()?.mac;

    client.start()?;
    let eth_id = state.add_ethernet_device(netstack_core::Mac::new(mac));
    // Hardcoded subnet: if you update the IPADDR above, update this as well.
    let fixed_subnet =
        netstack_core::Subnet::new(netstack_core::Ipv4Addr::new([192, 168, 1, 0]), 24);
    netstack_core::set_ip_addr(&mut state, eth_id, FIXED_IPADDR, fixed_subnet);

    let mut buf = [0; 2048];
    let mut events = client.get_stream();
    let fut = async {
        while let Some(evt) = await!(events.try_next())? {
            if let eth::Event::Receive(rx) = evt {
                let len = rx.read(&mut buf);
                netstack_core::receive_frame(&mut state, eth_id, &mut buf[..len]);
            } else {
                println!("unhandled Ethernet event: {:?}", evt);
            }
        }
        Ok(())
    };
    executor.run_singlethreaded(fut)
}
