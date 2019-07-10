// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A networking stack.

// In case we roll the toolchain and something we're using as a feature has been
// stabilized.
#![allow(stable_features)]
#![feature(specialization)]
#![deny(missing_docs)]
#![deny(unreachable_patterns)]
// TODO(joshlf): Remove this once all of the elements in the crate are actually
// used.
#![allow(unused)]
#![deny(unused_imports)]
// This is a hack until we migrate to a different benchmarking framework. To run
// benchmarks, edit your Cargo.toml file to add a "benchmark" feature, and then
// run with that feature enabled.
#![cfg_attr(feature = "benchmark", feature(test))]

#[cfg(all(test, feature = "benchmark"))]
extern crate test;

#[macro_use]
mod macros;

#[cfg(all(test, feature = "benchmark"))]
mod benchmarks;
mod data_structures;
mod device;
mod error;
mod ip;
#[cfg(test)]
mod testutil;
mod transport;
mod types;
mod wire;

pub use crate::data_structures::{IdMapCollection, IdMapCollectionKey};
pub use crate::device::{
    ethernet::Mac, get_ip_addr_subnet, receive_frame, DeviceId, DeviceLayerEventDispatcher,
};
pub use crate::error::NetstackError;
pub use crate::ip::{
    icmp, AddrSubnet, AddrSubnetEither, EntryDest, EntryDestEither, EntryEither, IpAddr,
    IpLayerEventDispatcher, IpStateBuilder, Ipv4, Ipv4Addr, Ipv6, Ipv6Addr, Subnet, SubnetEither,
};
pub use crate::transport::udp::UdpEventDispatcher;
pub use crate::transport::TransportLayerEventDispatcher;

use std::time;

use crate::device::{DeviceLayerState, DeviceLayerTimerId};
use crate::ip::{IpLayerState, IpLayerTimerId};
use crate::transport::{TransportLayerState, TransportLayerTimerId};

/// Map an expression over either version of an address.
///
/// `map_addr_version!` takes a type which is an enum with two variants - `V4`
/// and `V6` - and a value of that type. It matches on the variants, and for
/// both variants, invokes an expression on the inner contents. `$addr` is both
/// the name of the variable to match on, and the name that the address will be
/// bound to for the scope of the expression.
///
/// To make it concrete, the expression `map_addr_version!(Foo, bar, blah(bar))`
/// desugars to:
///
/// ```rust,ignore
/// match bar {
///     Foo::V4(bar) => blah(bar),
///     Foo::V6(bar) => blah(bar),
/// }
/// ```
#[macro_export]
macro_rules! map_addr_version {
    ($ty:tt, $addr:ident, $expr:expr) => {
        match $addr {
            $ty::V4($addr) => $expr,
            $ty::V6($addr) => $expr,
        }
    };
    ($ty:tt, $addr:ident, $expr:expr,) => {
        map_addr_version!($addr, $expr)
    };
}

/// A builder for [`StackState`].
#[derive(Default)]
pub struct StackStateBuilder {
    ip: IpStateBuilder,
}

impl StackStateBuilder {
    /// Get the builder for the IP state.
    pub fn ip_builder(&mut self) -> &mut IpStateBuilder {
        &mut self.ip
    }

    /// Consume this builder and produce a `StackState`.
    pub fn build<D: EventDispatcher>(self) -> StackState<D> {
        StackState {
            transport: TransportLayerState::default(),
            ip: self.ip.build(),
            device: DeviceLayerState::default(),
            #[cfg(test)]
            test_counters: testutil::TestCounters::default(),
        }
    }
}

/// The state associated with the network stack.
pub struct StackState<D: EventDispatcher> {
    transport: TransportLayerState<D>,
    ip: IpLayerState<D>,
    device: DeviceLayerState,
    #[cfg(test)]
    test_counters: testutil::TestCounters,
}

impl<D: EventDispatcher> Default for StackState<D> {
    fn default() -> StackState<D> {
        StackStateBuilder::default().build()
    }
}

