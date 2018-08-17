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

#[macro_use]
mod macros;

mod device;
mod error;
mod ip;
#[cfg(test)]
mod testutil;
mod transport;
mod wire;

pub use crate::device::{ethernet::Mac, receive_frame, set_ip_addr, DeviceId};
pub use crate::ip::{Ipv4Addr, Subnet};

use crate::device::DeviceLayerState;
use crate::ip::IpLayerState;
use crate::transport::TransportLayerState;

/// The state associated with the network stack.
#[derive(Default)]
pub struct StackState {
    transport: TransportLayerState,
    ip: IpLayerState,
    device: DeviceLayerState,
    #[cfg(test)]
    test_counters: testutil::TestCounters,
}

impl StackState {
    /// Add a new ethernet device to the device layer.
    pub fn add_ethernet_device(&mut self, mac: Mac) -> DeviceId {
        self.device.add_ethernet_device(mac)
    }
}
