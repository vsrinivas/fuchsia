// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Module for IP level paths' maximum transmission unit (PMTU) size
//! cache support.

use alloc::collections::HashMap;
use core::time::Duration;

use derivative::Derivative;
use log::trace;
use net_types::ip::{Ip, IpAddress, IpVersionMarker};

use crate::context::{TimerContext, TimerHandler};

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
#[derive(Copy, Clone, Default, Debug, PartialEq, Eq, Hash)]
pub(crate) struct PmtuTimerId<I: Ip>(IpVersionMarker<I>);

/// The state context for the path MTU cache.
pub(super) trait PmtuStateContext<I: Ip, Instant> {
    /// Calls a function with a mutable reference to the PMTU cache.
    fn with_state_mut<F: FnOnce(&mut PmtuCache<I, Instant>)>(&mut self, cb: F);
}

/// The non-synchronized execution context for path MTU discovery.
trait PmtuNonSyncContext<I: Ip>: TimerContext<PmtuTimerId<I>> {}
impl<I: Ip, C: TimerContext<PmtuTimerId<I>>> PmtuNonSyncContext<I> for C {}

/// The execution context for path MTU discovery.
trait PmtuContext<I: Ip, C: PmtuNonSyncContext<I>>: PmtuStateContext<I, C::Instant> {}

impl<I: Ip, C: PmtuNonSyncContext<I>, SC: PmtuStateContext<I, C::Instant>> PmtuContext<I, C>
    for SC
{
}

/// A handler for incoming PMTU events.
///
/// `PmtuHandler` is intended to serve as the interface between ICMP the IP
/// layer, which holds the PMTU cache. In production, method calls are delegated
/// to a real [`PmtuCache`], while in testing, method calls may be delegated to
/// a dummy implementation.
pub(crate) trait PmtuHandler<I: Ip, C> {
    /// Updates the PMTU between `src_ip` and `dst_ip` if `new_mtu` is less than
    /// the current PMTU and does not violate the minimum MTU size requirements
    /// for an IP.
    fn update_pmtu_if_less(&mut self, ctx: &mut C, src_ip: I::Addr, dst_ip: I::Addr, new_mtu: u32);

    /// Updates the PMTU between `src_ip` and `dst_ip` to the next lower
    /// estimate from `from`.
    fn update_pmtu_next_lower(&mut self, ctx: &mut C, src_ip: I::Addr, dst_ip: I::Addr, from: u32);
}

fn maybe_schedule_timer<I: Ip, C: PmtuNonSyncContext<I>>(ctx: &mut C, cache_is_empty: bool) {
    // Only attempt to create the next maintenance task if we still have
    // PMTU entries in the cache. If we don't, it would be a waste to
    // schedule the timer. We will let the next creation of a PMTU entry
    // create the timer.
    if cache_is_empty {
        return;
    }

    let timer_id = PmtuTimerId::default();
    match ctx.scheduled_instant(timer_id) {
        Some(scheduled_at) => {
            let _: C::Instant = scheduled_at;
            // Timer already set, nothing to do.
        }
        None => {
            // We only enter this match arm if a timer was not already set.
            assert_eq!(ctx.schedule_timer(MAINTENANCE_PERIOD, timer_id), None)
        }
    }
}

fn handle_update_result<I: Ip, C: PmtuNonSyncContext<I>>(
    ctx: &mut C,
    result: Result<Option<u32>, Option<u32>>,
    cache_is_empty: bool,
) {
    // TODO(https://fxbug.dev/92599): Do something with this `Result`.
    let _: Result<_, _> = result.map(|ret| {
        maybe_schedule_timer(ctx, cache_is_empty);
        ret
    });
}

impl<I: Ip, C: PmtuNonSyncContext<I>, SC: PmtuContext<I, C>> PmtuHandler<I, C> for SC {
    fn update_pmtu_if_less(&mut self, ctx: &mut C, src_ip: I::Addr, dst_ip: I::Addr, new_mtu: u32) {
        self.with_state_mut(|cache| {
            let now = ctx.now();
            let res = cache.update_pmtu_if_less(src_ip, dst_ip, new_mtu, now);
            handle_update_result(ctx, res, cache.is_empty());
        })
    }