impl<D: EventDispatcher> StackState<D> {
    /// Add a new ethernet device to the device layer.
    pub fn add_ethernet_device(&mut self, mac: Mac, mtu: u32) -> DeviceId {
        self.device.add_ethernet_device(mac, mtu)
    }
}

/// Context available during the execution of the netstack.
///
/// `Context` provides access to the state of the netstack and to an event
/// dispatcher which can be used to emit events and schedule timeouts. A mutable
/// reference to a `Context` is passed to every function in the netstack.
#[derive(Default)]
pub struct Context<D: EventDispatcher> {
    state: StackState<D>,
    dispatcher: D,
}

impl<D: EventDispatcher> Context<D> {
    /// Construct a new `Context`.
    pub fn new(state: StackState<D>, dispatcher: D) -> Context<D> {
        Context { state, dispatcher }
    }

    /// Construct a new `Context` using the default `StackState`.
    pub fn with_default_state(dispatcher: D) -> Context<D> {
        Context { state: StackState::default(), dispatcher }
    }

    /// Get the stack state immutably.
    pub fn state(&self) -> &StackState<D> {
        &self.state
    }

    /// Get the stack state mutably.
    pub fn state_mut(&mut self) -> &mut StackState<D> {
        &mut self.state
    }

    /// Get the dispatcher immutably.
    pub fn dispatcher(&self) -> &D {
        &self.dispatcher
    }

    /// Get the dispatcher mutably.
    pub fn dispatcher_mut(&mut self) -> &mut D {
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

impl<D: EventDispatcher + Default> Context<D> {
    /// Construct a new `Context` using the default dispatcher.
    pub fn with_default_dispatcher(state: StackState<D>) -> Context<D> {
        Context { state, dispatcher: D::default() }
    }
}

/// The identifier for any timer event.
#[derive(Copy, Clone, PartialEq, Debug)]
pub struct TimerId(TimerIdInner);

#[derive(Copy, Clone, PartialEq, Debug)]
enum TimerIdInner {
    /// A timer event in the device layer.
    DeviceLayer(DeviceLayerTimerId),
    /// A timer event in the transport layer.
    TransportLayer(TransportLayerTimerId),
    /// A timer event in the IP layer.
    IpLayer(IpLayerTimerId),
    /// A no-op timer event (used for tests)
    #[cfg(test)]
    Nop(usize),
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
        TimerId(TimerIdInner::IpLayer(x)) => {
            ip::handle_timeout(ctx, x);
        }
        #[cfg(test)]
        TimerId(TimerIdInner::Nop(_)) => {
            increment_counter!(ctx, "timer::nop");
        }
    }
}

/// A type representing an instant in time.
///
/// `Instant` can be implemented by any type which represents an instant in
/// time. This can include any sort of real-world clock time (e.g.,
/// [`std::time::Instant`]) or fake time such as in testing.
pub trait Instant: Sized + Ord + Copy + Clone {
    /// Returns the amount of time elapsed from another instant to this one.
    ///
    /// # Panics
    ///
    /// This function will panic if `earlier` is later than `self`.
    fn duration_since(&self, earlier: Self) -> time::Duration;

    /// Returns `Some(t)` where `t` is the time `self + duration` if `t` can be
    /// represented as `Instant` (which means it's inside the bounds of the
    /// underlying data structure), `None` otherwise.
    fn checked_add(&self, duration: time::Duration) -> Option<Self>;

    /// Returns `Some(t)` where `t` is the time `self - duration` if `t` can be
    /// represented as `Instant` (which means it's inside the bounds of the
    /// underlying data structure), `None` otherwise.
    fn checked_sub(&self, duration: time::Duration) -> Option<Self>;
}

impl Instant for time::Instant {
    fn duration_since(&self, earlier: time::Instant) -> time::Duration {
        time::Instant::duration_since(self, earlier)
    }

    fn checked_add(&self, duration: time::Duration) -> Option<Self> {
        time::Instant::checked_add(self, duration)
    }

