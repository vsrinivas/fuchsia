// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A networking stack.

// In case we roll the toolchain and something we're using as a feature has been
// stabilized.
#![allow(stable_features)]
#![feature(specialization)]
#![deny(missing_docs, unreachable_patterns)]
// This is a hack until we migrate to a different benchmarking framework. To run
// benchmarks, edit your Cargo.toml file to add a "benchmark" feature, and then
// run with that feature enabled.
#![cfg_attr(feature = "benchmark", feature(test))]
// TODO Follow 2018 idioms
#![allow(elided_lifetimes_in_paths)]

#[cfg(all(test, feature = "benchmark"))]
extern crate test;

// TODO(joshlf): Remove this once the old packet crate has been deleted and the
// new one's name has been changed back to `packet`.
extern crate packet_new as packet;

#[macro_use]
mod macros;

mod algorithm;
#[cfg(test)]
mod benchmarks;
mod context;
mod data_structures;
mod device;
pub mod error;
mod ip;
#[cfg(test)]
mod testutil;
mod transport;
mod wire;

use log::trace;

pub use crate::data_structures::{IdMapCollection, IdMapCollectionKey};
pub use crate::device::ndp::NdpConfigurations;
pub use crate::device::{
    get_ip_addr_subnets, initialize_device, receive_frame, remove_device, DeviceId,
    DeviceLayerEventDispatcher,
};
pub use crate::error::{ConnectError, LocalAddressError, NetstackError, RemoteAddressError};
pub use crate::ip::{
    icmp, EntryDest, EntryDestEither, EntryEither, IpLayerEventDispatcher, Ipv4StateBuilder,
    Ipv6StateBuilder,
};
pub use crate::transport::udp::{
    connect_udp, get_udp_conn_info, get_udp_listener_info, listen_udp, remove_udp_conn,
    remove_udp_listener, send_udp, send_udp_conn, send_udp_listener, UdpConnId, UdpEventDispatcher,
    UdpListenerId,
};
pub use crate::transport::TransportLayerEventDispatcher;

use std::fmt::Debug;
use std::time;

use net_types::ethernet::Mac;
use net_types::ip::{AddrSubnetEither, IpAddr, Ipv4, Ipv4Addr, Ipv6, Ipv6Addr, SubnetEither};
use net_types::SpecifiedAddr;
use packet::{Buf, BufferMut, EmptyBuf};
use rand::{CryptoRng, RngCore};

use crate::context::TimerContext;
use crate::device::{DeviceLayerState, DeviceLayerTimerId, DeviceStateBuilder};
use crate::ip::{IpLayerTimerId, Ipv4State, Ipv6State};
use crate::transport::{TransportLayerState, TransportLayerTimerId};

/// Map an expression over either version of one or more addresses.
///
/// `map_addr_version!` when given a value of a type which is an enum with two
/// variants - `V4` and `V6` - matches on the variants, and for both variants,
/// invokes an expression on the inner contents. `$addr` is both the name of the
/// variable to match on, and the name that the address will be bound to for the
/// scope of the expression.
///
/// `map_addr_version!` when given a list of values and their types (all enums
/// with variants `V4` and `V6`), matches on the tuple of values and invokes the
/// `$match` expression when all values are of the same variant. Otherwise the
/// `$mismatch` expression is invoked.
///
/// To make it concrete, the expression `map_addr_version!(bar: Foo; blah(bar))`
/// desugars to:
///
/// ```rust,ignore
/// match bar {
///     Foo::V4(bar) => blah(bar),
///     Foo::V6(bar) => blah(bar),
/// }
/// ```
///
/// Also,
/// `map_addr_version!((foo: Foo, bar: Bar); blah(foo, bar), unreachable!())`
/// desugars to:
///
/// ```rust,ignore
/// match (foo, bar) {
///     (Foo::V4(foo), Bar::V4(bar)) => blah(foo, bar),
///     (Foo::V6(foo), Bar::V6(bar)) => blah(foo, bar),
///     _ => unreachable!(),
/// }
/// ```
#[macro_export]
macro_rules! map_addr_version {
    ($addr:ident: $ty:tt; $expr:expr) => {
        match $addr {
            $ty::V4($addr) => $expr,
            $ty::V6($addr) => $expr,
        }
    };
    (( $( $addr:ident : $ty:tt ),+ ); $match:expr, $mismatch:expr) => {
        match ( $( $addr ),+ ) {
            ( $( $ty::V4( $addr ) ),+ ) => $match,
            ( $( $ty::V6( $addr ) ),+ ) => $match,
            _ => $mismatch,
        }
    };
    (( $( $addr:ident : $ty:tt ),+ ); $match:expr, $mismatch:expr,) => {
        map_addr_version!(($( $addr: $ty ),+); $match, $mismatch)
    };
}