    fn update_pmtu_next_lower(&mut self, ctx: &mut C, src_ip: I::Addr, dst_ip: I::Addr, from: u32) {
        self.with_state_mut(|cache| {
            let now = ctx.now();
            let res = cache.update_pmtu_next_lower(src_ip, dst_ip, from, now);
            handle_update_result(ctx, res, cache.is_empty());
        })
    }
}

impl<I: Ip, C: PmtuNonSyncContext<I>, SC: PmtuContext<I, C>> TimerHandler<C, PmtuTimerId<I>>
    for SC
{
    fn handle_timer(&mut self, ctx: &mut C, _timer: PmtuTimerId<I>) {
        self.with_state_mut(|cache| {
            let now = ctx.now();
            cache.handle_timer(now);
            maybe_schedule_timer(ctx, cache.is_empty());
        })
    }
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
#[derive(Derivative)]
#[derivative(Default(bound = ""))]
pub(crate) struct PmtuCache<I: Ip, Instant> {
    cache: HashMap<PmtuCacheKey<I::Addr>, PmtuCacheData<Instant>>,
}

impl<I: Ip, Instant: crate::Instant> PmtuCache<I, Instant> {
    /// Gets the PMTU between `src_ip` and `dst_ip`.
    pub(crate) fn get_pmtu(&self, src_ip: I::Addr, dst_ip: I::Addr) -> Option<u32> {
        self.cache.get(&PmtuCacheKey::new(src_ip, dst_ip)).map(|x| x.pmtu)
    }

    /// Updates the PMTU between `src_ip` and `dst_ip` if `new_mtu` is less than
    /// the current PMTU and does not violate the minimum MTU size requirements
    /// for an IP.
    fn update_pmtu_if_less(
        &mut self,
        src_ip: I::Addr,
        dst_ip: I::Addr,
        new_mtu: u32,
        now: Instant,
    ) -> Result<Option<u32>, Option<u32>> {
        match self.get_pmtu(src_ip, dst_ip) {
            // No PMTU exists so update.
            None => self.update_pmtu(src_ip, dst_ip, new_mtu, now),
            // A PMTU exists but it is greater than `new_mtu` so update.
            Some(prev_mtu) if new_mtu < prev_mtu => self.update_pmtu(src_ip, dst_ip, new_mtu, now),
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
    fn update_pmtu_next_lower(
        &mut self,
        src_ip: I::Addr,
        dst_ip: I::Addr,
        from: u32,
        now: Instant,
    ) -> Result<Option<u32>, Option<u32>> {
        if let Some(next_pmtu) = next_lower_pmtu_plateau(from) {
            trace!(
                "update_pmtu_next_lower: Attempting to update PMTU between src {} and dest {} to {}",
                src_ip,
                dst_ip,
                next_pmtu
            );

            self.update_pmtu_if_less(src_ip, dst_ip, next_pmtu, now)
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
    fn update_pmtu(
        &mut self,
        src_ip: I::Addr,
        dst_ip: I::Addr,
        new_mtu: u32,
        now: Instant,
    ) -> Result<Option<u32>, Option<u32>> {
        // New MTU must not be smaller than the minimum MTU for an IP.
        if new_mtu < I::MINIMUM_LINK_MTU.into() {
            return Err(self.get_pmtu(src_ip, dst_ip));
        }

        Ok(self
            .cache
            .insert(PmtuCacheKey::new(src_ip, dst_ip), PmtuCacheData::new(new_mtu, now))
            .map(|PmtuCacheData { pmtu, last_updated: _ }| pmtu))
    }

    fn handle_timer(&mut self, now: Instant) {
        // Make sure we expected this timer to fire.
        assert!(!self.cache.is_empty());

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
            now.duration_since(v.last_updated) < PMTU_STALE_TIMEOUT
        });
    }

    fn is_empty(&self) -> bool {
        self.cache.is_empty()
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
        ($ty:ty, $ctx:ty, $ip_version:ident) => {
            impl PmtuHandler<net_types::ip::$ip_version, $ctx> for $ty {
                fn update_pmtu_if_less(
                    &mut self,
                    _ctx: &mut $ctx,
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
                    _ctx: &mut $ctx,
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

    use ip_test_macro::ip_test;
    use net_types::ip::{Ipv4, Ipv6};
    use net_types::{SpecifiedAddr, Witness};

    use crate::{
        context::{
            testutil::{
                handle_timer_helper_with_sc_ref_mut, DummyCtx, DummyInstant, DummySyncCtx,
                DummyTimerCtxExt,
            },
            InstantContext,
        },
        testutil::{assert_empty, TestIpExt},
    };

    #[derive(Default)]
    struct DummyPmtuContext<I: Ip> {
        cache: PmtuCache<I, DummyInstant>,
    }

    type MockCtx<I> = DummyCtx<DummyPmtuContext<I>, PmtuTimerId<I>, (), (), (), ()>;
    type MockSyncCtx<I> = DummySyncCtx<DummyPmtuContext<I>, (), ()>;

    impl<I: Ip> PmtuStateContext<I, DummyInstant> for MockSyncCtx<I> {
        fn with_state_mut<F: FnOnce(&mut PmtuCache<I, DummyInstant>)>(&mut self, cb: F) {
            cb(&mut self.get_mut().cache)
        }
    }

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

    fn get_pmtu<I: Ip>(ctx: &MockSyncCtx<I>, src_ip: I::Addr, dst_ip: I::Addr) -> Option<u32> {
        ctx.get_ref().cache.get_pmtu(src_ip, dst_ip)
    }

    fn get_last_updated<I: Ip>(
        ctx: &MockSyncCtx<I>,
        src_ip: I::Addr,
        dst_ip: I::Addr,
    ) -> Option<DummyInstant> {
        ctx.get_ref().cache.get_last_updated(src_ip, dst_ip)
    }

    #[ip_test]
    fn test_ip_path_mtu_cache_ctx<I: Ip + TestIpExt>() {
        let dummy_config = I::DUMMY_CONFIG;
        let MockCtx { mut sync_ctx, mut non_sync_ctx } = MockCtx::<I>::default();

        // Nothing in the cache yet
        assert_eq!(
            get_pmtu(&sync_ctx, dummy_config.local_ip.get(), dummy_config.remote_ip.get()),
            None
        );
        assert_eq!(
            get_last_updated(&sync_ctx, dummy_config.local_ip.get(), dummy_config.remote_ip.get()),
            None
        );

        let new_mtu1 = u32::from(I::MINIMUM_LINK_MTU) + 50;
        let start_time = non_sync_ctx.now();
        let duration = Duration::from_secs(1);

        // Advance time to 1s.
        assert_empty(non_sync_ctx.trigger_timers_for(
            duration,
            handle_timer_helper_with_sc_ref_mut(&mut sync_ctx, TimerHandler::handle_timer),
        ));

        // Update pmtu from local to remote. PMTU should be updated to
        // `new_mtu1` and last updated instant should be updated to the start of
        // the test + 1s.
        PmtuHandler::update_pmtu_if_less(
            &mut sync_ctx,
            &mut non_sync_ctx,
            dummy_config.local_ip.get(),
            dummy_config.remote_ip.get(),
            new_mtu1,
        );

        // Advance time to 2s.
        assert_empty(non_sync_ctx.trigger_timers_for(
            duration,
            handle_timer_helper_with_sc_ref_mut(&mut sync_ctx, TimerHandler::handle_timer),
        ));

        // Make sure the update worked. PMTU should be updated to `new_mtu1` and
        // last updated instant should be updated to the start of the test + 1s
        // (when the update occurred.
        assert_eq!(
            get_pmtu(&sync_ctx, dummy_config.local_ip.get(), dummy_config.remote_ip.get()).unwrap(),
            new_mtu1
        );
        assert_eq!(
            get_last_updated(&sync_ctx, dummy_config.local_ip.get(), dummy_config.remote_ip.get())
                .unwrap(),
            start_time + duration
        );

        let new_mtu2 = new_mtu1 - 1;

        // Advance time to 3s.
        assert_empty(non_sync_ctx.trigger_timers_for(
            duration,
            handle_timer_helper_with_sc_ref_mut(&mut sync_ctx, TimerHandler::handle_timer),
        ));

        // Updating again should return the last pmtu PMTU should be updated to
        // `new_mtu2` and last updated instant should be updated to the start of
        // the test + 3s.
        PmtuHandler::update_pmtu_if_less(
            &mut sync_ctx,
            &mut non_sync_ctx,
            dummy_config.local_ip.get(),
            dummy_config.remote_ip.get(),
            new_mtu2,
        );

        // Advance time to 4s.
        assert_empty(non_sync_ctx.trigger_timers_for(
            duration,
            handle_timer_helper_with_sc_ref_mut(&mut sync_ctx, TimerHandler::handle_timer),
        ));

        // Make sure the update worked. PMTU should be updated to `new_mtu2` and
        // last updated instant should be updated to the start of the test + 3s
        // (when the update occurred).
        assert_eq!(
            get_pmtu(&sync_ctx, dummy_config.local_ip.get(), dummy_config.remote_ip.get()).unwrap(),
            new_mtu2
        );
        assert_eq!(
            get_last_updated(&sync_ctx, dummy_config.local_ip.get(), dummy_config.remote_ip.get())
                .unwrap(),
            start_time + (duration * 3)
        );

        let new_mtu3 = new_mtu2 - 1;

        // Advance time to 5s.
        assert_empty(non_sync_ctx.trigger_timers_for(
            duration,
            handle_timer_helper_with_sc_ref_mut(&mut sync_ctx, TimerHandler::handle_timer),
        ));

        // Make sure update only if new PMTU is less than current (it is). PMTU
        // should be updated to `new_mtu3` and last updated instant should be
        // updated to the start of the test + 5s.
        PmtuHandler::update_pmtu_if_less(
            &mut sync_ctx,
            &mut non_sync_ctx,
            dummy_config.local_ip.get(),
            dummy_config.remote_ip.get(),
            new_mtu3,
        );

        // Advance time to 6s.
        assert_empty(non_sync_ctx.trigger_timers_for(
            duration,
            handle_timer_helper_with_sc_ref_mut(&mut sync_ctx, TimerHandler::handle_timer),
        ));

        // Make sure the update worked. PMTU should be updated to `new_mtu3` and
        // last updated instant should be updated to the start of the test + 5s
        // (when the update occurred).
        assert_eq!(
            get_pmtu(&sync_ctx, dummy_config.local_ip.get(), dummy_config.remote_ip.get()).unwrap(),
            new_mtu3
        );
        let last_updated = start_time + (duration * 5);
        assert_eq!(
            get_last_updated(&sync_ctx, dummy_config.local_ip.get(), dummy_config.remote_ip.get())
                .unwrap(),
            last_updated
        );

        let new_mtu4 = new_mtu3 + 50;

        // Advance time to 7s.
        assert_empty(non_sync_ctx.trigger_timers_for(
            duration,
            handle_timer_helper_with_sc_ref_mut(&mut sync_ctx, TimerHandler::handle_timer),
        ));

        // Make sure update only if new PMTU is less than current (it isn't)
        PmtuHandler::update_pmtu_if_less(
            &mut sync_ctx,
            &mut non_sync_ctx,
            dummy_config.local_ip.get(),
            dummy_config.remote_ip.get(),
            new_mtu4,
        );

        // Advance time to 8s.
        assert_empty(non_sync_ctx.trigger_timers_for(
            duration,
            handle_timer_helper_with_sc_ref_mut(&mut sync_ctx, TimerHandler::handle_timer),
        ));

        // Make sure the update didn't work. PMTU and last updated should not
        // have changed.
        assert_eq!(
            get_pmtu(&sync_ctx, dummy_config.local_ip.get(), dummy_config.remote_ip.get()).unwrap(),
            new_mtu3
        );
        assert_eq!(
            get_last_updated(&sync_ctx, dummy_config.local_ip.get(), dummy_config.remote_ip.get())
                .unwrap(),
            last_updated
        );

        let low_mtu = u32::from(I::MINIMUM_LINK_MTU) - 1;

        // Advance time to 9s.
        assert_empty(non_sync_ctx.trigger_timers_for(
            duration,
            handle_timer_helper_with_sc_ref_mut(&mut sync_ctx, TimerHandler::handle_timer),
        ));

        // Updating with MTU value less than the minimum MTU should fail.
        PmtuHandler::update_pmtu_if_less(
            &mut sync_ctx,
            &mut non_sync_ctx,
            dummy_config.local_ip.get(),
            dummy_config.remote_ip.get(),
            low_mtu,
        );

        // Advance time to 10s.
        assert_empty(non_sync_ctx.trigger_timers_for(
            duration,
            handle_timer_helper_with_sc_ref_mut(&mut sync_ctx, TimerHandler::handle_timer),
        ));

        // Make sure the update didn't work. PMTU and last updated should not
        // have changed.
        assert_eq!(
            get_pmtu(&sync_ctx, dummy_config.local_ip.get(), dummy_config.remote_ip.get()).unwrap(),
            new_mtu3
        );
        assert_eq!(
            get_last_updated(&sync_ctx, dummy_config.local_ip.get(), dummy_config.remote_ip.get())
                .unwrap(),
            last_updated
        );
    }

    #[ip_test]
    fn test_ip_pmtu_task<I: Ip + TestIpExt>() {
        let dummy_config = I::DUMMY_CONFIG;
        let MockCtx { mut sync_ctx, mut non_sync_ctx } = MockCtx::<I>::default();

        // Make sure there are no timers.
        non_sync_ctx.timer_ctx().assert_no_timers_installed();

        let new_mtu1 = u32::from(I::MINIMUM_LINK_MTU) + 50;
        let start_time = non_sync_ctx.now();
        let duration = Duration::from_secs(1);

        // Advance time to 1s.
        assert_empty(non_sync_ctx.trigger_timers_for(
            duration,
            handle_timer_helper_with_sc_ref_mut(&mut sync_ctx, TimerHandler::handle_timer),
        ));

        // Update pmtu from local to remote. PMTU should be updated to
        // `new_mtu1` and last updated instant should be updated to the start of
        // the test + 1s.
        PmtuHandler::update_pmtu_if_less(
            &mut sync_ctx,
            &mut non_sync_ctx,
            dummy_config.local_ip.get(),
            dummy_config.remote_ip.get(),
            new_mtu1,
        );

        // Make sure a task got scheduled.
        non_sync_ctx.timer_ctx().assert_timers_installed([(
            PmtuTimerId::default(),
            DummyInstant::from(MAINTENANCE_PERIOD + Duration::from_secs(1)),
        )]);

        // Advance time to 2s.
        assert_empty(non_sync_ctx.trigger_timers_for(
            duration,
            handle_timer_helper_with_sc_ref_mut(&mut sync_ctx, TimerHandler::handle_timer),
        ));

        // Make sure the update worked. PMTU should be updated to `new_mtu1` and
        // last updated instant should be updated to the start of the test + 1s
        // (when the update occurred.
        assert_eq!(
            get_pmtu(&sync_ctx, dummy_config.local_ip.get(), dummy_config.remote_ip.get()).unwrap(),
            new_mtu1
        );
        assert_eq!(
            get_last_updated(&sync_ctx, dummy_config.local_ip.get(), dummy_config.remote_ip.get())
                .unwrap(),
            start_time + duration
        );

        // Advance time to 30mins.
        assert_empty(non_sync_ctx.trigger_timers_for(
            duration * 1798,
            handle_timer_helper_with_sc_ref_mut(&mut sync_ctx, TimerHandler::handle_timer),
        ));

        // Update pmtu from local to another remote. PMTU should be updated to
        // `new_mtu1` and last updated instant should be updated to the start of
        // the test + 1s.
        let other_ip = get_other_ip_address::<I>();
        let new_mtu2 = u32::from(I::MINIMUM_LINK_MTU) + 100;
        PmtuHandler::update_pmtu_if_less(
            &mut sync_ctx,
            &mut non_sync_ctx,
            dummy_config.local_ip.get(),
            other_ip.get(),
            new_mtu2,
        );

        // Make sure there is still a task scheduled. (we know no timers got
        // triggered because the `run_for` methods returned 0 so far).
        non_sync_ctx.timer_ctx().assert_timers_installed([(
            PmtuTimerId::default(),
            DummyInstant::from(MAINTENANCE_PERIOD + Duration::from_secs(1)),
        )]);

        // Make sure the update worked. PMTU should be updated to `new_mtu2` and
        // last updated instant should be updated to the start of the test +
        // 30mins + 2s (when the update occurred.
        assert_eq!(
            get_pmtu(&sync_ctx, dummy_config.local_ip.get(), other_ip.get()).unwrap(),
            new_mtu2
        );
        assert_eq!(
            get_last_updated(&sync_ctx, dummy_config.local_ip.get(), other_ip.get()).unwrap(),
            start_time + (duration * 1800)
        );
        // Make sure first update is still in the cache.
        assert_eq!(
            get_pmtu(&sync_ctx, dummy_config.local_ip.get(), dummy_config.remote_ip.get()).unwrap(),
            new_mtu1
        );
        assert_eq!(
            get_last_updated(&sync_ctx, dummy_config.local_ip.get(), dummy_config.remote_ip.get())
                .unwrap(),
            start_time + duration
        );

        // Advance time to 1hr + 1s. Should have triggered a timer.
        non_sync_ctx.trigger_timers_for_and_expect(
            duration * 1801,
            [PmtuTimerId::default()],
            handle_timer_helper_with_sc_ref_mut(&mut sync_ctx, TimerHandler::handle_timer),
        );
        // Make sure none of the cache data has been marked as stale and
        // removed.
        assert_eq!(
            get_pmtu(&sync_ctx, dummy_config.local_ip.get(), dummy_config.remote_ip.get()).unwrap(),
            new_mtu1
        );
        assert_eq!(
            get_last_updated(&sync_ctx, dummy_config.local_ip.get(), dummy_config.remote_ip.get())
                .unwrap(),
            start_time + duration
        );
        assert_eq!(
            get_pmtu(&sync_ctx, dummy_config.local_ip.get(), other_ip.get()).unwrap(),
            new_mtu2
        );
        assert_eq!(
            get_last_updated(&sync_ctx, dummy_config.local_ip.get(), other_ip.get()).unwrap(),
            start_time + (duration * 1800)
        );
        // Should still have another task scheduled.
        non_sync_ctx.timer_ctx().assert_timers_installed([(
            PmtuTimerId::default(),
            DummyInstant::from(MAINTENANCE_PERIOD * 2 + Duration::from_secs(1)),
        )]);

        // Advance time to 3hr + 1s. Should have triggered 2 timers.
        non_sync_ctx.trigger_timers_for_and_expect(
            duration * 7200,
            [PmtuTimerId::default(), PmtuTimerId::default()],
            handle_timer_helper_with_sc_ref_mut(&mut sync_ctx, TimerHandler::handle_timer),
        );
        // Make sure only the earlier PMTU data got marked as stale and removed.
        assert_eq!(
            get_pmtu(&sync_ctx, dummy_config.local_ip.get(), dummy_config.remote_ip.get()),
            None
        );
        assert_eq!(
            get_last_updated(&sync_ctx, dummy_config.local_ip.get(), dummy_config.remote_ip.get()),
            None
        );
        assert_eq!(
            get_pmtu(&sync_ctx, dummy_config.local_ip.get(), other_ip.get()).unwrap(),
            new_mtu2
        );
        assert_eq!(
            get_last_updated(&sync_ctx, dummy_config.local_ip.get(), other_ip.get()).unwrap(),
            start_time + (duration * 1800)
        );
        // Should still have another task scheduled.
        non_sync_ctx.timer_ctx().assert_timers_installed([(
            PmtuTimerId::default(),
            DummyInstant::from(MAINTENANCE_PERIOD * 4 + Duration::from_secs(1)),
        )]);

        // Advance time to 4hr + 1s. Should have triggered 1 timers.
        non_sync_ctx.trigger_timers_for_and_expect(
            duration * 3600,
            [PmtuTimerId::default()],
            handle_timer_helper_with_sc_ref_mut(&mut sync_ctx, TimerHandler::handle_timer),
        );
        // Make sure both PMTU data got marked as stale and removed.
        assert_eq!(
            get_pmtu(&sync_ctx, dummy_config.local_ip.get(), dummy_config.remote_ip.get()),
            None
        );
        assert_eq!(
            get_last_updated(&sync_ctx, dummy_config.local_ip.get(), dummy_config.remote_ip.get()),
            None
        );
        assert_eq!(get_pmtu(&sync_ctx, dummy_config.local_ip.get(), other_ip.get()), None);
        assert_eq!(get_last_updated(&sync_ctx, dummy_config.local_ip.get(), other_ip.get()), None);
        // Should not have a task scheduled since there is no more PMTU data.
        non_sync_ctx.timer_ctx().assert_no_timers_installed();
    }
}
