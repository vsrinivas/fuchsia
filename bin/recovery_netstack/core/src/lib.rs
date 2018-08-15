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

pub use crate::device::{ethernet::Mac, receive_frame, set_ip_addr, DeviceId,
                        DeviceLayerEventDispatcher};
pub use crate::ip::{Ipv4Addr, Subnet};
pub use crate::transport::TransportLayerEventDispatcher;

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

/// Context available during the execution of the netstack.
///
/// `Context` provides access to the state of the netstack and to an event
/// dispatcher which can be used to emit events and schedule timeouts. A mutable
/// reference to a `Context` is passed to every function in the netstack.
pub struct Context<D: EventDispatcher> {
    state: StackState,
    dispatcher: D,
}

impl<D: EventDispatcher> Context<D> {
    /// Construct a new `Context`.
    pub fn new(state: StackState, dispatcher: D) -> Context<D> {
        Context { state, dispatcher }
    }

    /// Get the stack state.
    pub fn state(&mut self) -> &mut StackState {
        &mut self.state
    }

    /// Get the dispatcher.
    pub fn dispatcher(&mut self) -> &mut D {
        &mut self.dispatcher
    }
}

/// An object which can dispatch events to a real system.
///
/// An `EventDispatcher` provides access to a real system. It provides the
/// ability to emit events and schedule timeouts. Each layer of the stack
/// provides its own event dispatcher trait which specifies the types of actions
/// that must be supported in order to support that layer of the stack. The
/// `EventDispatcher` trait is a sub-trait of all of these traits.
pub trait EventDispatcher: DeviceLayerEventDispatcher + TransportLayerEventDispatcher {
    // TODO(joshlf): Figure out how to cancel timeouts.

    /// Schedule a callback to be invoked after a timeout.
    ///
    /// `schedule_timeout` schedules `f` to be invoked after `duration` has
    /// elapsed.
    fn schedule_timeout<F: FnOnce(&mut Context<Self>)>(&mut self, duration: (), f: F);
}