/// A builder for [`StackState`].
#[derive(Default, Clone)]
pub struct StackStateBuilder {
    ipv4: Ipv4StateBuilder,
    ipv6: Ipv6StateBuilder,
    device: DeviceStateBuilder,
}

impl StackStateBuilder {
    /// Get the builder for the IPv4 state.
    pub fn ipv4_builder(&mut self) -> &mut Ipv4StateBuilder {
        &mut self.ipv4
    }

    /// Get the builder for the IPv6 state.
    pub fn ipv6_builder(&mut self) -> &mut Ipv6StateBuilder {
        &mut self.ipv6
    }

    /// Get the builder for the device state.
    pub fn device_builder(&mut self) -> &mut DeviceStateBuilder {
        &mut self.device
    }

    /// Consume this builder and produce a `StackState`.
    pub fn build<D: EventDispatcher>(self) -> StackState<D> {
        StackState {
            transport: TransportLayerState::default(),
            ipv4: self.ipv4.build(),
            ipv6: self.ipv6.build(),
            device: self.device.build(),
            #[cfg(test)]
            test_counters: testutil::TestCounters::default(),
        }
    }
}

/// The state associated with the network stack.
pub struct StackState<D: EventDispatcher> {
    transport: TransportLayerState,
    ipv4: Ipv4State<D::Instant, DeviceId>,
    ipv6: Ipv6State<D::Instant, DeviceId>,
    device: DeviceLayerState<D::Instant>,
    #[cfg(test)]
    test_counters: testutil::TestCounters,
}

impl<D: EventDispatcher> StackState<D> {
    /// Add a new ethernet device to the device layer.
    ///
    /// `add_ethernet_device` only makes the netstack aware of the device. The device still needs to
    /// be initialized. A device MUST NOT be used until it has been initialized. The netstack
    /// promises not to generate any outbound traffic on the device until [`initialize_device`] has
    /// been called.
    ///
    /// See [`initialize_device`] for more information.
    ///
    /// [`initialize_device`]: crate::device::initialize_device
    pub fn add_ethernet_device(&mut self, mac: Mac, mtu: u32) -> DeviceId {
        self.device.add_ethernet_device(mac, mtu)
    }
}

