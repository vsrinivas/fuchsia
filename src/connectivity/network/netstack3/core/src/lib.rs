// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A networking stack.

#![cfg_attr(not(fuzz), no_std)]
// In case we roll the toolchain and something we're using as a feature has been
// stabilized.
#![allow(stable_features)]
#![deny(missing_docs, unreachable_patterns)]
// Turn off checks for dead code, but only when building for fuzzing or
// benchmarking. This allows fuzzers and benchmarks to be written as part of
// the crate, with access to test utilities, without a bunch of build errors
// due to unused code. These checks are turned back on in the 'fuzz' and
// 'benchmark' modules.
#![cfg_attr(any(fuzz, benchmark), allow(dead_code, unused_imports, unused_macros))]

// TODO(https://github.com/rust-lang-nursery/portability-wg/issues/11): remove
// this module.
extern crate fakealloc as alloc;

// TODO(https://github.com/dtolnay/thiserror/pull/64): remove this module.
#[cfg(not(fuzz))]
extern crate fakestd as std;

#[macro_use]
mod macros;

mod algorithm;
#[cfg(test)]
pub mod benchmarks;
pub mod context;
pub(crate) mod convert;
pub mod data_structures;
pub mod device;
pub mod error;
#[cfg(fuzz)]
mod fuzz;
pub mod ip;
pub mod socket;
pub mod sync;
#[cfg(test)]
mod testutil;
pub mod transport;

use alloc::vec::Vec;
use core::{fmt::Debug, marker::PhantomData, time};

use derivative::Derivative;
use log::trace;
use net_types::{
    ip::{AddrSubnetEither, IpAddr, Ipv4, Ipv6, SubnetEither},
    SpecifiedAddr,
};
use packet::{Buf, BufferMut, EmptyBuf};

