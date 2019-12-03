// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Module for IP level paths' maximum transmission unit (PMTU) size
//! cache support.

use std::collections::HashMap;
use std::marker::PhantomData;
use std::time::Duration;

use log::trace;
use net_types::ip::{Ip, IpAddress};
use never::Never;
use specialize_ip_macro::specialize_ip;

use crate::context::{InstantContext, StateContext, TimerContext, TimerHandler};
use crate::Instant;

/// [RFC 791 section 3.2] requires that an IPv4 node be able to forward
/// datagrams of up to 68 octets without further fragmentation. That is,
/// the minimum MTU of an IPv4 path must be 68 bytes. This is because
/// an IPv4 header may be up to 60 octets, and the minimum fragment is
/// 8 octets.
///
/// [RFC 791 section 3.2]: https://tools.ietf.org/html/rfc791#section-3.2
pub(crate) const IPV4_MIN_MTU: u32 = 68;

/// [RFC 8200 section 5] requires that every link in the Internet have an
/// MTU of 1280 octets or greater. Any link that cannot convey a 1280-
/// octet packet in one piece must provide link-specific fragmentation
/// and reassembly at a layer below IPv6.
///
/// [RFC 8200 section 5]: https://tools.ietf.org/html/rfc8200#section-5
pub(crate) const IPV6_MIN_MTU: u32 = 1280;

/// Time between PMTU maintenance operations.
///
/// Maintenance operations are things like resetting cached PMTU
/// data to force restart PMTU discovery to detect increases in
/// a PMTU.
///
/// 1 hour.
// TODO(ghanan): Make this value configurable by runtime options.
const MAINTENANCE_PERIOD: Duration = Duration::from_secs(3600);

/// Time for a PMTU value to be considered stale.
///
/// 3 hours.
// TODO(ghanan): Make this value configurable by runtime options.
const PMTU_STALE_TIMEOUT: Duration = Duration::from_secs(10800);

/// Common MTU values taken from [RFC 1191 section 7.1].
///
/// This list includes lower bounds of groups of common MTU values that
/// are relatively close to each other, sorted in descending order.
///
/// Note, the RFC does not actually include the value 1280 in the list of
/// plateau values, but we include it here because it is the minimum IPv6
/// MTU value and is not expected to be an uncommon value for MTUs.
///
/// This list MUST be sorted in descending order; methods such as
/// `next_lower_pmtu_plateau` assume `PMTU_PLATEAUS` has this property.
///
/// We use this list when estimating PMTU values when doing PMTU discovery
/// with IPv4 on paths with nodes that do not implement RFC 1191. This
/// list is useful as in practice, relatively few MTU values are in use.
///
/// [RFC 1191 section 7.1]: https://tools.ietf.org/html/rfc1191#section-7.1
const PMTU_PLATEAUS: [u32; 12] =
    [65535, 32000, 17914, 8166, 4352, 2002, 1492, 1280, 1006, 508, 296, 68];

/// The timer ID for the path MTU cache.
#[derive(Clone, PartialEq, Eq)]
pub(crate) struct PmtuTimerId<I: Ip>(PhantomData<I>);

/// The execution context for the path MTU cache.
pub(crate) trait PmtuContext<I: Ip>:
    TimerContext<PmtuTimerId<I>>
    + StateContext<IpLayerPathMtuCache<I, <Self as InstantContext>::Instant>>
{
}

impl<
        I: Ip,
        C: TimerContext<PmtuTimerId<I>>
            + StateContext<IpLayerPathMtuCache<I, <C as InstantContext>::Instant>>,
    > PmtuContext<I> for C
{
}

/// Get the minimum MTU size for a specific IP version, identified by `I`.
#[specialize_ip]
pub(crate) fn min_mtu<I: Ip>() -> u32 {
    #[ipv4]
    let ret = IPV4_MIN_MTU;

    #[ipv6]
    let ret = IPV6_MIN_MTU;

    ret
}

/// Get the PMTU between `src_ip` and `dst_ip`.
///
/// See [`IpLayerPathMtuCache::get_pmtu`].
pub(crate) fn get_pmtu<A: IpAddress, C: PmtuContext<A::Version>>(
    ctx: &C,
    src_ip: A,
    dst_ip: A,
) -> Option<u32> {
    ctx.get_state().get_pmtu(src_ip, dst_ip)
}

/// A handler for PMTU events.
///
/// `PmtuHandler<I>` is implemented by any type which also implements
/// [`PmtuContext<I>`], and it can also be mocked for use in testing. See the
/// [`testutil`] module for an simple mock.
pub(crate) trait PmtuHandler<I: Ip> {
    /// Update the PMTU between `src_ip` and `dst_ip` if `new_mtu` is less than
    /// the current PMTU and does not violate the minimum MTU size requirements
    /// for an IP.
    fn update_pmtu_if_less(&mut self, src_ip: I::Addr, dst_ip: I::Addr, new_mtu: u32);