impl<D: EventDispatcher> Default for StackState<D> {
    fn default() -> StackState<D> {
        StackStateBuilder::default().build()
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
#[derive(Copy, Clone, Eq, PartialEq, Debug, Hash)]
pub struct TimerId(TimerIdInner);

#[derive(Copy, Clone, Eq, PartialEq, Debug, Hash)]
enum TimerIdInner {
    /// A timer event in the device layer.
    DeviceLayer(DeviceLayerTimerId),
    /// A timer event in the transport layer.
    _TransportLayer(TransportLayerTimerId),
    /// A timer event in the IP layer.
    IpLayer(IpLayerTimerId),
    /// A no-op timer event (used for tests)
    #[cfg(test)]
    Nop(usize),
}

impl From<DeviceLayerTimerId> for TimerId {
    fn from(id: DeviceLayerTimerId) -> TimerId {
        TimerId(TimerIdInner::DeviceLayer(id))
    }
}

impl_timer_context!(TimerId, DeviceLayerTimerId, TimerId(TimerIdInner::DeviceLayer(id)), id);

/// Handle a generic timer event.
pub fn handle_timeout<D: EventDispatcher>(ctx: &mut Context<D>, id: TimerId) {
    trace!("handle_timeout: dispatching timerid: {:?}", id);

    match id {
        TimerId(TimerIdInner::DeviceLayer(x)) => {
            device::handle_timeout(ctx, x);
        }
        TimerId(TimerIdInner::_TransportLayer(x)) => {
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
pub trait Instant: Sized + Ord + Copy + Clone + Debug {
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

/// An `EventDispatcher` which supports sending buffers of a given type.
///
/// `D: BufferDispatcher<B>` is shorthand for `D: EventDispatcher +
/// DeviceLayerEventDispatcher<B>`.
pub trait BufferDispatcher<B: BufferMut>:
    EventDispatcher + DeviceLayerEventDispatcher<B> + IpLayerEventDispatcher<B>
{
}
impl<
        B: BufferMut,
        D: EventDispatcher + DeviceLayerEventDispatcher<B> + IpLayerEventDispatcher<B>,
    > BufferDispatcher<B> for D
{
}

// TODO(joshlf): Should we add a `for<'a> DeviceLayerEventDispatcher<&'a mut
// [u8]>` bound? Would anything get more efficient if we were able to stack
// allocate internally-generated buffers?

/// An object which can dispatch events to a real system.
///
/// An `EventDispatcher` provides access to a real system. It provides the
/// ability to emit events and schedule timeouts. Each layer of the stack
/// provides its own event dispatcher trait which specifies the types of actions
/// that must be supported in order to support that layer of the stack. The
/// `EventDispatcher` trait is a sub-trait of all of these traits.
pub trait EventDispatcher:
    DeviceLayerEventDispatcher<Buf<Vec<u8>>>
    + DeviceLayerEventDispatcher<EmptyBuf>
    + IpLayerEventDispatcher<Buf<Vec<u8>>>
    + IpLayerEventDispatcher<EmptyBuf>
    + TransportLayerEventDispatcher<Ipv4>
    + TransportLayerEventDispatcher<Ipv6>
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

    /// Cancel all timeouts which satisfy a predicate.
    ///
    /// `cancel_timeouts_with` calls `f` on each scheduled timer, and cancels
    /// any timeout for which `f` returns true.
    fn cancel_timeouts_with<F: FnMut(&TimerId) -> bool>(&mut self, f: F);

    /// Get the instant a timer will fire, if one is scheduled.
    ///
    /// Returns the [`Instant`] a timer with ID `id` will be invoked. If no timer
    /// with the given ID exists, `scheduled_instant` will return `None`.
    fn scheduled_instant(&self, id: TimerId) -> Option<Self::Instant>;

    // TODO(joshlf): If the CSPRNG requirement becomes a performance problem,
    // introduce a second, non-cryptographically secure, RNG.

    /// The random number generator (RNG) provided by this `EventDispatcher`.
    ///
    /// Code in the core is required to only obtain random values through this
    /// RNG. This allows a deterministic RNG to be provided when useful (for
    /// example, in tests).
    ///
    /// The provided RNG must be cryptographically secure in order to ensure
    /// that random values produced within the network stack are not predictable
    /// by outside observers. This helps to prevent certain kinds of
    /// fingerprinting and denial of service attacks.
    type Rng: RngCore + CryptoRng;

    /// Get the random number generator (RNG).
    ///
    /// Code in the core is required to only obtain random values through this
    /// RNG. This allows a deterministic RNG to be provided when useful (for
    /// example, in tests).
    fn rng(&mut self) -> &mut Self::Rng;
}

impl<D: EventDispatcher> TimerContext<TimerId> for Context<D> {
    fn schedule_timer_instant(
        &mut self,
        time: Self::Instant,
        id: TimerId,
    ) -> Option<Self::Instant> {
        self.dispatcher_mut().schedule_timeout_instant(time, id)
    }

    fn cancel_timer(&mut self, id: TimerId) -> Option<Self::Instant> {
        self.dispatcher_mut().cancel_timeout(id)
    }

    fn cancel_timers_with<F: FnMut(&TimerId) -> bool>(&mut self, f: F) {
        self.dispatcher_mut().cancel_timeouts_with(f)
    }

    fn scheduled_instant(&self, id: TimerId) -> Option<Self::Instant> {
        self.dispatcher().scheduled_instant(id)
    }
}

/// Get all IPv4 and IPv6 address/subnet pairs configured on a device
pub fn get_all_ip_addr_subnets<'a, D: EventDispatcher>(
    ctx: &'a Context<D>,
    device: DeviceId,
) -> impl 'a + Iterator<Item = AddrSubnetEither> {
    let addr_v4 = crate::device::get_ip_addr_subnets::<_, Ipv4Addr>(ctx, device)
        .map(|a| AddrSubnetEither::V4(a));
    let addr_v6 = crate::device::get_ip_addr_subnets::<_, Ipv6Addr>(ctx, device)
        .map(|a| AddrSubnetEither::V6(a));

    addr_v4.chain(addr_v6)
}

/// Set the IP address and subnet for a device.
pub fn add_ip_addr_subnet<D: EventDispatcher>(
    ctx: &mut Context<D>,
    device: DeviceId,
    addr_sub: AddrSubnetEither,
) -> error::Result<()> {
    map_addr_version!(
        addr_sub: AddrSubnetEither;
        crate::device::add_ip_addr_subnet(ctx, device, addr_sub)
    )
    .map_err(From::from)
}

/// Delete an IP address on a device.
pub fn del_ip_addr<D: EventDispatcher>(
    ctx: &mut Context<D>,
    device: DeviceId,
    addr: IpAddr<SpecifiedAddr<Ipv4Addr>, SpecifiedAddr<Ipv6Addr>>,
) -> error::Result<()> {
    map_addr_version!(
        addr: IpAddr;
        crate::device::del_ip_addr(ctx, device, &addr)
    )
    .map_err(From::from)
}

/// Adds a route to the forwarding table.
pub fn add_route<D: EventDispatcher>(
    ctx: &mut Context<D>,
    entry: EntryEither<DeviceId>,
) -> Result<(), error::NetstackError> {
    let (subnet, dest) = entry.into_subnet_dest();
    match dest {
        EntryDest::Local { device } => map_addr_version!(
            subnet: SubnetEither;
            crate::ip::add_device_route(ctx, subnet, device)
        )
        .map_err(From::from),
        EntryDest::Remote { next_hop } => {
            let next_hop = next_hop.into();
            map_addr_version!(
                (subnet: SubnetEither, next_hop: IpAddr);
                crate::ip::add_route(ctx, subnet, next_hop),
                unreachable!(),
            )
            .map_err(From::from)
        }
    }
}

/// Delete a route from the forwarding table, returning `Err` if no
/// route was found to be deleted.
pub fn del_device_route<D: EventDispatcher>(
    ctx: &mut Context<D>,
    subnet: SubnetEither,
) -> error::Result<()> {
    map_addr_version!(subnet: SubnetEither; crate::ip::del_device_route(ctx, subnet))
        .map_err(From::from)
}

/// Get all the routes.
pub fn get_all_routes<'a, D: EventDispatcher>(
    ctx: &'a Context<D>,
) -> impl 'a + Iterator<Item = EntryEither<DeviceId>> {
    let v4_routes = ip::iter_all_routes::<_, Ipv4Addr>(ctx);
    let v6_routes = ip::iter_all_routes::<_, Ipv6Addr>(ctx);
    v4_routes.cloned().map(From::from).chain(v6_routes.cloned().map(From::from))
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::testutil::{
        get_dummy_config, get_other_ip_address, DummyEventDispatcher, DummyEventDispatcherBuilder,
    };
    use net_types::ip::{Ip, Ipv4, Ipv6};
    use net_types::Witness;

    fn test_add_remove_ip_addresses<I: Ip>() {
        let config = get_dummy_config::<I::Addr>();
        let mut ctx = DummyEventDispatcherBuilder::default().build::<DummyEventDispatcher>();
        let device = ctx.state_mut().add_ethernet_device(config.local_mac, crate::ip::IPV6_MIN_MTU);
        crate::device::initialize_device(&mut ctx, device);

        let ip: IpAddr = get_other_ip_address::<I::Addr>(1).get().into();
        let prefix = config.subnet.prefix();
        let addr_subnet = AddrSubnetEither::new(ip, prefix).unwrap();

        // ip doesn't exist initially
        assert!(get_all_ip_addr_subnets(&ctx, device).find(|&a| a == addr_subnet).is_none());

        // Add ip (ok)
        let () = add_ip_addr_subnet(&mut ctx, device, addr_subnet).unwrap();
        assert!(get_all_ip_addr_subnets(&ctx, device).find(|&a| a == addr_subnet).is_some());

        // Add ip again (already exists)
        assert_eq!(
            add_ip_addr_subnet(&mut ctx, device, addr_subnet).unwrap_err(),
            NetstackError::Exists
        );
        assert!(get_all_ip_addr_subnets(&ctx, device).find(|&a| a == addr_subnet).is_some());

        // Add ip with different subnet (already exists)
        let wrong_addr_subnet = AddrSubnetEither::new(ip, prefix - 1).unwrap();
        assert_eq!(
            add_ip_addr_subnet(&mut ctx, device, wrong_addr_subnet).unwrap_err(),
            NetstackError::Exists
        );
        assert!(get_all_ip_addr_subnets(&ctx, device).find(|&a| a == addr_subnet).is_some());

        let ip = SpecifiedAddr::new(ip).unwrap();
        // Del ip (ok)
        let () = del_ip_addr(&mut ctx, device, ip.into()).unwrap();
        assert!(get_all_ip_addr_subnets(&ctx, device).find(|&a| a == addr_subnet).is_none());

        // Del ip again (not found)
        assert_eq!(del_ip_addr(&mut ctx, device, ip.into()).unwrap_err(), NetstackError::NotFound);
        assert!(get_all_ip_addr_subnets(&ctx, device).find(|&a| a == addr_subnet).is_none());
    }

    #[test]
    fn test_add_remove_ipv4_addresses() {
        test_add_remove_ip_addresses::<Ipv4>();
    }

    #[test]
    fn test_add_remove_ipv6_addresses() {
        test_add_remove_ip_addresses::<Ipv6>();
    }
}