use crate::{
    context::{CounterContext, EventContext, InstantContext, RngContext, TimerContext},
    device::DeviceId,
    device::{DeviceLayerState, DeviceLayerTimerId},
    ip::{
        device::{DualStackDeviceHandler, Ipv4DeviceTimerId, Ipv6DeviceTimerId},
        icmp::{BufferIcmpContext, IcmpContext},
        IpLayerTimerId, Ipv4State, Ipv6State,
    },
    transport::{TransportLayerState, TransportLayerTimerId},
};

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
    ($addr:ident: $ty:tt; $expr_v4:expr, $expr_v6:expr) => {
        match $addr {
            $ty::V4($addr) => $expr_v4,
            $ty::V6($addr) => $expr_v6,
        }
    };
    (( $( $addr:ident : $ty:tt ),+ ); $match:expr, $mismatch:expr) => {
        match ( $( $addr ),+ ) {
            ( $( $ty::V4( $addr ) ),+ ) => $match,
            ( $( $ty::V6( $addr ) ),+ ) => $match,
            _ => $mismatch,
        }
    };
    (( $( $addr:ident : $ty:tt ),+ ); $match_v4:expr, $match_v6:expr, $mismatch:expr) => {
        match ( $( $addr ),+ ) {
            ( $( $ty::V4( $addr ) ),+ ) => $match_v4,
            ( $( $ty::V6( $addr ) ),+ ) => $match_v6,
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
    transport: transport::TransportStateBuilder,
    ipv4: ip::Ipv4StateBuilder,
    ipv6: ip::Ipv6StateBuilder,
}

impl StackStateBuilder {
    /// Get the builder for the transport layer state.
    pub fn transport_builder(&mut self) -> &mut transport::TransportStateBuilder {
        &mut self.transport
    }

    /// Get the builder for the IPv4 state.
    pub fn ipv4_builder(&mut self) -> &mut ip::Ipv4StateBuilder {
        &mut self.ipv4
    }

    /// Get the builder for the IPv6 state.
    pub fn ipv6_builder(&mut self) -> &mut ip::Ipv6StateBuilder {
        &mut self.ipv6
    }

    /// Consume this builder and produce a `StackState`.
    pub fn build_with_ctx<C: NonSyncContext>(self, ctx: &mut C) -> StackState<C> {
        StackState {
            transport: self.transport.build_with_ctx(ctx),
            ipv4: self.ipv4.build(),
            ipv6: self.ipv6.build(),
            device: Default::default(),
        }
    }
}

/// The state associated with the network stack.
pub struct StackState<C: NonSyncContext> {
    transport: TransportLayerState<C>,
    ipv4: Ipv4State<C::Instant, DeviceId<C::Instant>>,
    ipv6: Ipv6State<C::Instant, DeviceId<C::Instant>>,
    device: DeviceLayerState<C::Instant>,
}

impl<C: NonSyncContext + Default> Default for StackState<C> {
    fn default() -> StackState<C> {
        StackStateBuilder::default().build_with_ctx(&mut Default::default())
    }
}

/// The non synchronized context for the stack with a buffer.
pub trait BufferNonSyncContextInner<B: BufferMut>:
    device::BufferDeviceLayerEventDispatcher<B>
    + transport::udp::BufferUdpContext<Ipv4, B>
    + transport::udp::BufferUdpContext<Ipv6, B>
    + BufferIcmpContext<Ipv4, B>
    + BufferIcmpContext<Ipv6, B>
{
}
impl<
        B: BufferMut,
        C: device::BufferDeviceLayerEventDispatcher<B>
            + transport::udp::BufferUdpContext<Ipv4, B>
            + transport::udp::BufferUdpContext<Ipv6, B>
            + BufferIcmpContext<Ipv4, B>
            + BufferIcmpContext<Ipv6, B>,
    > BufferNonSyncContextInner<B> for C
{
}

/// The non synchronized context for the stack with a buffer.
pub trait BufferNonSyncContext<B: BufferMut>:
    NonSyncContext + BufferNonSyncContextInner<B>
{
}
impl<B: BufferMut, C: NonSyncContext + BufferNonSyncContextInner<B>> BufferNonSyncContext<B> for C {}

/// The non-synchronized context for the stack.
pub trait NonSyncContext:
    CounterContext
    + BufferNonSyncContextInner<Buf<Vec<u8>>>
    + BufferNonSyncContextInner<EmptyBuf>
    + RngContext
    + TimerContext<TimerId<<Self as InstantContext>::Instant>>
    + EventContext<ip::device::IpDeviceEvent<DeviceId<Self::Instant>, Ipv4>>
    + EventContext<ip::device::IpDeviceEvent<DeviceId<Self::Instant>, Ipv6>>
    + EventContext<ip::IpLayerEvent<DeviceId<Self::Instant>, Ipv4>>
    + EventContext<ip::IpLayerEvent<DeviceId<Self::Instant>, Ipv6>>
    + EventContext<ip::device::dad::DadEvent<DeviceId<Self::Instant>>>
    + EventContext<ip::device::route_discovery::Ipv6RouteDiscoveryEvent<DeviceId<Self::Instant>>>
    + transport::udp::UdpContext<Ipv4>
    + transport::udp::UdpContext<Ipv6>
    + IcmpContext<Ipv4>
    + IcmpContext<Ipv6>
    + transport::tcp::socket::TcpNonSyncContext
    + device::DeviceLayerEventDispatcher
    + 'static
{
}
impl<
        C: CounterContext
            + BufferNonSyncContextInner<Buf<Vec<u8>>>
            + BufferNonSyncContextInner<EmptyBuf>
            + RngContext
            + TimerContext<TimerId<<Self as InstantContext>::Instant>>
            + EventContext<ip::device::IpDeviceEvent<DeviceId<Self::Instant>, Ipv4>>
            + EventContext<ip::device::IpDeviceEvent<DeviceId<Self::Instant>, Ipv6>>
            + EventContext<ip::IpLayerEvent<DeviceId<Self::Instant>, Ipv4>>
            + EventContext<ip::IpLayerEvent<DeviceId<Self::Instant>, Ipv6>>
            + EventContext<ip::device::dad::DadEvent<DeviceId<Self::Instant>>>
            + EventContext<
                ip::device::route_discovery::Ipv6RouteDiscoveryEvent<DeviceId<Self::Instant>>,
            > + transport::udp::UdpContext<Ipv4>
            + transport::udp::UdpContext<Ipv6>
            + IcmpContext<Ipv4>
            + IcmpContext<Ipv6>
            + transport::tcp::socket::TcpNonSyncContext
            + device::DeviceLayerEventDispatcher
            + 'static,
    > NonSyncContext for C
{
}

/// The synchronized context.
pub struct SyncCtx<NonSyncCtx: NonSyncContext> {
    /// Contains the state of the stack.
    pub state: StackState<NonSyncCtx>,
    /// A marker for the non-synchronized context type.
    pub non_sync_ctx_marker: PhantomData<NonSyncCtx>,
}

/// Context available during the execution of the netstack.
///
/// `Ctx` provides access to the state of the netstack and to an event
/// dispatcher which can be used to emit events and schedule timers. A mutable
/// reference to a `Ctx` is passed to every function in the netstack.
pub struct Ctx<NonSyncCtx: NonSyncContext> {
    /// The synchronized context.
    pub sync_ctx: SyncCtx<NonSyncCtx>,
    /// The non-synchronized context.
    pub non_sync_ctx: NonSyncCtx,
}

impl<NonSyncCtx: NonSyncContext + Default> Default for Ctx<NonSyncCtx>
where
    StackState<NonSyncCtx>: Default,
{
    fn default() -> Ctx<NonSyncCtx> {
        Ctx {
            sync_ctx: SyncCtx { state: StackState::default(), non_sync_ctx_marker: PhantomData },
            non_sync_ctx: Default::default(),
        }
    }
}

impl<NonSyncCtx: NonSyncContext + Default> Ctx<NonSyncCtx> {
    /// Constructs a new `Ctx`.
    pub fn new(state: StackState<NonSyncCtx>) -> Ctx<NonSyncCtx> {
        Ctx {
            sync_ctx: SyncCtx { state, non_sync_ctx_marker: PhantomData },
            non_sync_ctx: Default::default(),
        }
    }

    #[cfg(test)]
    pub(crate) fn new_with_builder(builder: StackStateBuilder) -> Self {
        let mut non_sync_ctx = Default::default();
        let state = builder.build_with_ctx(&mut non_sync_ctx);
        Self { sync_ctx: SyncCtx { state, non_sync_ctx_marker: PhantomData }, non_sync_ctx }
    }
}

/// The identifier for any timer event.
#[derive(Derivative)]
#[derivative(
    Clone(bound = ""),
    Eq(bound = ""),
    PartialEq(bound = ""),
    Hash(bound = ""),
    Debug(bound = "")
)]
pub struct TimerId<I: Instant>(TimerIdInner<I>);

