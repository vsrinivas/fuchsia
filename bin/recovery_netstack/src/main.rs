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
#[macro_use]
extern crate failure;
extern crate fuchsia_syslog;
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
use ip::IpLayerState;
use eventloop::EventLoop;
use transport::TransportLayerState;

fn main() -> Result<(), failure::Error> {
    fuchsia_syslog::init()?;
    // Severity is set to debug during development.
    fuchsia_syslog::set_severity(-1);

    let event_loop = EventLoop{};
    Ok(())
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