    /// Update the PMTU between `src_ip` and `dst_ip` to the next lower estimate
    /// from `from`.
    fn update_pmtu_next_lower(&mut self, src_ip: I::Addr, dst_ip: I::Addr, from: u32);
}

impl<I: Ip, C: PmtuContext<I>> PmtuHandler<I> for C {
    fn update_pmtu_if_less(&mut self, src_ip: I::Addr, dst_ip: I::Addr, new_mtu: u32) {
        let _ = update_pmtu_if_less(self, src_ip, dst_ip, new_mtu);
    }

    fn update_pmtu_next_lower(&mut self, src_ip: I::Addr, dst_ip: I::Addr, from: u32) {
        let _ = update_pmtu_next_lower(self, src_ip, dst_ip, from);
    }
}

fn update_pmtu_if_less<I: Ip, C: PmtuContext<I>>(
    ctx: &mut C,
    src_ip: I::Addr,
    dst_ip: I::Addr,
    new_mtu: u32,
) -> Result<Option<u32>, Option<u32>> {
    let prev_mtu = get_pmtu(ctx, src_ip, dst_ip);

    match prev_mtu {
        // No PMTU exists so update.
        None => update_pmtu(ctx, src_ip, dst_ip, new_mtu),
        // A PMTU exists but it is greater than `new_mtu` so update.
        Some(mtu) if new_mtu < mtu => update_pmtu(ctx, src_ip, dst_ip, new_mtu),
        // A PMTU exists but it is less than or equal to `new_mtu` so no need to
        // update.
        _ => {
            trace!("update_pmtu_if_less: Not updating the PMTU  between src {} and dest {} to {}; is {}", src_ip, dst_ip, new_mtu, prev_mtu.unwrap());
            Ok(prev_mtu)
        }
    }
}

/// Returns `Ok((a, b))` on successful update (a lower PMTU value, `b`, exists
/// that does not violate IP specific minimum MTU requirements and it is less
/// than the current PMTU estimate, `a`). Returns `Err(a)` otherwise, where `a`
/// is the same `a` as in the success case.
fn update_pmtu_next_lower<I: Ip, C: PmtuContext<I>>(
    ctx: &mut C,
    src_ip: I::Addr,
    dst_ip: I::Addr,
    from: u32,
) -> Result<(Option<u32>, u32), Option<u32>> {
    if let Some(next_pmtu) = next_lower_pmtu_plateau(from) {
        trace!(
            "update_pmtu_next_lower: Attempting to update PMTU between src {} and dest {} to {}",
            src_ip,
            dst_ip,
            next_pmtu
        );

        update_pmtu_if_less(ctx, src_ip, dst_ip, next_pmtu).map(|x| (x, next_pmtu))
    } else {
        // TODO(ghanan): Should we make sure the current PMTU value is set to
        //               the IP specific minimum MTU value?
        trace!("update_pmtu_next_lower: Not updating PMTU between src {} and dest {} as there is no lower PMTU value from {}", src_ip, dst_ip, from);
        Err(get_pmtu(ctx, src_ip, dst_ip))
    }
}

fn update_pmtu<A: IpAddress, C: PmtuContext<A::Version>>(
    ctx: &mut C,
    src_ip: A,
    dst_ip: A,
    new_mtu: u32,
) -> Result<Option<u32>, Option<u32>> {
    let ret = update_pmtu_inner(ctx, src_ip, dst_ip, new_mtu);
    trace!(
        "update_pmtu: Updated the PMTU between src {} and dest {} to {}; was {:?}",
        src_ip,
        dst_ip,
        new_mtu,
        ret
    );
    ret
}

/// Get next lower PMTU plateau value, if one exists.
fn next_lower_pmtu_plateau(start_mtu: u32) -> Option<u32> {
    for i in 0..PMTU_PLATEAUS.len() {
        let pmtu = PMTU_PLATEAUS[i];

        if pmtu < start_mtu {
            // Current PMTU is less than `start_mtu` and we know
            // `PMTU_PLATEAUS` is sorted so this is the next best
            // PMTU estimate.
            return Some(pmtu);
        }
    }

    None
}

/// The key used to identify a path.
///
/// This is a tuple of (src_ip, dst_ip) as a path is only identified
/// by the source and destination addresses.
// TODO(ghanan): Should device play a part in the key-ing of a path?
#[derive(Copy, Clone, Debug, Hash, PartialEq, Eq)]
pub(crate) struct PathMtuCacheKey<A: IpAddress>(A, A);