#[derive(Derivative)]
#[derivative(
    Clone(bound = ""),
    Eq(bound = ""),
    PartialEq(bound = ""),
    Hash(bound = ""),
    Debug(bound = "")
)]
enum TimerIdInner<I: Instant> {
    /// A timer event in the device layer.
    DeviceLayer(DeviceLayerTimerId<I>),
    /// A timer event in the transport layer.
    TransportLayer(TransportLayerTimerId),
    /// A timer event in the IP layer.
    IpLayer(IpLayerTimerId),
    /// A timer event for an IPv4 device.
    Ipv4Device(Ipv4DeviceTimerId<DeviceId<I>>),
    /// A timer event for an IPv6 device.
    Ipv6Device(Ipv6DeviceTimerId<DeviceId<I>>),
    /// A no-op timer event (used for tests)
    #[cfg(test)]
    Nop(usize),
}

impl<I: Instant> From<DeviceLayerTimerId<I>> for TimerId<I> {
    fn from(id: DeviceLayerTimerId<I>) -> TimerId<I> {
        TimerId(TimerIdInner::DeviceLayer(id))
    }
}

impl<I: Instant> From<Ipv4DeviceTimerId<DeviceId<I>>> for TimerId<I> {
    fn from(id: Ipv4DeviceTimerId<DeviceId<I>>) -> TimerId<I> {
        TimerId(TimerIdInner::Ipv4Device(id))
    }
}

impl<I: Instant> From<Ipv6DeviceTimerId<DeviceId<I>>> for TimerId<I> {
    fn from(id: Ipv6DeviceTimerId<DeviceId<I>>) -> TimerId<I> {
        TimerId(TimerIdInner::Ipv6Device(id))
    }
}

impl<I: Instant> From<IpLayerTimerId> for TimerId<I> {
    fn from(id: IpLayerTimerId) -> TimerId<I> {
        TimerId(TimerIdInner::IpLayer(id))
    }
}

impl<I: Instant> From<TransportLayerTimerId> for TimerId<I> {
    fn from(id: TransportLayerTimerId) -> Self {
        TimerId(TimerIdInner::TransportLayer(id))
    }
}