    fn checked_sub(&self, duration: time::Duration) -> Option<Self> {
        time::Instant::checked_sub(self, duration)
    }
}

/// An object which can dispatch events to a real system.
///
/// An `EventDispatcher` provides access to a real system. It provides the
/// ability to emit events and schedule timeouts. Each layer of the stack
/// provides its own event dispatcher trait which specifies the types of actions
/// that must be supported in order to support that layer of the stack. The
/// `EventDispatcher` trait is a sub-trait of all of these traits.
pub trait EventDispatcher:
    DeviceLayerEventDispatcher + IpLayerEventDispatcher + TransportLayerEventDispatcher
{
    /// The type of an instant in time.
    ///
    /// All time is measured using `Instant`s, including scheduling timeouts.
    /// This type may represent some sort of real-world time (e.g.,
    /// [`std::time::Instant`]), or may be mocked in testing using a fake clock.
    type Instant: Instant;

    /// Returns the current instant.
    ///
    /// `now` guarantees that two subsequent calls to `now` will return monotonically
    /// non-decreasing values.
    fn now(&self) -> Self::Instant;

    /// Schedule a callback to be invoked after a timeout.
    ///
    /// `schedule_timeout` schedules `f` to be invoked after `duration` has
    /// elapsed, overwriting any previous timeout with the same ID.
    ///
    /// If there was previously a timer with that ID, return the time at which
    /// is was scheduled to fire.
    ///
    /// # Panics
    ///
    /// `schedule_timeout` may panic if `duration` is large enough that
    /// `self.now() + duration` overflows.
    fn schedule_timeout(&mut self, duration: time::Duration, id: TimerId) -> Option<Self::Instant> {
        self.schedule_timeout_instant(self.now().checked_add(duration).unwrap(), id)
    }

    /// Schedule a callback to be invoked at a specific time.
    ///
    /// `schedule_timeout_instant` schedules `f` to be invoked at `time`,
    /// overwriting any previous timeout with the same ID.
    ///
    /// If there was previously a timer with that ID, return the time at which
    /// is was scheduled to fire.
    fn schedule_timeout_instant(
        &mut self,
        time: Self::Instant,
        id: TimerId,
    ) -> Option<Self::Instant>;

    /// Cancel a timeout.
    ///
    /// Returns true if the timeout was cancelled, false if there was no timeout
    /// for the given ID.
    fn cancel_timeout(&mut self, id: TimerId) -> Option<Self::Instant>;
}

/// Set the IP address and subnet for a device.
pub fn set_ip_addr_subnet<D: EventDispatcher>(
    ctx: &mut Context<D>,
    device: DeviceId,
    addr_sub: AddrSubnetEither,
) {
    map_addr_version!(
        AddrSubnetEither,
        addr_sub,
        crate::device::set_ip_addr_subnet(ctx, device, addr_sub)
    );
}

/// Add a route to send all packets addressed to a specific subnet to a specific device.
pub fn add_device_route<D: EventDispatcher>(
    ctx: &mut Context<D>,
    subnet: SubnetEither,
    device: DeviceId,
) -> Result<(), error::NetstackError> {
    map_addr_version!(SubnetEither, subnet, crate::ip::add_device_route(ctx, subnet, device))
        .map_err(From::from)
}

/// Delete a route from the forwarding table, returning `Err` if no
/// route was found to be deleted.
pub fn del_device_route<D: EventDispatcher>(
    ctx: &mut Context<D>,
    subnet: SubnetEither,
) -> Result<(), error::NetstackError> {
    map_addr_version!(SubnetEither, subnet, crate::ip::del_device_route(ctx, subnet))
        .map_err(From::from)
}

/// Get all the routes.
pub fn get_all_routes<'a, D: EventDispatcher>(
    ctx: &'a Context<D>,
) -> impl 'a + Iterator<Item = EntryEither> {
    let v4_routes = ip::iter_routes::<_, ip::Ipv4Addr>(ctx);
    let v6_routes = ip::iter_routes::<_, ip::Ipv6Addr>(ctx);
    v4_routes.cloned().map(From::from).chain(v6_routes.cloned().map(From::from))
}