impl<A: IpAddress> PathMtuCacheKey<A> {
    fn new(src_ip: A, dst_ip: A) -> Self {
        Self(src_ip, dst_ip)
    }
}

/// Structure to keep track of the PMTU from a (local) source address to
/// some destination address.
type PathMtuCache<A, I> = HashMap<PathMtuCacheKey<A>, PathMtuCacheData<I>>;

/// IP layer PMTU cache data.
pub(crate) struct PathMtuCacheData<I> {
    pmtu: u32,
    last_updated: I,
}

impl<I: Instant> PathMtuCacheData<I> {
    /// Construct a new `PathMtuCacheData`.
    ///
    /// `last_updated` will be set to `now`.
    fn new(pmtu: u32, now: I) -> Self {
        Self { pmtu, last_updated: now }
    }
}

/// IP Layer PMTU cache.
pub(crate) struct IpLayerPathMtuCache<I: Ip, Instant> {
    cache: PathMtuCache<I::Addr, Instant>,
    timer_scheduled: bool,
}

impl<I: Ip, Instant: Clone> IpLayerPathMtuCache<I, Instant> {
    /// Create a new `IpLayerPathMtuCache`.
    pub(crate) fn new() -> Self {
        Self { cache: PathMtuCache::new(), timer_scheduled: false }
    }

    /// Get the last updated [`Instant`] when the PMTU between `src_ip`
    /// and `dst_ip` was updated.
    ///
    /// Returns `None` if no PMTU is known by this `IpLayerPathMtuCache`, else
    /// `Some(x)` where `x` is the PMTU's last updated `Instant` in time.
    ///
    /// [`Instant`]: crate::Instant
    // TODO(rheacock): remove `#[cfg(test)]` when this is used.
    #[cfg(test)]
    pub(crate) fn get_last_updated(&self, src_ip: I::Addr, dst_ip: I::Addr) -> Option<Instant> {
        self.cache.get(&PathMtuCacheKey::new(src_ip, dst_ip)).map(|x| x.last_updated.clone())
    }

    /// Get the PMTU between `src_ip` and `dst_ip`.
    ///
    /// Returns `None` if no PMTU is known by this `IpLayerPathMtuCache`, else
    /// `Some(x)` where `x` is the current estimate of the PMTU.
    pub(crate) fn get_pmtu(&self, src_ip: I::Addr, dst_ip: I::Addr) -> Option<u32> {
        self.cache.get(&PathMtuCacheKey::new(src_ip, dst_ip)).map(|x| x.pmtu)
    }
}

/// Update the PMTU between `src_ip` and `dst_ip` if `new_mtu` does not violate
/// IP specific minimum MTU requirements.
///
/// Returns `Err(x)` if the `new_mtu` is less than the minimum MTU for an IP
/// where the same `x` is returned in the success case (`Ok(x)`). `x` is the
/// PMTU known by this `IpLayerPathMtuCache` before being updated. `x` will be
/// `None` if no PMTU is known, else `Some(y)` where `y` is the last estimate of
/// the PMTU.
///
/// If there is no PMTU maintenance task scheduled yet, `update_pmtu` will
/// schedule one to happen after a duration of `SCHEDULE_TIMEOUT` from the
/// current time instant known by `dispatcher`.
fn update_pmtu_inner<I: Ip, C: PmtuContext<I>>(
    ctx: &mut C,
    src_ip: I::Addr,
    dst_ip: I::Addr,
    new_mtu: u32,
) -> Result<Option<u32>, Option<u32>> {
    // New MTU must not be smaller than the minimum MTU for an IP.
    if new_mtu < min_mtu::<I>() {
        return Err(ctx.get_state_mut().get_pmtu(src_ip, dst_ip));
    }

    let key = PathMtuCacheKey::new(src_ip, dst_ip);
    let now = ctx.now();
    let ret = if let Some(data) = ctx.get_state_mut().cache.get_mut(&key) {
        let prev_pmtu = data.pmtu;
        data.pmtu = new_mtu;
        data.last_updated = now;
        Ok(Some(prev_pmtu))
    } else {
        let val = PathMtuCacheData::new(new_mtu, ctx.now());
        assert!(ctx.get_state_mut().cache.insert(key, val).is_none());
        Ok(None)
    };

    // Make sure we have a scheduled task to handle PMTU maintenance. If we
    // don't, create one.
    if !ctx.get_state().timer_scheduled {
        // We are guaranteed that this call will not panic because a panic will
        // only occur if there is already a PMTU maintenance task scheduled. We
        // will only reach here if there is no maintenance task scheduled so we
        // know the panic condition will not be triggered.
        create_maintenance_timer(ctx);
    }

    ret
}

