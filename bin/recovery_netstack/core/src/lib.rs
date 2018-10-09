// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A networking stack.

#![feature(async_await, await_macro)]
#![feature(never_type)]
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
// TODO(joshlf): Remove this once all of the elements in the crate are actually
// used.
#![allow(unused)]
#![deny(unused_imports)]

#[macro_use]
mod macros;

mod device;
mod error;
mod ip;
#[cfg(test)]
mod testutil;
mod transport;
mod wire;

pub mod types;

use crate::device::{ethernet::Mac, DeviceId, DeviceLayerEventDispatcher, DeviceLayerTimerId};
use crate::transport::{TransportLayerEventDispatcher, TransportLayerTimerId};

use crate::device::DeviceLayerState;
use crate::ip::IpLayerState;
use crate::transport::TransportLayerState;

/// The state associated with the network stack.
pub struct StackState<D: EventDispatcher> {
    transport: TransportLayerState<D>,
    ip: IpLayerState,
    device: DeviceLayerState,
    #[cfg(test)]
    test_counters: testutil::TestCounters,
}

impl<D: EventDispatcher> Default for StackState<D> {
    fn default() -> StackState<D> {
        StackState {
            transport: TransportLayerState::default(),
            ip: IpLayerState::default(),
            device: DeviceLayerState::default(),
            #[cfg(test)]
            test_counters: testutil::TestCounters::default(),
        }
    }
}

impl<D: EventDispatcher> StackState<D> {
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
    state: StackState<D>,
    dispatcher: D,
}

impl<D: EventDispatcher> Context<D> {
    /// Construct a new `Context`.
    pub fn new(state: StackState<D>, dispatcher: D) -> Context<D> {
        Context { state, dispatcher }
    }

    /// Get the stack state.
    pub fn state(&mut self) -> &mut StackState<D> {
        &mut self.state
    }

    /// Get the dispatcher.
    pub fn dispatcher(&mut self) -> &mut D {
        &mut self.dispatcher
    }

    /// Get the stack state and the dispatcher.
    ///
    /// This is useful when a mutable reference to both are required at the same
    /// time, which isn't possible when using the `state` or `dispatcher`
    /// methods.
    pub fn state_and_dispatcher(&mut self) -> (&mut StackState<D>, &mut D) {
        (&mut self.state, &mut self.dispatcher)
    }
}

/// The identifier for any timer event.
#[derive(Copy, Clone, PartialEq)]
pub struct TimerId(TimerIdInner);

#[derive(Copy, Clone, PartialEq)]
enum TimerIdInner {
    /// A timer event in the device layer.
    DeviceLayer(DeviceLayerTimerId),
    /// A timer event in the transport layer.
    TransportLayer(TransportLayerTimerId),
}

/// Handle a generic timer event.
pub fn handle_timeout<D: EventDispatcher>(ctx: &mut Context<D>, id: TimerId) {
    match id {
        TimerId(TimerIdInner::DeviceLayer(x)) => {
            device::handle_timeout(ctx, x);
        }
        TimerId(TimerIdInner::TransportLayer(x)) => {
            transport::handle_timeout(ctx, x);
        }
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
    /// Schedule a callback to be invoked after a timeout.
    ///
    /// `schedule_timeout` schedules `f` to be invoked after `duration` has elapsed, overwriting any
    /// previous timeout with the same ID.
    ///
    /// If there was previously a timer with that ID, return the time at which is was scheduled to
    /// fire.
    fn schedule_timeout(
        &mut self, duration: std::time::Duration, id: TimerId,
    ) -> Option<std::time::Instant>;

    /// Schedule a callback to be invoked at a specific time.
    ///
    /// `schedule_timeout_instant` schedules `f` to be invoked at `time`, overwriting any previous
    /// timeout with the same ID.
    ///
    /// If there was previously a timer with that ID, return the time at which is was scheduled to
    /// fire.
    fn schedule_timeout_instant(
        &mut self, time: std::time::Instant, id: TimerId,
    ) -> Option<std::time::Instant>;

    /// Cancel a timeout.
    ///
    /// Returns true if the timeout was cancelled, false if there was no timeout
    /// for the given ID.
    fn cancel_timeout(&mut self, id: TimerId) -> Option<std::time::Instant>;
}

/// Set the IP address for a device.
// TODO(wesleyac): A real error type
pub fn set_ip_addr<D: EventDispatcher>(
    ctx: &mut Context<D>, device: DeviceId, addr: std::net::IpAddr, subnet: types::Subnet,
) -> Result<(), ()> {
    match (addr, subnet.addr(), subnet.prefix_len()) {
        (std::net::IpAddr::V4(ip), std::net::IpAddr::V4(subnet_addr), prefix_len) => {
            crate::device::set_ip_addr(
                ctx,
                device,
                crate::ip::Ipv4Addr::new(ip.octets()),
                crate::ip::Subnet::new(crate::ip::Ipv4Addr::new(subnet_addr.octets()), prefix_len),
            );
            Ok(())
        }
        (std::net::IpAddr::V6(ip), std::net::IpAddr::V6(subnet_addr), prefix_len) => {
            crate::device::set_ip_addr(
                ctx,
                device,
                crate::ip::Ipv6Addr::new(ip.octets()),
                crate::ip::Subnet::new(crate::ip::Ipv6Addr::new(subnet_addr.octets()), prefix_len),
            );
            Ok(())
        }
        _ => Err(()),
    }
}

/// Get the IP addresses for a device.
pub fn get_ip_addr<D: EventDispatcher>(
    ctx: &mut Context<D>, device: DeviceId,
) -> (Option<std::net::Ipv4Addr>, Option<std::net::Ipv6Addr>) {
    unimplemented!();
}

/// Add a route to send all packets addressed to a specific subnet to a specific device.
pub fn add_device_route<D: EventDispatcher>(
    ctx: &mut Context<D>, subnet: types::Subnet, device: DeviceId,
) {
    match subnet.addr() {
        std::net::IpAddr::V4(addr) => {
            ip::add_device_route(
                ctx,
                ip::Subnet::new(ip::Ipv4Addr::from(addr), subnet.prefix_len()),
                device,
            );
        }
        std::net::IpAddr::V6(addr) => {
            ip::add_device_route(
                ctx,
                ip::Subnet::new(ip::Ipv6Addr::from(addr), subnet.prefix_len()),
                device,
            );
        }
    }
}