impl_timer_context!(
    TimerId<<C as InstantContext>::Instant>,
    DeviceLayerTimerId<<C as InstantContext>::Instant>,
    TimerId(TimerIdInner::DeviceLayer(id)),
    id
);
impl_timer_context!(
    TimerId<<C as InstantContext>::Instant>,
    IpLayerTimerId,
    TimerId(TimerIdInner::IpLayer(id)),
    id
);
impl_timer_context!(
    TimerId<<C as InstantContext>::Instant>,
    Ipv4DeviceTimerId<DeviceId<<C as InstantContext>::Instant>>,
    TimerId(TimerIdInner::Ipv4Device(id)),
    id
);
impl_timer_context!(
    TimerId<<C as InstantContext>::Instant>,
    Ipv6DeviceTimerId<DeviceId<<C as InstantContext>::Instant>>,
    TimerId(TimerIdInner::Ipv6Device(id)),
    id
);
impl_timer_context!(
    TimerId<<C as InstantContext>::Instant>,
    TransportLayerTimerId,
    TimerId(TimerIdInner::TransportLayer(id)),
    id
);

/// Handles a generic timer event.
pub fn handle_timer<NonSyncCtx: NonSyncContext>(
    mut sync_ctx: &SyncCtx<NonSyncCtx>,
    ctx: &mut NonSyncCtx,
    id: TimerId<NonSyncCtx::Instant>,
) {
    trace!("handle_timer: dispatching timerid: {:?}", id);

    match id {
        TimerId(TimerIdInner::DeviceLayer(x)) => {
            device::handle_timer(&mut sync_ctx, ctx, x);
        }
        TimerId(TimerIdInner::TransportLayer(x)) => {
            transport::handle_timer(&mut sync_ctx, ctx, x);
        }
        TimerId(TimerIdInner::IpLayer(x)) => {
            ip::handle_timer(&mut sync_ctx, ctx, x);
        }
        TimerId(TimerIdInner::Ipv4Device(x)) => {
            ip::device::handle_ipv4_timer(&mut sync_ctx, ctx, x);
        }
        TimerId(TimerIdInner::Ipv6Device(x)) => {
            ip::device::handle_ipv6_timer(&mut sync_ctx, ctx, x);
        }
        #[cfg(test)]
        TimerId(TimerIdInner::Nop(_)) => {
            ctx.increment_counter("timer::nop");
        }
    }
}

/// A type representing an instant in time.
///
/// `Instant` can be implemented by any type which represents an instant in
/// time. This can include any sort of real-world clock time (e.g.,
/// [`std::time::Instant`]) or fake time such as in testing.
pub trait Instant: Sized + Ord + Copy + Clone + Debug + Send + Sync {
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

/// Get all IPv4 and IPv6 address/subnet pairs configured on a device
pub fn get_all_ip_addr_subnets<NonSyncCtx: NonSyncContext>(
    ctx: &SyncCtx<NonSyncCtx>,
    device: &DeviceId<NonSyncCtx::Instant>,
) -> Vec<AddrSubnetEither> {
    DualStackDeviceHandler::get_all_ip_addr_subnets(&ctx, device)
}

/// Set the IP address and subnet for a device.
pub fn add_ip_addr_subnet<NonSyncCtx: NonSyncContext>(
    mut sync_ctx: &SyncCtx<NonSyncCtx>,
    ctx: &mut NonSyncCtx,
    device: &DeviceId<NonSyncCtx::Instant>,
    addr_sub: AddrSubnetEither,
) -> Result<(), error::ExistsError> {
    map_addr_version!(
        addr_sub: AddrSubnetEither;
        crate::device::add_ip_addr_subnet(&mut sync_ctx, ctx, device, addr_sub)
    )
}

/// Delete an IP address on a device.
pub fn del_ip_addr<NonSyncCtx: NonSyncContext>(
    mut sync_ctx: &SyncCtx<NonSyncCtx>,
    ctx: &mut NonSyncCtx,
    device: &DeviceId<NonSyncCtx::Instant>,
    addr: SpecifiedAddr<IpAddr>,
) -> Result<(), error::NotFoundError> {
    let addr = addr.into();
    map_addr_version!(
        addr: IpAddr;
        crate::device::del_ip_addr(&mut sync_ctx, ctx, device, &addr)
    )
}

/// Adds a route to the forwarding table.
pub fn add_route<NonSyncCtx: NonSyncContext>(
    mut sync_ctx: &SyncCtx<NonSyncCtx>,
    ctx: &mut NonSyncCtx,
    entry: ip::types::AddableEntryEither<DeviceId<NonSyncCtx::Instant>>,
) -> Result<(), ip::forwarding::AddRouteError> {
    let (subnet, device, gateway) = entry.into_subnet_device_gateway();
    match (device, gateway) {
        (Some(device), None) => map_addr_version!(
            subnet: SubnetEither;
            crate::ip::add_device_route::<Ipv4, _, _>(&mut sync_ctx, ctx, subnet, device),
            crate::ip::add_device_route::<Ipv6, _, _>(&mut sync_ctx, ctx, subnet, device)
        )
        .map_err(From::from),
        (None, Some(next_hop)) => {
            let next_hop = next_hop.into();
            map_addr_version!(
                (subnet: SubnetEither, next_hop: IpAddr);
                crate::ip::add_route::<Ipv4, _, _>(&mut sync_ctx, ctx, subnet, next_hop),
                crate::ip::add_route::<Ipv6, _, _>(&mut sync_ctx, ctx, subnet, next_hop),
                unreachable!()
            )
        }
        x => todo!("TODO(https://fxbug.dev/96680): support setting gateway route with device; (device, gateway) = {:?}", x),
    }
}

/// Delete a route from the forwarding table, returning `Err` if no
/// route was found to be deleted.
pub fn del_route<NonSyncCtx: NonSyncContext>(
    mut sync_ctx: &SyncCtx<NonSyncCtx>,
    ctx: &mut NonSyncCtx,
    subnet: SubnetEither,
) -> error::Result<()> {
    map_addr_version!(
        subnet: SubnetEither;
        crate::ip::del_route::<Ipv4, _, _>(&mut sync_ctx, ctx, subnet),
        crate::ip::del_route::<Ipv6, _, _>(&mut sync_ctx, ctx, subnet)
    )
    .map_err(From::from)
}

#[cfg(test)]
mod tests {
    use net_types::{
        ip::{Ip, Ipv4, Ipv6},
        Witness,
    };