/// A handler for PMTU timer events.
///
/// This performs scheduled maintenance on PMTU data such as resetting PMTU
/// values of stale cached values to restart the PMTU discovery process.
///
/// This type cannot be constructed, and is only meant to be used at the type
/// level. We implement [`TimerHandler`] for `PmtuTimerHandler` rather than just
/// provide the top-level `handle_timer` functions so that `PmtuTimerHandler`
/// can be used in tests with the [`DummyTimerContextExt`] trait and with the
/// [`DummyNetwork`] type.
///
/// [`DummyTimerContextExt`]: crate::context::testutil::DummyTimerContextExt
/// [`DummyNetwork`]: crate::context::testutil::DummyNetwork
pub(crate) struct PmtuTimerHandler<I> {
    _ip: PhantomData<I>,
    _never: Never,
}

impl<I: Ip, C: PmtuContext<I>> TimerHandler<C, PmtuTimerId<I>> for PmtuTimerHandler<I> {
    fn handle_timer(ctx: &mut C, _timer: PmtuTimerId<I>) {
        let curr_time = ctx.now();
        let mut cache = ctx.get_state_mut();

        // Make sure we expected this timer to fire.
        assert!(cache.timer_scheduled);

        // Now that this timer has fired, no others should currently be scheduled.
        cache.timer_scheduled = false;

        // Remove all stale PMTU data to force restart the PMTU discovery process.
        // This will be ok because the next time we try to send a packet to some
        // node, we will update the PMTU with the first known potential PMTU (the
        // first link's (connected to the node attempting PMTU discovery)) PMTU.
        cache.cache.retain(|_k, v| {
            // We know the call to `duration_since` will not panic because all the
            // entries in the cache should have been updated before this timer/PMTU
            // maintenance task was run. Therefore, `curr_time` will be greater than
            // `v.last_updated` for all `v`.
            //
            // TODO(ghanan): Add per-path options as per RFC 1981 section 5.3.
            //               Specifically, some links/paths may not need to have
            //               PMTU rediscovered as the PMTU will never change.
            //
            // TODO(ghanan): Consider not simply deleting all stale PMTU data as
            //               this may cause packets to be dropped every time the
            //               data seems to get stale when really it is still valid.
            //               Considering the use case, PMTU value changes may be
            //               infrequent so it may be enough to just use a long stale
            //               timer.
            (curr_time.duration_since(v.last_updated) < PMTU_STALE_TIMEOUT)
        });

        // Only attempt to create the next maintenance task if we still have PMTU
        // entries in this cache. If we don't, it would be a waste to schedule the
        // timer. We will let the next creation of a PMTU entry create the timer.
        //
        // See `IpLayerPathMtuCache::update_pmtu`.
        if !cache.cache.is_empty() {
            // We are guaranteed that this call will not panic because a panic will
            // only occur if there is already a PMTU maintenance task scheduled. We
            // will only reach here after starting a maintenance task and clear the
            // task's `TimerId` so the panic condition will not be triggered.
            create_maintenance_timer(ctx);
        }
    }
}

/// Handle a scheduled PMTU timer firing.
///
/// See [`PmtuTimerHandler`] for more information.
pub(crate) fn handle_pmtu_timer<I: Ip, C: PmtuContext<I>>(ctx: &mut C) {
    PmtuTimerHandler::<I>::handle_timer(ctx, PmtuTimerId::<I>(PhantomData::<I>))
}

/// Create a PMTU maintenance task to occur after a duration of
/// `MAINTENANCE_PERIOD`.
///
/// # Panics
///
/// Panics if there is already a maintenance task scheduled that has not yet
/// run.
fn create_maintenance_timer<I: Ip, C: PmtuContext<I>>(ctx: &mut C) {
    let mut cache = ctx.get_state_mut();
    // Should not create a new job if we already have a maintenance job to be
    // run.
    assert!(!cache.timer_scheduled);

    cache.timer_scheduled = true;
    assert!(ctx.schedule_timer(MAINTENANCE_PERIOD, PmtuTimerId(PhantomData)).is_none());
}

#[cfg(test)]
#[macro_use]
pub(crate) mod testutil {
    use super::*;

    // TODO(rheacock): remove `#[allow(dead_code)]` when the impl_pmtu_handler
    // macro is used.
    #[allow(dead_code)]
    pub(crate) struct UpdatePmtuIfLessArgs<A: IpAddress> {
        pub(crate) src_ip: A,
        pub(crate) dst_ip: A,
        pub(crate) new_mtu: u32,
    }

