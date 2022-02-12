// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Module for IP level paths' maximum transmission unit (PMTU) size
//! cache support.

use alloc::collections::HashMap;
use core::time::Duration;

use log::trace;
use net_types::ip::{Ip, IpAddress, IpVersionMarker};

use crate::context::TimerContext;

/// Time between PMTU maintenance operations.
///
/// Maintenance operations are things like resetting cached PMTU data to force
/// restart PMTU discovery to detect increases in a PMTU.
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
/// This list includes lower bounds of groups of common MTU values that are
/// relatively close to each other, sorted in descending order.
///
/// Note, the RFC does not actually include the value 1280 in the list of
/// plateau values, but we include it here because it is the minimum IPv6 MTU
/// value and is not expected to be an uncommon value for MTUs.
///
/// This list MUST be sorted in descending order; methods such as
/// `next_lower_pmtu_plateau` assume `PMTU_PLATEAUS` has this property.
///
/// We use this list when estimating PMTU values when doing PMTU discovery with
/// IPv4 on paths with nodes that do not implement RFC 1191. This list is useful
/// as in practice, relatively few MTU values are in use.
///
/// [RFC 1191 section 7.1]: https://tools.ietf.org/html/rfc1191#section-7.1
const PMTU_PLATEAUS: [u32; 12] =
    [65535, 32000, 17914, 8166, 4352, 2002, 1492, 1280, 1006, 508, 296, 68];

/// The timer ID for the path MTU cache.
#[derive(Default, Clone, Debug, PartialEq, Eq, Hash)]
pub(crate) struct PmtuTimerId<I: Ip>(IpVersionMarker<I>);

/// The execution context for the path MTU cache.
pub(crate) trait PmtuContext<I: Ip>: TimerContext<PmtuTimerId<I>> {}
impl<I: Ip, C: TimerContext<PmtuTimerId<I>>> PmtuContext<I> for C {}

/// A handler for incoming PMTU events.
///
/// `PmtuHandler` is intended to serve as the interface between ICMP the IP
/// layer, which holds the PMTU cache. In production, method calls are delegated
/// to a real [`PmtuCache`], while in testing, method calls may be delegated to
/// a dummy implementation.
pub(crate) trait PmtuHandler<I: Ip> {
    /// Updates the PMTU between `src_ip` and `dst_ip` if `new_mtu` is less than
    /// the current PMTU and does not violate the minimum MTU size requirements
    /// for an IP.
    fn update_pmtu_if_less(&mut self, src_ip: I::Addr, dst_ip: I::Addr, new_mtu: u32);

    /// Updates the PMTU between `src_ip` and `dst_ip` to the next lower
    /// estimate from `from`.
    fn update_pmtu_next_lower(&mut self, src_ip: I::Addr, dst_ip: I::Addr, from: u32);
}

/// The key used to identify a path.
///
/// This is a tuple of (src_ip, dst_ip) as a path is only identified by the
/// source and destination addresses.
// TODO(ghanan): Should device play a part in the key-ing of a path?
#[derive(Copy, Clone, Debug, Hash, PartialEq, Eq)]
pub(crate) struct PmtuCacheKey<A: IpAddress>(A, A);

impl<A: IpAddress> PmtuCacheKey<A> {
    fn new(src_ip: A, dst_ip: A) -> Self {
        Self(src_ip, dst_ip)
    }
}

/// IP layer PMTU cache data.
#[derive(Debug, PartialEq)]
pub(crate) struct PmtuCacheData<I> {
    pmtu: u32,
    last_updated: I,
}

impl<I: crate::Instant> PmtuCacheData<I> {
    /// Construct a new `PmtuCacheData`.
    ///
    /// `last_updated` will be set to `now`.
    fn new(pmtu: u32, now: I) -> Self {
        Self { pmtu, last_updated: now }
    }
}

/// A path MTU cache.
pub(crate) struct PmtuCache<I: Ip, Instant> {
    cache: HashMap<PmtuCacheKey<I::Addr>, PmtuCacheData<Instant>>,
    timer_scheduled: bool,
}

impl<I: Ip, Instant> Default for PmtuCache<I, Instant> {
    fn default() -> PmtuCache<I, Instant> {
        PmtuCache { cache: HashMap::new(), timer_scheduled: false }
    }
}