    use super::*;
    use crate::testutil::TestIpExt;

    fn test_add_remove_ip_addresses<I: Ip + TestIpExt>() {
        let config = I::FAKE_CONFIG;
        let Ctx { mut sync_ctx, mut non_sync_ctx } = crate::testutil::FakeCtx::default();
        let device = crate::device::add_ethernet_device(
            &mut sync_ctx,
            &mut non_sync_ctx,
            config.local_mac,
            Ipv6::MINIMUM_LINK_MTU.into(),
        );
        crate::device::testutil::enable_device(&mut sync_ctx, &mut non_sync_ctx, &device);

        let ip: IpAddr = I::get_other_ip_address(1).get().into();
        let prefix = config.subnet.prefix();
        let addr_subnet = AddrSubnetEither::new(ip, prefix).unwrap();

        // IP doesn't exist initially.
        assert_eq!(
            get_all_ip_addr_subnets(&sync_ctx, &device).into_iter().find(|&a| a == addr_subnet),
            None
        );

        // Add IP (OK).
        let () =
            add_ip_addr_subnet(&mut sync_ctx, &mut non_sync_ctx, &device, addr_subnet).unwrap();
        assert_eq!(
            get_all_ip_addr_subnets(&sync_ctx, &device).into_iter().find(|&a| a == addr_subnet),
            Some(addr_subnet)
        );

        // Add IP again (already exists).
        assert_eq!(
            add_ip_addr_subnet(&mut sync_ctx, &mut non_sync_ctx, &device, addr_subnet).unwrap_err(),
            error::ExistsError
        );
        assert_eq!(
            get_all_ip_addr_subnets(&sync_ctx, &device).into_iter().find(|&a| a == addr_subnet),
            Some(addr_subnet)
        );

        // Add IP with different subnet (already exists).
        let wrong_addr_subnet = AddrSubnetEither::new(ip, prefix - 1).unwrap();
        assert_eq!(
            add_ip_addr_subnet(&mut sync_ctx, &mut non_sync_ctx, &device, wrong_addr_subnet)
                .unwrap_err(),
            error::ExistsError
        );
        assert_eq!(
            get_all_ip_addr_subnets(&sync_ctx, &device).into_iter().find(|&a| a == addr_subnet),
            Some(addr_subnet)
        );

        let ip = SpecifiedAddr::new(ip).unwrap();
        // Del IP (ok).
        let () = del_ip_addr(&mut sync_ctx, &mut non_sync_ctx, &device, ip.into()).unwrap();
        assert_eq!(
            get_all_ip_addr_subnets(&sync_ctx, &device).into_iter().find(|&a| a == addr_subnet),
            None
        );

        // Del IP again (not found).
        assert_eq!(
            del_ip_addr(&mut sync_ctx, &mut non_sync_ctx, &device, ip.into()).unwrap_err(),
            error::NotFoundError
        );
        assert_eq!(
            get_all_ip_addr_subnets(&sync_ctx, &device).into_iter().find(|&a| a == addr_subnet),
            None
        );
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
