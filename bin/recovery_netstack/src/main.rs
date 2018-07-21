// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A networking stack.

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

extern crate byteorder;
extern crate ethernet as eth;
#[macro_use]
extern crate failure;
extern crate fuchsia_async as fasync;
extern crate fuchsia_syslog;
extern crate fuchsia_zircon as zx;
extern crate futures;
#[macro_use]
extern crate log;
#[cfg(test)]
extern crate rand;
extern crate zerocopy;

#[macro_use]
mod macros;

// mark all modules as public so that deny(missing_docs) will be more powerful
pub mod device;
pub mod error;
pub mod eventloop;
pub mod ip;
#[cfg(test)]
pub mod testutil;
pub mod transport;
pub mod wire;

use device::DeviceLayerState;
use eventloop::EventLoop;
use failure::{Error, ResultExt};
use futures::prelude::*;
use ip::IpLayerState;
use transport::TransportLayerState;

use std::env;
use std::fs::File;

const DEFAULT_ETH: &str = "/dev/class/ethernet/000";
// Hardcoded IPv4 address: if you use something other than a /24, update the subnet below as well.
const FIXED_IPADDR: ip::Ipv4Addr = ip::Ipv4Addr::new([192, 168, 1, 39]);

fn main() -> Result<(), failure::Error> {
    fuchsia_syslog::init()?;
    // Severity is set to debug during development.
    fuchsia_syslog::set_severity(-1);

    let event_loop = EventLoop {};
    let mut state = StackState::default();

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

    client.start()?;
    let eth_id = state
        .device
        .add_ethernet_device(device::ethernet::EthernetDeviceState::default());
    // Hardcoded subnet: if you update the IPADDR above to use a network that's not /24, update
    // this as well.
    let fixed_subnet = ip::Subnet::new(ip::Ipv4Addr::new([255, 255, 255, 0]), 24);
    device::ethernet::set_ip_addr(&mut state, eth_id.id(), FIXED_IPADDR, fixed_subnet);

    let mut buf = [0; 2048];
    let stream = client.get_stream().for_each(|evt| {
        match evt {
            eth::Event::Receive(rx) => {
                let len = rx.read(&mut buf);
                device::receive_frame(&mut state, eth_id, &mut buf[..len]);
            }
            other => println!("unhandled Ethernet event: {:?}", other),
        }
        futures::future::ok(())
    });
    executor
        .run_singlethreaded(stream)
        .map(|_| ())
        .map_err(Into::into)
}

/// The state associated with the network stack.
#[allow(missing_docs)]
#[derive(Default)]
pub struct StackState {
    pub transport: TransportLayerState,
    pub ip: IpLayerState,
    pub device: DeviceLayerState,
    #[cfg(test)]
    pub test_counters: testutil::TestCounters,
}