impl<I: Ip, Instant: crate::Instant> PmtuCache<I, Instant> {
    /// Gets the PMTU between `src_ip` and `dst_ip`.
    pub(crate) fn get_pmtu(&self, src_ip: I::Addr, dst_ip: I::Addr) -> Option<u32> {
        self.cache.get(&PmtuCacheKey::new(src_ip, dst_ip)).map(|x| x.pmtu)
    }

    /// Updates the PMTU between `src_ip` and `dst_ip` if `new_mtu` is less than
    /// the current PMTU and does not violate the minimum MTU size requirements
    /// for an IP.
    pub(crate) fn update_pmtu_if_less<C: PmtuContext<I, Instant = Instant>>(
        &mut self,
        ctx: &mut C,
        src_ip: I::Addr,
        dst_ip: I::Addr,
        new_mtu: u32,
    ) -> Result<Option<u32>, Option<u32>> {
        match self.get_pmtu(src_ip, dst_ip) {
            // No PMTU exists so update.
            None => self.update_pmtu(ctx, src_ip, dst_ip, new_mtu),
            // A PMTU exists but it is greater than `new_mtu` so update.
            Some(prev_mtu) if new_mtu < prev_mtu => self.update_pmtu(ctx, src_ip, dst_ip, new_mtu),
            // A PMTU exists but it is less than or equal to `new_mtu` so no need to
            // update.
            Some(prev_mtu) => {
                trace!("update_pmtu_if_less: Not updating the PMTU  between src {} and dest {} to {}; is {}", src_ip, dst_ip, new_mtu, prev_mtu);
                Ok(Some(prev_mtu))
            }
        }
    }

    /// Updates the PMTU between `src_ip` and `dst_ip` to the next lower
    /// estimate from `from`.
    ///
    /// Returns `Ok((a, b))` on successful update (a lower PMTU value, `b`,
    /// exists that does not violate IP specific minimum MTU requirements and it
    /// is less than the current PMTU estimate, `a`). Returns `Err(a)`
    /// otherwise, where `a` is the same `a` as in the success case.
    pub(crate) fn update_pmtu_next_lower<C: PmtuContext<I, Instant = Instant>>(
        &mut self,
        ctx: &mut C,
        src_ip: I::Addr,
        dst_ip: I::Addr,
        from: u32,
    ) -> Result<Option<u32>, Option<u32>> {
        if let Some(next_pmtu) = next_lower_pmtu_plateau(from) {
            trace!(
                "update_pmtu_next_lower: Attempting to update PMTU between src {} and dest {} to {}",
                src_ip,
                dst_ip,
                next_pmtu
            );

            self.update_pmtu_if_less(ctx, src_ip, dst_ip, next_pmtu)
        } else {
            // TODO(ghanan): Should we make sure the current PMTU value is set
            //               to the IP specific minimum MTU value?
            trace!("update_pmtu_next_lower: Not updating PMTU between src {} and dest {} as there is no lower PMTU value from {}", src_ip, dst_ip, from);
            Err(self.get_pmtu(src_ip, dst_ip))
        }
    }

    /// Updates the PMTU between `src_ip` and `dst_ip` if `new_mtu` does not
    /// violate IP-specific minimum MTU requirements.
    ///
    /// Returns `Err(x)` if the `new_mtu` is less than the minimum MTU for an IP
    /// where the same `x` is returned in the success case (`Ok(x)`). `x` is the
    /// PMTU known by this `PmtuCache` before being updated. `x` will be `None`
    /// if no PMTU is known, else `Some(y)` where `y` is the last estimate of
    /// the PMTU.
    ///
    /// If there is no PMTU maintenance task scheduled yet, `update_pmtu` will
    /// schedule one to happen after a duration of `SCHEDULE_TIMEOUT` from the
    /// current time instant known by `dispatcher`.
    fn update_pmtu<C: PmtuContext<I, Instant = Instant>>(
        &mut self,
        ctx: &mut C,
        src_ip: I::Addr,
        dst_ip: I::Addr,
        new_mtu: u32,
    ) -> Result<Option<u32>, Option<u32>> {
        // New MTU must not be smaller than the minimum MTU for an IP.
        if new_mtu < I::MINIMUM_LINK_MTU.into() {
            return Err(self.get_pmtu(src_ip, dst_ip));
        }

        let key = PmtuCacheKey::new(src_ip, dst_ip);
        let now = ctx.now();
        let ret = if let Some(data) = self.cache.get_mut(&key) {
            let prev_pmtu = data.pmtu;
            data.pmtu = new_mtu;
            data.last_updated = now;
            Ok(Some(prev_pmtu))
        } else {
            let val = PmtuCacheData::new(new_mtu, ctx.now());
            assert!(self.cache.insert(key, val).is_none());
            Ok(None)
        };

        // Make sure we have a scheduled task to handle PMTU maintenance. If we
        // don't, create one.
        if !self.timer_scheduled {
            self.timer_scheduled = true;
            assert_eq!(ctx.schedule_timer(MAINTENANCE_PERIOD, PmtuTimerId::default()), None);
        }

        ret
    }