    // TODO(rheacock): remove `#[allow(dead_code)]` when the impl_pmtu_handler
    // macro is used.
    #[allow(dead_code)]
    pub(crate) struct UpdatePmtuNextLowerArgs<A: IpAddress> {
        pub(crate) src_ip: A,
        pub(crate) dst_ip: A,
        pub(crate) from: u32,
    }

    #[derive(Default)]
    pub(crate) struct DummyPmtuState<A: IpAddress> {
        /// Each time `PmtuHandler::update_pmtu_if_less` is called, a new entry
        /// is pushed onto this vector.
        pub(crate) update_pmtu_if_less: Vec<UpdatePmtuIfLessArgs<A>>,
        /// Each time `PmtuHandler::update_pmtu_next_lower` is called, a new
        /// entry is pushed onto this vector.
        pub(crate) update_pmtu_next_lower: Vec<UpdatePmtuNextLowerArgs<A>>,
    }

    /// Implement the `PmtuHandler<$ip_version>` trait for a particular type
    /// which implements `AsMut<DummyPmtuState<$ip_version::Addr>>`.
    macro_rules! impl_pmtu_handler {
        ($ty:ty, $ip_version:ident) => {
            impl PmtuHandler<net_types::ip::$ip_version> for $ty {
                fn update_pmtu_if_less(
                    &mut self,
                    src_ip: <net_types::ip::$ip_version as net_types::ip::Ip>::Addr,
                    dst_ip: <net_types::ip::$ip_version as net_types::ip::Ip>::Addr,
                    new_mtu: u32,
                ) {
                    let state: &mut DummyPmtuState<
                        <net_types::ip::$ip_version as net_types::ip::Ip>::Addr,
                    > = self.as_mut();
                    state.update_pmtu_if_less.push(
                        crate::ip::path_mtu::testutil::UpdatePmtuIfLessArgs {
                            src_ip,
                            dst_ip,
                            new_mtu,
                        },
                    );
                }

                fn update_pmtu_next_lower(
                    &mut self,
                    src_ip: <net_types::ip::$ip_version as net_types::ip::Ip>::Addr,
                    dst_ip: <net_types::ip::$ip_version as net_types::ip::Ip>::Addr,
                    from: u32,
                ) {
                    let state: &mut DummyPmtuState<
                        <net_types::ip::$ip_version as net_types::ip::Ip>::Addr,
                    > = self.as_mut();
                    state.update_pmtu_next_lower.push(
                        crate::ip::path_mtu::testutil::UpdatePmtuNextLowerArgs {
                            src_ip,
                            dst_ip,
                            from,
                        },
                    );
                }
            }
        };
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use net_types::ip::{Ipv4, Ipv6};
    use net_types::{SpecifiedAddr, Witness};
    use specialize_ip_macro::ip_test;

    use crate::context::testutil::{DummyInstant, DummyTimerContextExt};
    use crate::testutil::get_dummy_config;

    /// A dummy [`PmtuContext`] that stores an [`IpLayerPathMtuCache`].
    struct DummyPmtuContext<I: Ip> {
        cache: IpLayerPathMtuCache<I, DummyInstant>,
    }

    impl<I: Ip> Default for DummyPmtuContext<I> {
        fn default() -> Self {
            DummyPmtuContext { cache: IpLayerPathMtuCache::new() }
        }
    }

    type DummyContext<I> =
        crate::context::testutil::DummyContext<DummyPmtuContext<I>, PmtuTimerId<I>>;

    impl<I: Ip> StateContext<IpLayerPathMtuCache<I, DummyInstant>> for DummyContext<I> {
        fn get_state_with(&self, _id: ()) -> &IpLayerPathMtuCache<I, DummyInstant> {
            &self.get_ref().cache
        }

        fn get_state_mut_with(&mut self, _id: ()) -> &mut IpLayerPathMtuCache<I, DummyInstant> {
            &mut self.get_mut().cache
        }
    }

    impl<I: Ip> PmtuContext<I> for DummyContext<I> {}

    /// Get an IPv4 or IPv6 address within the same subnet as that of `DUMMY_CONFIG_*`,
    /// but with the last octet set to `3`.
    fn get_other_ip_address<A: IpAddress>() -> SpecifiedAddr<A> {
        crate::testutil::get_other_ip_address::<A>(3)
    }

    #[test]
    fn test_next_lower_pmtu_plateau() {
        assert_eq!(next_lower_pmtu_plateau(65536).unwrap(), 65535);
        assert_eq!(next_lower_pmtu_plateau(65535).unwrap(), 32000);
        assert_eq!(next_lower_pmtu_plateau(65534).unwrap(), 32000);
        assert_eq!(next_lower_pmtu_plateau(32001).unwrap(), 32000);
        assert_eq!(next_lower_pmtu_plateau(32000).unwrap(), 17914);
        assert_eq!(next_lower_pmtu_plateau(31999).unwrap(), 17914);
        assert_eq!(next_lower_pmtu_plateau(1281).unwrap(), 1280);
        assert_eq!(next_lower_pmtu_plateau(1280).unwrap(), 1006);
        assert_eq!(next_lower_pmtu_plateau(69).unwrap(), 68);
        assert_eq!(next_lower_pmtu_plateau(68), None);
        assert_eq!(next_lower_pmtu_plateau(67), None);
        assert_eq!(next_lower_pmtu_plateau(0), None);
    }

    #[ip_test]
    fn test_ip_path_mtu_cache_ctx<I: Ip>() {
        let dummy_config = get_dummy_config::<I::Addr>();
        let mut ctx = DummyContext::<I>::default();

        // Nothing in the cache yet
        assert_eq!(
            get_pmtu(&mut ctx, dummy_config.local_ip.get(), dummy_config.remote_ip.get()),
            None
        );
        assert_eq!(
            ctx.get_state()
                .get_last_updated(dummy_config.local_ip.get(), dummy_config.remote_ip.get()),
            None
        );

        let new_mtu1 = min_mtu::<I>() + 50;
        let start_time = ctx.now();
        let duration = Duration::from_secs(1);

        // Advance time to 1s.
        assert_eq!(ctx.trigger_timers_for::<PmtuTimerHandler<I>>(duration), 0);

        // Update pmtu from local to remote.
        // PMTU should be updated to `new_mtu1` and last updated instant
        // should be updated to the start of the test + 1s.
        assert_eq!(
            update_pmtu(
                &mut ctx,
                dummy_config.local_ip.get(),
                dummy_config.remote_ip.get(),
                new_mtu1
            )
            .unwrap(),
            None
        );

        // Advance time to 2s.
        assert_eq!(ctx.trigger_timers_for::<PmtuTimerHandler<I>>(duration), 0);

        // Make sure the update worked.
        // PMTU should be updated to `new_mtu1` and last updated instant
        // should be updated to the start of the test + 1s (when the
        // update occurred.
        assert_eq!(
            get_pmtu(&mut ctx, dummy_config.local_ip.get(), dummy_config.remote_ip.get()).unwrap(),
            new_mtu1
        );
        assert_eq!(
            ctx.get_state()
                .get_last_updated(dummy_config.local_ip.get(), dummy_config.remote_ip.get())
                .unwrap(),
            start_time + duration
        );

        let new_mtu2 = min_mtu::<I>() + 100;

        // Advance time to 3s.
        assert_eq!(ctx.trigger_timers_for::<PmtuTimerHandler<I>>(duration), 0);

        // Updating again should return the last pmtu
        // PMTU should be updated to `new_mtu2` and last updated instant
        // should be updated to the start of the test + 3s.
        assert_eq!(
            update_pmtu(
                &mut ctx,
                dummy_config.local_ip.get(),
                dummy_config.remote_ip.get(),
                new_mtu2
            )
            .unwrap()
            .unwrap(),
            new_mtu1
        );

        // Advance time to 4s.
        assert_eq!(ctx.trigger_timers_for::<PmtuTimerHandler<I>>(duration), 0);

        // Make sure the update worked.
        // PMTU should be updated to `new_mtu2` and last updated instant
        // should be updated to the start of the test + 3s (when the
        // update occurred).
        assert_eq!(
            get_pmtu(&mut ctx, dummy_config.local_ip.get(), dummy_config.remote_ip.get()).unwrap(),
            new_mtu2
        );
        assert_eq!(
            ctx.get_state()
                .get_last_updated(dummy_config.local_ip.get(), dummy_config.remote_ip.get())
                .unwrap(),
            start_time + (duration * 3)
        );

        let new_mtu3 = new_mtu2 - 10;

        // Advance time to 5s.
        assert_eq!(ctx.trigger_timers_for::<PmtuTimerHandler<I>>(duration), 0);

        // Make sure update only if new PMTU is less than current (it is).
        // PMTU should be updated to `new_mtu3` and last updated instant
        // should be updated to the start of the test + 5s.
        assert_eq!(
            update_pmtu_if_less::<I, _>(
                &mut ctx,
                dummy_config.local_ip.get(),
                dummy_config.remote_ip.get(),
                new_mtu3
            )
            .unwrap()
            .unwrap(),
            new_mtu2
        );

        // Advance time to 6s.
        assert_eq!(ctx.trigger_timers_for::<PmtuTimerHandler<I>>(duration), 0);

        // Make sure the update worked.
        // PMTU should be updated to `new_mtu3` and last updated instant
        // should be updated to the start of the test + 5s (when the
        // update occurred).
        assert_eq!(
            get_pmtu(&mut ctx, dummy_config.local_ip.get(), dummy_config.remote_ip.get()).unwrap(),
            new_mtu3
        );
        let last_updated = start_time + (duration * 5);
        assert_eq!(
            ctx.get_state()
                .get_last_updated(dummy_config.local_ip.get(), dummy_config.remote_ip.get())
                .unwrap(),
            last_updated
        );

        let new_mtu4 = new_mtu3 + 50;

        // Advance time to 7s.
        assert_eq!(ctx.trigger_timers_for::<PmtuTimerHandler<I>>(duration), 0);

        // Make sure update only if new PMTU is less than current (it isn't)
        assert_eq!(
            update_pmtu_if_less::<I, _>(
                &mut ctx,
                dummy_config.local_ip.get(),
                dummy_config.remote_ip.get(),
                new_mtu4
            )
            .unwrap()
            .unwrap(),
            new_mtu3
        );

        // Advance time to 8s.
        assert_eq!(ctx.trigger_timers_for::<PmtuTimerHandler<I>>(duration), 0);

        // Make sure the update didn't work.
        // PMTU and last updated should not have changed.
        assert_eq!(
            get_pmtu(&mut ctx, dummy_config.local_ip.get(), dummy_config.remote_ip.get()).unwrap(),
            new_mtu3
        );
        assert_eq!(
            ctx.get_state()
                .get_last_updated(dummy_config.local_ip.get(), dummy_config.remote_ip.get())
                .unwrap(),
            last_updated
        );

        let low_mtu = min_mtu::<I>() - 1;

        // Advance time to 9s.
        assert_eq!(ctx.trigger_timers_for::<PmtuTimerHandler<I>>(duration), 0);

        // Updating with mtu value less than the minimum MTU should fail.
        assert_eq!(
            update_pmtu_if_less::<I, _>(
                &mut ctx,
                dummy_config.local_ip.get(),
                dummy_config.remote_ip.get(),
                low_mtu
            )
            .unwrap_err()
            .unwrap(),
            new_mtu3
        );

        // Advance time to 10s.
        assert_eq!(ctx.trigger_timers_for::<PmtuTimerHandler<I>>(duration), 0);

        // Make sure the update didn't work.
        // PMTU and last updated should not have changed.
        assert_eq!(
            get_pmtu(&mut ctx, dummy_config.local_ip.get(), dummy_config.remote_ip.get()).unwrap(),
            new_mtu3
        );
        assert_eq!(
            ctx.get_state()
                .get_last_updated(dummy_config.local_ip.get(), dummy_config.remote_ip.get())
                .unwrap(),
            last_updated
        );
    }

    #[ip_test]
    fn test_ip_pmtu_task<I: Ip>() {
        let dummy_config = get_dummy_config::<I::Addr>();
        let mut ctx = DummyContext::<I>::default();

        // Make sure there are no timers.
        assert_eq!(ctx.timers().len(), 0);

        let new_mtu1 = min_mtu::<I>() + 50;
        let start_time = ctx.now();
        let duration = Duration::from_secs(1);

        // Advance time to 1s.
        assert_eq!(ctx.trigger_timers_for::<PmtuTimerHandler<I>>(duration), 0);

        // Update pmtu from local to remote.
        // PMTU should be updated to `new_mtu1` and last updated instant
        // should be updated to the start of the test + 1s.
        assert_eq!(
            update_pmtu(
                &mut ctx,
                dummy_config.local_ip.get(),
                dummy_config.remote_ip.get(),
                new_mtu1
            )
            .unwrap(),
            None
        );

        // Make sure a task got scheduled.
        assert_eq!(ctx.timers().len(), 1);

        // Advance time to 2s.
        assert_eq!(ctx.trigger_timers_for::<PmtuTimerHandler<I>>(duration), 0);

        // Make sure the update worked.
        // PMTU should be updated to `new_mtu1` and last updated instant
        // should be updated to the start of the test + 1s (when the
        // update occurred.
        assert_eq!(
            get_pmtu(&mut ctx, dummy_config.local_ip.get(), dummy_config.remote_ip.get()).unwrap(),
            new_mtu1
        );
        assert_eq!(
            ctx.get_state()
                .get_last_updated(dummy_config.local_ip.get(), dummy_config.remote_ip.get())
                .unwrap(),
            start_time + duration
        );

        // Advance time to 30mins.
        assert_eq!(ctx.trigger_timers_for::<PmtuTimerHandler<I>>(duration * 1798), 0);

        // Update pmtu from local to another remote.
        // PMTU should be updated to `new_mtu1` and last updated instant
        // should be updated to the start of the test + 1s.
        let other_ip = get_other_ip_address::<I::Addr>();
        let new_mtu2 = min_mtu::<I>() + 100;
        assert_eq!(
            update_pmtu(&mut ctx, dummy_config.local_ip.get(), other_ip.get(), new_mtu2).unwrap(),
            None
        );

        // Make sure there is still a task scheduled.
        // (we know no timers got triggered because the `run_for`
        // methods returned 0 so far).
        assert_eq!(ctx.timers().len(), 1);

        // Make sure the update worked.
        // PMTU should be updated to `new_mtu2` and last updated instant
        // should be updated to the start of the test + 30mins + 2s (when the
        // update occurred.
        assert_eq!(
            get_pmtu(&mut ctx, dummy_config.local_ip.get(), other_ip.get()).unwrap(),
            new_mtu2
        );
        assert_eq!(
            ctx.get_state().get_last_updated(dummy_config.local_ip.get(), other_ip.get()).unwrap(),
            start_time + (duration * 1800)
        );
        // Make sure first update is still in the cache.
        assert_eq!(
            get_pmtu(&mut ctx, dummy_config.local_ip.get(), dummy_config.remote_ip.get()).unwrap(),
            new_mtu1
        );
        assert_eq!(
            ctx.get_state()
                .get_last_updated(dummy_config.local_ip.get(), dummy_config.remote_ip.get())
                .unwrap(),
            start_time + duration
        );

        // Advance time to 1hr + 1s.
        // Should have triggered a timer.
        assert_eq!(ctx.trigger_timers_for::<PmtuTimerHandler<I>>(duration * 1801), 1);
        // Make sure none of the cache data has been marked as
        // stale and removed.
        assert_eq!(
            get_pmtu(&mut ctx, dummy_config.local_ip.get(), dummy_config.remote_ip.get()).unwrap(),
            new_mtu1
        );
        assert_eq!(
            ctx.get_state()
                .get_last_updated(dummy_config.local_ip.get(), dummy_config.remote_ip.get())
                .unwrap(),
            start_time + duration
        );
        assert_eq!(
            get_pmtu(&mut ctx, dummy_config.local_ip.get(), other_ip.get()).unwrap(),
            new_mtu2
        );
        assert_eq!(
            ctx.get_state().get_last_updated(dummy_config.local_ip.get(), other_ip.get()).unwrap(),
            start_time + (duration * 1800)
        );
        // Should still have another task scheduled.
        assert_eq!(ctx.timers().len(), 1);

        // Advance time to 3hr + 1s.
        // Should have triggered 2 timers.
        assert_eq!(ctx.trigger_timers_for::<PmtuTimerHandler<I>>(duration * 7200), 2);
        // Make sure only the earlier PMTU data got marked
        // as stale and removed.
        assert!(
            get_pmtu(&mut ctx, dummy_config.local_ip.get(), dummy_config.remote_ip.get()).is_none()
        );
        assert!(ctx
            .get_state()
            .get_last_updated(dummy_config.local_ip.get(), dummy_config.remote_ip.get())
            .is_none());
        assert_eq!(
            get_pmtu(&mut ctx, dummy_config.local_ip.get(), other_ip.get()).unwrap(),
            new_mtu2
        );
        assert_eq!(
            ctx.get_state().get_last_updated(dummy_config.local_ip.get(), other_ip.get()).unwrap(),
            start_time + (duration * 1800)
        );
        // Should still have another task scheduled.
        assert_eq!(ctx.timers().len(), 1);

        // Advance time to 4hr + 1s.
        // Should have triggered 1 timers.
        assert_eq!(ctx.trigger_timers_for::<PmtuTimerHandler<I>>(duration * 3600), 1);
        // Make sure both PMTU data got marked
        // as stale and removed.
        assert!(
            get_pmtu(&mut ctx, dummy_config.local_ip.get(), dummy_config.remote_ip.get()).is_none()
        );
        assert!(ctx
            .get_state()
            .get_last_updated(dummy_config.local_ip.get(), dummy_config.remote_ip.get())
            .is_none());
        assert!(get_pmtu(&mut ctx, dummy_config.local_ip.get(), other_ip.get()).is_none(),);
        assert!(ctx
            .get_state()
            .get_last_updated(dummy_config.local_ip.get(), other_ip.get())
            .is_none(),);
        // Should not have a task scheduled since there is no more PMTU
        // data.
        assert_eq!(ctx.timers().len(), 0);
    }
}