    pub(crate) fn handle_timer<C: PmtuContext<I, Instant = Instant>>(
        &mut self,
        ctx: &mut C,
        _timer: PmtuTimerId<I>,
    ) {
        // Make sure we expected this timer to fire.
        assert!(self.timer_scheduled);

        // Now that this timer has fired, no others should currently be
        // scheduled.
        self.timer_scheduled = false;

        // Remove all stale PMTU data to force restart the PMTU discovery
        // process. This will be ok because the next time we try to send a
        // packet to some node, we will update the PMTU with the first known
        // potential PMTU (the first link's (connected to the node attempting
        // PMTU discovery)) PMTU.
        self.cache.retain(|_k, v| {
            // We know the call to `duration_since` will not panic because all
            // the entries in the cache should have been updated before this
            // timer/PMTU maintenance task was run. Therefore, `curr_time` will
            // be greater than `v.last_updated` for all `v`.
            //
            // TODO(ghanan): Add per-path options as per RFC 1981 section 5.3.
            //               Specifically, some links/paths may not need to have
            //               PMTU rediscovered as the PMTU will never change.
            //
            // TODO(ghanan): Consider not simply deleting all stale PMTU data as
            //               this may cause packets to be dropped every time the
            //               data seems to get stale when really it is still
            //               valid. Considering the use case, PMTU value changes
            //               may be infrequent so it may be enough to just use a
            //               long stale timer.
            ctx.now().duration_since(v.last_updated) < PMTU_STALE_TIMEOUT
        });

        // Only attempt to create the next maintenance task if we still have
        // PMTU entries in this cache. If we don't, it would be a waste to
        // schedule the timer. We will let the next creation of a PMTU entry
        // create the timer.
        //
        // See `IpLayerPathMtuCache::update_pmtu`.
        if !self.cache.is_empty() {
            self.timer_scheduled = true;
            assert_eq!(ctx.schedule_timer(MAINTENANCE_PERIOD, PmtuTimerId::default()), None);
        }
    }
}

/// Get next lower PMTU plateau value, if one exists.
fn next_lower_pmtu_plateau(start_mtu: u32) -> Option<u32> {
    for i in 0..PMTU_PLATEAUS.len() {
        let pmtu = PMTU_PLATEAUS[i];

        if pmtu < start_mtu {
            // Current PMTU is less than `start_mtu` and we know `PMTU_PLATEAUS`
            // is sorted so this is the next best PMTU estimate.
            return Some(pmtu);
        }
    }

    None
}

#[cfg(test)]
#[macro_use]
pub(crate) mod testutil {
    use alloc::vec::Vec;

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

    use crate::{
        assert_empty,
        context::{
            testutil::{DummyInstant, DummyTimerCtxExt},
            InstantContext,
        },
        testutil::TestIpExt,
    };

    type DummyCtx<I> = crate::context::testutil::DummyTimerCtx<PmtuTimerId<I>>;

    /// Get an IPv4 or IPv6 address within the same subnet as that of
    /// `DUMMY_CONFIG_*`, but with the last octet set to `3`.
    fn get_other_ip_address<I: TestIpExt>() -> SpecifiedAddr<I::Addr> {
        I::get_other_ip_address(3)
    }

    impl<I: Ip, Instant: Clone> PmtuCache<I, Instant> {
        /// Gets the last updated [`Instant`] when the PMTU between `src_ip` and
        /// `dst_ip` was updated.
        ///
        /// [`Instant`]: crate::Instant
        fn get_last_updated(&self, src_ip: I::Addr, dst_ip: I::Addr) -> Option<Instant> {
            self.cache.get(&PmtuCacheKey::new(src_ip, dst_ip)).map(|x| x.last_updated.clone())
        }
    }

    /// Constructs a closure which captures `cache` and can be passed
    /// to `DummyTimerCtx::trigger_timers_for` and friends.
    fn get_timer_handler<I: Ip, Instant: crate::Instant, C: PmtuContext<I, Instant = Instant>>(
        cache: &mut PmtuCache<I, Instant>,
    ) -> impl FnMut(&mut C, PmtuTimerId<I>) + '_ {
        move |ctx, id| cache.handle_timer(ctx, id)
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
    fn test_ip_path_mtu_cache_ctx<I: Ip + TestIpExt>() {
        let dummy_config = I::DUMMY_CONFIG;
        let mut ctx = DummyCtx::<I>::default();
        let mut cache = PmtuCache::default();

        // Nothing in the cache yet
        assert_eq!(cache.get_pmtu(dummy_config.local_ip.get(), dummy_config.remote_ip.get()), None);
        assert_eq!(
            cache.get_last_updated(dummy_config.local_ip.get(), dummy_config.remote_ip.get()),
            None
        );

        let new_mtu1 = u32::from(I::MINIMUM_LINK_MTU) + 50;
        let start_time = ctx.now();
        let duration = Duration::from_secs(1);

        // Advance time to 1s.
        assert_empty(ctx.trigger_timers_for(duration, get_timer_handler(&mut cache)));

        // Update pmtu from local to remote. PMTU should be updated to
        // `new_mtu1` and last updated instant should be updated to the start of
        // the test + 1s.
        assert_eq!(
            cache
                .update_pmtu(
                    &mut ctx,
                    dummy_config.local_ip.get(),
                    dummy_config.remote_ip.get(),
                    new_mtu1
                )
                .unwrap(),
            None
        );

        // Advance time to 2s.
        assert_empty(ctx.trigger_timers_for(duration, get_timer_handler(&mut cache)));

        // Make sure the update worked. PMTU should be updated to `new_mtu1` and
        // last updated instant should be updated to the start of the test + 1s
        // (when the update occurred.
        assert_eq!(
            cache.get_pmtu(dummy_config.local_ip.get(), dummy_config.remote_ip.get()).unwrap(),
            new_mtu1
        );
        assert_eq!(
            cache
                .get_last_updated(dummy_config.local_ip.get(), dummy_config.remote_ip.get())
                .unwrap(),
            start_time + duration
        );

        let new_mtu2 = u32::from(I::MINIMUM_LINK_MTU) + 100;

        // Advance time to 3s.
        assert_empty(ctx.trigger_timers_for(duration, get_timer_handler(&mut cache)));

        // Updating again should return the last pmtu PMTU should be updated to
        // `new_mtu2` and last updated instant should be updated to the start of
        // the test + 3s.
        assert_eq!(
            cache
                .update_pmtu(
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
        assert_empty(ctx.trigger_timers_for(duration, get_timer_handler(&mut cache)));

        // Make sure the update worked. PMTU should be updated to `new_mtu2` and
        // last updated instant should be updated to the start of the test + 3s
        // (when the update occurred).
        assert_eq!(
            cache.get_pmtu(dummy_config.local_ip.get(), dummy_config.remote_ip.get()).unwrap(),
            new_mtu2
        );
        assert_eq!(
            cache
                .get_last_updated(dummy_config.local_ip.get(), dummy_config.remote_ip.get())
                .unwrap(),
            start_time + (duration * 3)
        );

        let new_mtu3 = new_mtu2 - 10;

        // Advance time to 5s.
        assert_empty(ctx.trigger_timers_for(duration, get_timer_handler(&mut cache)));

        // Make sure update only if new PMTU is less than current (it is). PMTU
        // should be updated to `new_mtu3` and last updated instant should be
        // updated to the start of the test + 5s.
        assert_eq!(
            cache
                .update_pmtu_if_less(
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
        assert_empty(ctx.trigger_timers_for(duration, get_timer_handler(&mut cache)));

        // Make sure the update worked. PMTU should be updated to `new_mtu3` and
        // last updated instant should be updated to the start of the test + 5s
        // (when the update occurred).
        assert_eq!(
            cache.get_pmtu(dummy_config.local_ip.get(), dummy_config.remote_ip.get()).unwrap(),
            new_mtu3
        );
        let last_updated = start_time + (duration * 5);
        assert_eq!(
            cache
                .get_last_updated(dummy_config.local_ip.get(), dummy_config.remote_ip.get())
                .unwrap(),
            last_updated
        );

        let new_mtu4 = new_mtu3 + 50;

        // Advance time to 7s.
        assert_empty(ctx.trigger_timers_for(duration, get_timer_handler(&mut cache)));

        // Make sure update only if new PMTU is less than current (it isn't)
        assert_eq!(
            cache
                .update_pmtu_if_less(
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
        assert_empty(ctx.trigger_timers_for(duration, get_timer_handler(&mut cache)));

        // Make sure the update didn't work. PMTU and last updated should not
        // have changed.
        assert_eq!(
            cache.get_pmtu(dummy_config.local_ip.get(), dummy_config.remote_ip.get()).unwrap(),
            new_mtu3
        );
        assert_eq!(
            cache
                .get_last_updated(dummy_config.local_ip.get(), dummy_config.remote_ip.get())
                .unwrap(),
            last_updated
        );

        let low_mtu = u32::from(I::MINIMUM_LINK_MTU) - 1;

        // Advance time to 9s.
        assert_empty(ctx.trigger_timers_for(duration, get_timer_handler(&mut cache)));

        // Updating with MTU value less than the minimum MTU should fail.
        assert_eq!(
            cache
                .update_pmtu_if_less(
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
        assert_empty(ctx.trigger_timers_for(duration, get_timer_handler(&mut cache)));

        // Make sure the update didn't work. PMTU and last updated should not
        // have changed.
        assert_eq!(
            cache.get_pmtu(dummy_config.local_ip.get(), dummy_config.remote_ip.get()).unwrap(),
            new_mtu3
        );
        assert_eq!(
            cache
                .get_last_updated(dummy_config.local_ip.get(), dummy_config.remote_ip.get())
                .unwrap(),
            last_updated
        );
    }

    #[ip_test]
    fn test_ip_pmtu_task<I: Ip + TestIpExt>() {
        let dummy_config = I::DUMMY_CONFIG;
        let mut ctx = DummyCtx::<I>::default();
        let mut cache = PmtuCache::default();

        // Make sure there are no timers.
        ctx.assert_no_timers_installed();

        let new_mtu1 = u32::from(I::MINIMUM_LINK_MTU) + 50;
        let start_time = ctx.now();
        let duration = Duration::from_secs(1);

        // Advance time to 1s.
        assert_empty(ctx.trigger_timers_for(duration, get_timer_handler(&mut cache)));

        // Update pmtu from local to remote. PMTU should be updated to
        // `new_mtu1` and last updated instant should be updated to the start of
        // the test + 1s.
        assert_eq!(
            cache
                .update_pmtu(
                    &mut ctx,
                    dummy_config.local_ip.get(),
                    dummy_config.remote_ip.get(),
                    new_mtu1
                )
                .unwrap(),
            None
        );

        // Make sure a task got scheduled.
        ctx.assert_timers_installed([(
            PmtuTimerId::default(),
            DummyInstant::from(MAINTENANCE_PERIOD + Duration::from_secs(1)),
        )]);

        // Advance time to 2s.
        assert_empty(ctx.trigger_timers_for(duration, get_timer_handler(&mut cache)));

        // Make sure the update worked. PMTU should be updated to `new_mtu1` and
        // last updated instant should be updated to the start of the test + 1s
        // (when the update occurred.
        assert_eq!(
            cache.get_pmtu(dummy_config.local_ip.get(), dummy_config.remote_ip.get()).unwrap(),
            new_mtu1
        );
        assert_eq!(
            cache
                .get_last_updated(dummy_config.local_ip.get(), dummy_config.remote_ip.get())
                .unwrap(),
            start_time + duration
        );

        // Advance time to 30mins.
        assert_empty(ctx.trigger_timers_for(duration * 1798, get_timer_handler(&mut cache)));

        // Update pmtu from local to another remote. PMTU should be updated to
        // `new_mtu1` and last updated instant should be updated to the start of
        // the test + 1s.
        let other_ip = get_other_ip_address::<I>();
        let new_mtu2 = u32::from(I::MINIMUM_LINK_MTU) + 100;
        assert_eq!(
            cache
                .update_pmtu(&mut ctx, dummy_config.local_ip.get(), other_ip.get(), new_mtu2)
                .unwrap(),
            None
        );

        // Make sure there is still a task scheduled. (we know no timers got
        // triggered because the `run_for` methods returned 0 so far).
        ctx.assert_timers_installed([(
            PmtuTimerId::default(),
            DummyInstant::from(MAINTENANCE_PERIOD + Duration::from_secs(1)),
        )]);

        // Make sure the update worked. PMTU should be updated to `new_mtu2` and
        // last updated instant should be updated to the start of the test +
        // 30mins + 2s (when the update occurred.
        assert_eq!(cache.get_pmtu(dummy_config.local_ip.get(), other_ip.get()).unwrap(), new_mtu2);
        assert_eq!(
            cache.get_last_updated(dummy_config.local_ip.get(), other_ip.get()).unwrap(),
            start_time + (duration * 1800)
        );
        // Make sure first update is still in the cache.
        assert_eq!(
            cache.get_pmtu(dummy_config.local_ip.get(), dummy_config.remote_ip.get()).unwrap(),
            new_mtu1
        );
        assert_eq!(
            cache
                .get_last_updated(dummy_config.local_ip.get(), dummy_config.remote_ip.get())
                .unwrap(),
            start_time + duration
        );

        // Advance time to 1hr + 1s. Should have triggered a timer.
        ctx.trigger_timers_for_and_expect(
            duration * 1801,
            [PmtuTimerId::default()],
            get_timer_handler(&mut cache),
        );
        // Make sure none of the cache data has been marked as stale and
        // removed.
        assert_eq!(
            cache.get_pmtu(dummy_config.local_ip.get(), dummy_config.remote_ip.get()).unwrap(),
            new_mtu1
        );
        assert_eq!(
            cache
                .get_last_updated(dummy_config.local_ip.get(), dummy_config.remote_ip.get())
                .unwrap(),
            start_time + duration
        );
        assert_eq!(cache.get_pmtu(dummy_config.local_ip.get(), other_ip.get()).unwrap(), new_mtu2);
        assert_eq!(
            cache.get_last_updated(dummy_config.local_ip.get(), other_ip.get()).unwrap(),
            start_time + (duration * 1800)
        );
        // Should still have another task scheduled.
        ctx.assert_timers_installed([(
            PmtuTimerId::default(),
            DummyInstant::from(MAINTENANCE_PERIOD * 2 + Duration::from_secs(1)),
        )]);

        // Advance time to 3hr + 1s. Should have triggered 2 timers.
        ctx.trigger_timers_for_and_expect(
            duration * 7200,
            [PmtuTimerId::default(), PmtuTimerId::default()],
            get_timer_handler(&mut cache),
        );
        // Make sure only the earlier PMTU data got marked as stale and removed.
        assert_eq!(cache.get_pmtu(dummy_config.local_ip.get(), dummy_config.remote_ip.get()), None);
        assert_eq!(
            cache.get_last_updated(dummy_config.local_ip.get(), dummy_config.remote_ip.get()),
            None
        );
        assert_eq!(cache.get_pmtu(dummy_config.local_ip.get(), other_ip.get()).unwrap(), new_mtu2);
        assert_eq!(
            cache.get_last_updated(dummy_config.local_ip.get(), other_ip.get()).unwrap(),
            start_time + (duration * 1800)
        );
        // Should still have another task scheduled.
        ctx.assert_timers_installed([(
            PmtuTimerId::default(),
            DummyInstant::from(MAINTENANCE_PERIOD * 4 + Duration::from_secs(1)),
        )]);

        // Advance time to 4hr + 1s. Should have triggered 1 timers.
        ctx.trigger_timers_for_and_expect(
            duration * 3600,
            [PmtuTimerId::default()],
            get_timer_handler(&mut cache),
        );
        // Make sure both PMTU data got marked as stale and removed.
        assert_eq!(cache.get_pmtu(dummy_config.local_ip.get(), dummy_config.remote_ip.get()), None);
        assert_eq!(
            cache.get_last_updated(dummy_config.local_ip.get(), dummy_config.remote_ip.get()),
            None
        );
        assert_eq!(cache.get_pmtu(dummy_config.local_ip.get(), other_ip.get()), None);
        assert_eq!(cache.get_last_updated(dummy_config.local_ip.get(), other_ip.get()), None);
        // Should not have a task scheduled since there is no more PMTU data.
        ctx.assert_no_timers_installed();
    }
}
