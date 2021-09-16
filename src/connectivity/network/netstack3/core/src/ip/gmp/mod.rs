// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Group Management Protocols (GMPs).
//!
//! This module provides implementations of the Internet Group Management Protocol
//! (IGMP) and the Multicast Listener Discovery (MLD) protocol. These allow
//! hosts to join IPv4 and IPv6 multicast groups respectively.
//!
//! The term "Group Management Protocol" is defined in [RFC 4606]:
//!
//! > Due to the commonality of function, the term "Group Management Protocol",
//! > or "GMP", will be used to refer to both IGMP and MLD.
//!
//! [RFC 4606]: https://tools.ietf.org/html/rfc4604

// This macro is used by tests in both the `igmp` and `mld` modules.

/// Assert that the GMP state machine for `$group` is in the given state.
///
/// `$ctx` is a `context::testutil::DummyCtx` whose state contains a `groups:
/// MulticastGroupSet` field.
#[cfg(test)]
macro_rules! assert_gmp_state {
    ($ctx:expr, $group:expr, Delaying) => {
        assert_gmp_state!(@inner $ctx, $group, MemberState::Delaying(_));
    };
    ($ctx:expr, $group:expr, Idle) => {
        assert_gmp_state!(@inner $ctx, $group, MemberState::Idle(_));
    };
    (@inner $ctx:expr, $group:expr, $pattern:pat) => {
        assert!(matches!($ctx.get_ref().groups.get($group).unwrap().0.inner.as_ref().unwrap(), $pattern))
    };
}

pub(crate) mod igmp;
pub(crate) mod mld;

use alloc::vec::{self, Vec};
use core::convert::TryFrom;
use core::time::Duration;

use net_types::ip::{Ip, IpAddress};
use net_types::MulticastAddr;
use rand::Rng;

use crate::data_structures::ref_counted_hash_map::{InsertResult, RefCountedHashMap, RemoveResult};
use crate::device::link::LinkDevice;
use crate::device::DeviceIdContext;
use crate::Instant;

/// The result of joining a multicast group.
///
/// `GroupJoinResult` is the result of joining a multicast group in a
/// [`MulticastGroupSet`].
#[cfg_attr(test, derive(Debug, Eq, PartialEq))]
pub(crate) enum GroupJoinResult<O = ()> {
    /// We were not previously a member of the group, so we joined the
    /// group.
    Joined(O),
    /// We were already a member of the group, so we incremented the group's
    /// reference count.
    AlreadyMember,
}

impl<O> GroupJoinResult<O> {
    /// Maps a [`GroupJoinResult::Joined`] variant to another type.
    ///
    /// If `self` is [`GroupJoinResult::AlreadyMember`], it is left as-is.
    pub(crate) fn map<P, F: FnOnce(O) -> P>(self, f: F) -> GroupJoinResult<P> {
        match self {
            GroupJoinResult::Joined(output) => GroupJoinResult::Joined(f(output)),
            GroupJoinResult::AlreadyMember => GroupJoinResult::AlreadyMember,
        }
    }
}

impl<O> From<InsertResult<O>> for GroupJoinResult<O> {
    fn from(result: InsertResult<O>) -> Self {
        match result {
            InsertResult::Inserted(output) => GroupJoinResult::Joined(output),
            InsertResult::AlreadyPresent => GroupJoinResult::AlreadyMember,
        }
    }
}

/// The result of leaving a multicast group.
///
/// `GroupLeaveResult` is the result of leaving a multicast group in
/// [`MulticastGroupSet`].
#[cfg_attr(test, derive(Debug, Eq, PartialEq))]
pub(crate) enum GroupLeaveResult<T = ()> {
    /// The reference count reached 0, so we left the group.
    Left(T),
    /// The reference count did not reach 0, so we are still a member of the
    /// group.
    StillMember,
    /// We were not a member of the group.
    NotMember,
}

impl<T> GroupLeaveResult<T> {
    /// Maps a [`GroupLeaveResult::Left`] variant to another type.
    ///
    /// If `self` is [`GroupLeaveResult::StillMember`] or
    /// [`GroupLeaveResult::NotMember`], it is left as-is.
    pub(crate) fn map<U, F: FnOnce(T) -> U>(self, f: F) -> GroupLeaveResult<U> {
        match self {
            GroupLeaveResult::Left(value) => GroupLeaveResult::Left(f(value)),
            GroupLeaveResult::StillMember => GroupLeaveResult::StillMember,
            GroupLeaveResult::NotMember => GroupLeaveResult::NotMember,
        }
    }
}

impl<T> From<RemoveResult<T>> for GroupLeaveResult<T> {
    fn from(result: RemoveResult<T>) -> Self {
        match result {
            RemoveResult::Removed(value) => GroupLeaveResult::Left(value),
            RemoveResult::StillPresent => GroupLeaveResult::StillMember,
            RemoveResult::NotPresent => GroupLeaveResult::NotMember,
        }
    }
}

/// A set of reference-counted multicast groups and associated data.
///
/// `MulticastGroupSet` is a set of multicast groups, each with associated data
/// `T`. Each group is reference-counted, only being removed once its reference
/// count reaches zero.
pub(crate) struct MulticastGroupSet<A: IpAddress, T> {
    inner: RefCountedHashMap<MulticastAddr<A>, T>,
}

impl<A: IpAddress, T> Default for MulticastGroupSet<A, T> {
    fn default() -> MulticastGroupSet<A, T> {
        MulticastGroupSet { inner: RefCountedHashMap::default() }
    }
}

impl<A: IpAddress, T> MulticastGroupSet<A, T> {
    fn join_group_with<O, F: FnOnce() -> (T, O)>(
        &mut self,
        group: MulticastAddr<A>,
        f: F,
    ) -> GroupJoinResult<O> {
        self.inner.insert_with(group, f).into()
    }

    /// Joins a multicast group and initializes it with a GMP state machine.
    ///
    /// `join_group_gmp` joins the multicast group `group`. If the group was not
    /// already joined, then a new instance of [`GmpStateMachine`] is generated
    /// using [`GmpStateMachine::join_group`], it is inserted with a reference
    /// count of 1, and the list of actions returned by `join_group` is
    /// returned. Otherwise, if the group was already joined, its reference
    /// count is incremented.
    fn join_group_gmp<I: Instant, P: ProtocolSpecific + Default, R: Rng>(
        &mut self,
        group: MulticastAddr<A>,
        rng: &mut R,
        now: I,
    ) -> GroupJoinResult<Actions<P>>
    where
        T: From<GmpStateMachine<I, P>>,
        P::Config: Default,
    {
        self.join_group_with(group, || {
            let (state, actions) = GmpStateMachine::join_group(rng, now);
            (T::from(state), actions)
        })
        .into()
    }

    fn leave_group(&mut self, group: MulticastAddr<A>) -> GroupLeaveResult<T> {
        self.inner.remove(group).into()
    }

    /// Leaves a multicast group.
    ///
    /// `leave_group_gmp` leaves the multicast group `group` by decrementing the
    /// reference count on the group. If the reference count reaches 0, the
    /// group is left using [`GmpStateMachine::leave_group`] and the list of
    /// actions returned by `leave_group` is returned.
    fn leave_group_gmp<I: Instant, P: ProtocolSpecific>(
        &mut self,
        group: MulticastAddr<A>,
    ) -> GroupLeaveResult<Actions<P>>
    where
        T: Into<GmpStateMachine<I, P>>,
    {
        self.leave_group(group).map(|state| state.into().leave_group()).into()
    }
}

impl<A: IpAddress, T> MulticastGroupSet<A, T> {
    /// Does the set contain the given group?
    pub(crate) fn contains(&self, group: &MulticastAddr<A>) -> bool {
        self.inner.contains_key(group)
    }

    #[cfg(test)]
    fn get(&self, group: &MulticastAddr<A>) -> Option<&T> {
        self.inner.get(group)
    }

    fn get_mut(&mut self, group: &MulticastAddr<A>) -> Option<&mut T> {
        self.inner.get_mut(group)
    }

    fn iter_mut<'a>(&'a mut self) -> impl 'a + Iterator<Item = (&'a MulticastAddr<A>, &'a mut T)> {
        self.inner.iter_mut()
    }
}

/// An implementation of a Group Management Protocol (GMP) such as the Internet
/// Group Management Protocol, Version 2 (IGMPv2) for IPv4 or the Multicast
/// Listener Discovery (MLD) protocol for IPv6.
pub(crate) trait GmpHandler<D: LinkDevice, I: Ip>: DeviceIdContext<D> {
    /// Joins the given multicast group.
    fn gmp_join_group(
        &mut self,
        device: Self::DeviceId,
        group_addr: MulticastAddr<I::Addr>,
    ) -> GroupJoinResult;

    /// Leaves the given multicast group.
    fn gmp_leave_group(
        &mut self,
        device: Self::DeviceId,
        group_addr: MulticastAddr<I::Addr>,
    ) -> GroupLeaveResult;
}

/// This trait is used to model the different parts of the two protocols.
///
/// Though MLD and IGMPv2 share the most part of their state machines there are
/// some subtle differences between each other.
trait ProtocolSpecific: Copy {
    /// The type for protocol-specific actions.
    type Action;
    /// The type for protocol-specific configs.
    type Config;

    /// The maximum delay to wait to send an unsolicited report.
    fn cfg_unsolicited_report_interval(cfg: &Self::Config) -> Duration;

    /// Whether the host should send a leave message even if it is not the last
    /// host in the group.
    fn cfg_send_leave_anyway(cfg: &Self::Config) -> bool;

    /// Get the _real_ `MAX_RESP_TIME`
    ///
    /// For IGMP messages, if the given duration is zero, it should be
    /// interpreted as 10 seconds. For MLD messages, this function is the
    /// identity function.
    fn get_max_resp_time(resp_time: Duration) -> Duration;

    /// Respond to a query in a protocol-specific way.
    ///
    /// When receiving a query, IGMPv2 needs to check whether the query is an
    /// IGMPv1 message and, if so, set a local "IGMPv1 Router Present" flag and
    /// set a timer. For MLD, this function is a no-op.
    fn do_query_received_specific(
        cfg: &Self::Config,
        actions: &mut Actions<Self>,
        max_resp_time: Duration,
        old: Self,
    ) -> Self;
}

/// This is used to represent the states that are common in both MLD and IGMPv2.
/// The state machine should behave as described on [RFC 2236 page 10] and [RFC
/// 2710 page 10].
///
/// [RFC 2236 page 10]: https://tools.ietf.org/html/rfc2236#page-10
/// [RFC 2710 page 10]: https://tools.ietf.org/html/rfc2710#page-10
struct GmpHostState<State, P: ProtocolSpecific> {
    state: State,
    /// `protocol_specific` are the value(s) you don't want the users to have a
    /// chance to modify. It is supposed to be only modified by the protocol
    /// itself.
    protocol_specific: P,
    /// `cfg` is used to store value(s) that is supposed to be modified by
    /// users.
    cfg: P::Config,
}

// Used to write tests in the `igmp` and `mld` modules.
#[cfg(test)]
impl<S, P: ProtocolSpecific> GmpHostState<S, P> {
    fn get_protocol_specific(&self) -> P {
        self.protocol_specific
    }

    fn get_state(&self) -> &S {
        &self.state
    }
}

/// Generic actions that will be used both in MLD and IGMPv2.
///
/// The terms are biased towards IGMPv2. `Leave` is called `Done` in MLD.
#[derive(Debug, PartialEq, Eq)]
enum GmpAction<P: ProtocolSpecific> {
    ScheduleReportTimer(Duration),
    StopReportTimer,
    SendLeave,
    SendReport(P),
}

/// The type to represent the actions generated by the state machine.
///
/// An action could either be a generic action as defined in `GmpAction` Or any
/// other protocol-specific action that is associated with `ProtocolSpecific`.
#[derive(Debug, PartialEq, Eq)]
enum Action<P: ProtocolSpecific> {
    Generic(GmpAction<P>),
    Specific(P::Action),
}

/// A collection of `Action`s.
// TODO: switch to ArrayVec if performance ever becomes a concern.
struct Actions<P: ProtocolSpecific>(Vec<Action<P>>);

impl<P: ProtocolSpecific> Actions<P> {
    /// An empty set of actions.
    const EMPTY: Actions<P> = Actions(Vec::new());

    /// Add a generic action to the set.
    fn push_generic(&mut self, action: GmpAction<P>) {
        self.0.push(Action::Generic(action));
    }

    /// Add a specific action to the set.
    fn push_specific(&mut self, action: P::Action) {
        self.0.push(Action::Specific(action));
    }
}

impl<P: ProtocolSpecific> IntoIterator for Actions<P> {
    type Item = Action<P>;
    type IntoIter = vec::IntoIter<Self::Item>;

    fn into_iter(self) -> Self::IntoIter {
        self.0.into_iter()
    }
}

/// Two of the three states that are common in both IGMPv2 and MLD.
///
/// Unlike the [IGMPv2] and [MLD] RFCs, a `MemberState` cannot be in the
/// Non-Member state. We can construct the Non-Member state ephemerally, but we
/// cannot store it in a `MemberState`.
///
/// This allows us to guarantee that the only way to construct a `MemberState`
/// is through the [`join_group`] constructor, which constructs an ephemeral
/// Non-Member state but then immediately executes the "join group" transition.
/// This, combined with the fact that `leave_group` consumes its `MemberState`
/// argument by value, allows us to guarantee that we only store `MemberState`s
/// for groups we have actually joined, and that `MemberState`s are immediately
/// removed when we leave a group.
///
/// As with [`GmpAction`], the terms used here are biased towards IGMPv2. In
/// MLD, their names are {Non, Delaying, Idle}-Listener instead.
///
/// [IGMPv2]: https://tools.ietf.org/html/rfc2236
/// [MLD]: https://tools.ietf.org/html/rfc2710
enum MemberState<I: Instant, P: ProtocolSpecific> {
    Delaying(GmpHostState<DelayingMember<I>, P>),
    Idle(GmpHostState<IdleMember, P>),
}

/// The transition between one state and the next.
///
/// A `Transition` includes the next state to enter and any actions to take
/// while executing the transition.
struct Transition<S, P: ProtocolSpecific>(GmpHostState<S, P>, Actions<P>);

/// Represents Non Member-specific state variables. Empty for now.
struct NonMember;

/// Represents Delaying Member-specific state variables.
struct DelayingMember<I: Instant> {
    /// The expiration time for the current timer. Useful to check if the timer
    /// needs to be reset when a query arrives.
    timer_expiration: I,

    /// Used to indicate whether we need to send out a Leave message when we are
    /// leaving the group. This flag will become false once we heard about
    /// another reporter.
    last_reporter: bool,
}

/// Represents Idle Member-specific state variables.
struct IdleMember {
    /// Used to indicate whether we need to send out a Leave message when we are
    /// leaving the group.
    last_reporter: bool,
}

impl<S, P: ProtocolSpecific> GmpHostState<S, P> {
    /// Construct a `Transition` from this state into the new state `T` with the
    /// given actions.
    fn transition<T>(self, t: T, actions: Actions<P>) -> Transition<T, P> {
        Transition(
            GmpHostState { state: t, protocol_specific: self.protocol_specific, cfg: self.cfg },
            actions,
        )
    }

    /// Construct a `Transition` from this state into the new state `T` with a
    /// given protocol-specific value and actions.
    fn transition_with_protocol_specific<T>(
        self,
        t: T,
        ps: P,
        actions: Actions<P>,
    ) -> Transition<T, P> {
        Transition(GmpHostState { state: t, protocol_specific: ps, cfg: self.cfg }, actions)
    }
}

/// Compute the new timer expiration with given inputs.
///
/// # Arguments
///
/// * `old` is `None` if there are currently no timers, otherwise `Some(t)`
///   where `t` is the old instant when the currently installed timer should
///   fire.
/// * `resp_time` is the maximum response time required by Query message.
/// * `now` is the current time.
/// * `actions` is a mutable reference to the actions being built.
///
/// # Return value
///
/// The computed new expiration time.
fn compute_timer_expiration<I: Instant, P: ProtocolSpecific, R: Rng>(
    rng: &mut R,
    old: Option<I>,
    resp_time: Duration,
    now: I,
    actions: &mut Actions<P>,
) -> I {
    let resp_time_deadline = P::get_max_resp_time(resp_time);
    if resp_time_deadline.as_millis() == 0 {
        now
    } else {
        let new_deadline = now.checked_add(resp_time_deadline).unwrap();
        let urgent = match old {
            Some(old) => new_deadline < old,
            None => true,
        };
        let delay = random_report_timeout(rng, resp_time_deadline);
        let timer_expiration = if urgent { now.checked_add(delay).unwrap() } else { old.unwrap() };
        if urgent {
            actions.push_generic(GmpAction::ScheduleReportTimer(delay));
        }
        timer_expiration
    }
}

/// Randomly generates a timeout in (0, period].
///
/// # Panics
///
/// `random_report_timeout` may panic if `period.as_micros()` overflows `u64`.
fn random_report_timeout<R: Rng>(rng: &mut R, period: Duration) -> Duration {
    let micros = rng.gen_range(0, u64::try_from(period.as_micros()).unwrap()) + 1;
    // u64 will be enough here because the only input of the function is from
    // the `MaxRespTime` field of the GMP query packets. The representable
    // number of microseconds is bounded by 2^33.
    Duration::from_micros(micros)
}

impl<P: ProtocolSpecific> GmpHostState<NonMember, P> {
    fn join_group<I: Instant, R: Rng>(
        self,
        rng: &mut R,
        now: I,
    ) -> Transition<DelayingMember<I>, P> {
        let duration = P::cfg_unsolicited_report_interval(&self.cfg);
        let delay = random_report_timeout(rng, duration);
        let mut actions = Actions::EMPTY;
        actions.push_generic(GmpAction::SendReport(self.protocol_specific));
        actions.push_generic(GmpAction::ScheduleReportTimer(delay));
        self.transition(
            DelayingMember {
                last_reporter: true,
                timer_expiration: now.checked_add(delay).expect("timer expiration overflowed"),
            },
            actions,
        )
    }
}

impl<I: Instant, P: ProtocolSpecific> GmpHostState<DelayingMember<I>, P> {
    fn query_received<R: Rng>(
        self,
        rng: &mut R,
        max_resp_time: Duration,
        now: I,
    ) -> Transition<DelayingMember<I>, P> {
        let mut actions = Actions::EMPTY;
        let last_reporter = self.state.last_reporter;
        let timer_expiration = compute_timer_expiration(
            rng,
            Some(self.state.timer_expiration),
            max_resp_time,
            now,
            &mut actions,
        );
        let new_ps = P::do_query_received_specific(
            &self.cfg,
            &mut actions,
            max_resp_time,
            self.protocol_specific,
        );
        self.transition_with_protocol_specific(
            DelayingMember { last_reporter, timer_expiration },
            new_ps,
            actions,
        )
    }

    fn leave_group(self) -> Transition<NonMember, P> {
        let mut actions = Actions::EMPTY;
        if self.state.last_reporter || P::cfg_send_leave_anyway(&self.cfg) {
            actions.push_generic(GmpAction::SendLeave);
        }
        actions.push_generic(GmpAction::StopReportTimer);
        self.transition(NonMember {}, actions)
    }

    fn report_received(self) -> Transition<IdleMember, P> {
        let mut actions = Actions::EMPTY;
        actions.push_generic(GmpAction::StopReportTimer);
        self.transition(IdleMember { last_reporter: false }, actions)
    }

    fn report_timer_expired(self) -> Transition<IdleMember, P> {
        let mut actions = Actions::EMPTY;
        actions.push_generic(GmpAction::SendReport(self.protocol_specific));
        self.transition(IdleMember { last_reporter: true }, actions)
    }
}

impl<P: ProtocolSpecific> GmpHostState<IdleMember, P> {
    fn query_received<I: Instant, R: Rng>(
        self,
        rng: &mut R,
        max_resp_time: Duration,
        now: I,
    ) -> Transition<DelayingMember<I>, P> {
        let last_reporter = self.state.last_reporter;
        let mut actions = Actions::EMPTY;
        let timer_expiration =
            compute_timer_expiration(rng, None, max_resp_time, now, &mut actions);
        let new_ps = P::do_query_received_specific(
            &self.cfg,
            &mut actions,
            max_resp_time,
            self.protocol_specific,
        );
        self.transition_with_protocol_specific(
            DelayingMember { last_reporter, timer_expiration },
            new_ps,
            actions,
        )
    }

    fn leave_group(self) -> Transition<NonMember, P> {
        let mut actions = Actions::EMPTY;
        if self.state.last_reporter || P::cfg_send_leave_anyway(&self.cfg) {
            actions.push_generic(GmpAction::SendLeave);
        }
        self.transition(NonMember {}, actions)
    }
}

impl<I: Instant, P: ProtocolSpecific> From<GmpHostState<DelayingMember<I>, P>>
    for MemberState<I, P>
{
    fn from(s: GmpHostState<DelayingMember<I>, P>) -> Self {
        MemberState::Delaying(s)
    }
}

impl<I: Instant, P: ProtocolSpecific> From<GmpHostState<IdleMember, P>> for MemberState<I, P> {
    fn from(s: GmpHostState<IdleMember, P>) -> Self {
        MemberState::Idle(s)
    }
}

impl<S, P: ProtocolSpecific> Transition<S, P> {
    fn into_state_actions<I: Instant>(self) -> (MemberState<I, P>, Actions<P>)
    where
        MemberState<I, P>: From<GmpHostState<S, P>>,
    {
        (self.0.into(), self.1)
    }
}

impl<I: Instant, P: ProtocolSpecific> MemberState<I, P> {
    /// Performs the "join group" transition, producing a new `MemberState` and
    /// set of actions to execute.
    ///
    /// In the [IGMPv2] and [MLD] RFCs, the "join group" transition moves from
    /// the Non-Member state to the Delaying Member state. However, we don't
    /// allow `MemberState` to be in the Non-Member state, so we instead
    /// implement `join_group` as a constructor of a new state which starts off
    /// in the Delaying Member state. Since `join_group` is the only way to
    /// construct a `MemberState`, this ensures that we don't store a state for
    /// a group until we've joined that group.
    ///
    /// [IGMPv2]: https://tools.ietf.org/html/rfc2236
    /// [MLD]: https://tools.ietf.org/html/rfc2710
    fn join_group<R: Rng>(
        protocol_specific: P,
        cfg: P::Config,
        rng: &mut R,
        now: I,
    ) -> (MemberState<I, P>, Actions<P>) {
        GmpHostState { protocol_specific, cfg, state: NonMember }
            .join_group(rng, now)
            .into_state_actions()
    }

    /// Performs the "leave group" transition, consuming the state by value, and
    /// returning a set of actions to execute.
    ///
    /// In the [IGMPv2] and [MLD] RFCs, the "leave group" transition moves from
    /// any state to the Non-Member state. However, we don't allow `MemberState`
    /// to be in the Non-Member state, so we instead implement `leave_group` by
    /// consuming the state by value. This ensures that once a group has been
    /// left, we don't spuriously store state for it.
    ///
    /// [IGMPv2]: https://tools.ietf.org/html/rfc2236
    /// [MLD]: https://tools.ietf.org/html/rfc2710
    fn leave_group(self) -> Actions<P> {
        // Rust can infer these types, but since we're just discarding `_state`,
        // we explicitly make sure it's the state we expect in case we introduce
        // a bug.
        let Transition(_state, actions): Transition<NonMember, _> = match self {
            MemberState::Delaying(state) => state.leave_group(),
            MemberState::Idle(state) => state.leave_group(),
        };
        actions
    }

    fn query_received<R: Rng>(
        self,
        rng: &mut R,
        max_resp_time: Duration,
        now: I,
    ) -> (MemberState<I, P>, Actions<P>) {
        match self {
            MemberState::Delaying(state) => {
                state.query_received(rng, max_resp_time, now).into_state_actions()
            }
            MemberState::Idle(state) => {
                state.query_received(rng, max_resp_time, now).into_state_actions()
            }
        }
    }

    fn report_received(self) -> (MemberState<I, P>, Actions<P>) {
        match self {
            MemberState::Delaying(state) => state.report_received().into_state_actions(),
            state => (state, Actions::EMPTY),
        }
    }

    fn report_timer_expired(self) -> (MemberState<I, P>, Actions<P>) {
        match self {
            MemberState::Delaying(state) => state.report_timer_expired().into_state_actions(),
            state => (state, Actions::EMPTY),
        }
    }
}

struct GmpStateMachine<I: Instant, P: ProtocolSpecific> {
    // Invariant: `inner` is always `Some`. It is stored as an `Option` so that
    // methods can `.take()` the `MemberState` in order to perform transitions
    // that consume `MemberState` by value. However, a new `MemberState` is
    // always put back in its place so that `inner` is `Some` by the time the
    // methods return.
    inner: Option<MemberState<I, P>>,
}

impl<I: Instant, P: ProtocolSpecific + Default> GmpStateMachine<I, P>
where
    P::Config: Default,
{
    /// When a "join group" command is received.
    ///
    /// `join_group` initializes a new state machine in the Non-Member state and
    /// then immediately executes the "join group" transition. The new state
    /// machine is returned along with any actions to take.
    fn join_group<R: Rng>(rng: &mut R, now: I) -> (GmpStateMachine<I, P>, Actions<P>) {
        let (state, actions) =
            MemberState::join_group(P::default(), P::Config::default(), rng, now);
        (GmpStateMachine { inner: Some(state) }, actions)
    }
}

impl<I: Instant, P: ProtocolSpecific> GmpStateMachine<I, P> {
    /// When a "leave group" command is received.
    ///
    /// `leave_group` consumes the state machine by value since we don't allow
    /// storing a state machine in the Non-Member state.
    fn leave_group(self) -> Actions<P> {
        // This `unwrap` is safe because we maintain the invariant that `inner`
        // is always `Some`.
        self.inner.unwrap().leave_group()
    }

    /// When a query is received, and we have to respond within max_resp_time.
    fn query_received<R: Rng>(
        &mut self,
        rng: &mut R,
        max_resp_time: Duration,
        now: I,
    ) -> Actions<P> {
        self.update(|s| s.query_received(rng, max_resp_time, now))
    }

    /// We have received a report from another host on our local network.
    fn report_received(&mut self) -> Actions<P> {
        self.update(MemberState::report_received)
    }

    /// The timer installed has expired.
    fn report_timer_expired(&mut self) -> Actions<P> {
        self.update(MemberState::report_timer_expired)
    }

    /// Update the state with no argument.
    fn update<F: FnOnce(MemberState<I, P>) -> (MemberState<I, P>, Actions<P>)>(
        &mut self,
        f: F,
    ) -> Actions<P> {
        let (s, a) = f(self.inner.take().unwrap());
        self.inner = Some(s);
        a
    }

    /// Update the state with a new protocol-specific value.
    fn update_with_protocol_specific(&mut self, ps: P) -> Actions<P> {
        self.update(|s| {
            (
                match s {
                    MemberState::Delaying(GmpHostState { state, cfg, .. }) => {
                        MemberState::Delaying(GmpHostState { state, cfg, protocol_specific: ps })
                    }
                    MemberState::Idle(GmpHostState { state, cfg, .. }) => {
                        MemberState::Idle(GmpHostState { state, cfg, protocol_specific: ps })
                    }
                },
                Actions::EMPTY,
            )
        })
    }

    #[cfg(test)]
    fn get_inner(&self) -> &MemberState<I, P> {
        self.inner.as_ref().unwrap()
    }
}

#[cfg(test)]
mod test {
    use core::convert::Infallible as Never;
    use matches::assert_matches;

    use super::*;
    use crate::ip::gmp::{Action, GmpAction, MemberState};
    use crate::testutil::{new_rng, DummyInstant};

    const DEFAULT_UNSOLICITED_REPORT_INTERVAL: Duration = Duration::from_secs(10);

    /// Dummy `ProtocolSpecific` for test purposes.
    #[derive(PartialEq, Eq, Copy, Clone, Debug, Default)]
    struct DummyProtocolSpecific;

    impl ProtocolSpecific for DummyProtocolSpecific {
        /// Tests for generic state machine should not know anything about
        /// protocol specific actions.
        type Action = Never;

        /// Whether to send leave group message if our flag is not set.
        type Config = bool;

        fn cfg_unsolicited_report_interval(_cfg: &Self::Config) -> Duration {
            DEFAULT_UNSOLICITED_REPORT_INTERVAL
        }

        fn cfg_send_leave_anyway(cfg: &Self::Config) -> bool {
            *cfg
        }

        fn get_max_resp_time(resp_time: Duration) -> Duration {
            resp_time
        }

        fn do_query_received_specific(
            _cfg: &Self::Config,
            _actions: &mut Actions<Self>,
            _max_resp_time: Duration,
            old: Self,
        ) -> Self {
            old
        }
    }

    impl<P: ProtocolSpecific> GmpStateMachine<DummyInstant, P> {
        pub(crate) fn get_config_mut(&mut self) -> &mut P::Config {
            match self.inner.as_mut().unwrap() {
                MemberState::Delaying(s) => &mut s.cfg,
                MemberState::Idle(s) => &mut s.cfg,
            }
        }
    }

    type DummyGmpStateMachine = GmpStateMachine<DummyInstant, DummyProtocolSpecific>;

    #[test]
    fn test_gmp_state_non_member_to_delay_should_set_flag() {
        let (s, _actions) =
            DummyGmpStateMachine::join_group(&mut new_rng(0), DummyInstant::default());
        match s.get_inner() {
            MemberState::Delaying(s) => assert!(s.get_state().last_reporter),
            _ => panic!("Wrong State!"),
        }
    }

    #[test]
    fn test_gmp_state_non_member_to_delay_actions() {
        let (_state, Actions(actions)) =
            DummyGmpStateMachine::join_group(&mut new_rng(0), DummyInstant::default());
        assert_matches!(
            actions.as_slice(),
            [
                Action::Generic(GmpAction::SendReport(DummyProtocolSpecific)),
                Action::Generic(GmpAction::ScheduleReportTimer(d))
            ] if *d <= DEFAULT_UNSOLICITED_REPORT_INTERVAL
        );
    }

    #[test]
    fn test_gmp_state_delay_no_reset_timer() {
        let mut rng = new_rng(0);
        let (mut s, _actions) = DummyGmpStateMachine::join_group(&mut rng, DummyInstant::default());
        let Actions(actions) = s.query_received(
            &mut rng,
            DEFAULT_UNSOLICITED_REPORT_INTERVAL + Duration::from_secs(1),
            DummyInstant::default(),
        );
        assert_eq!(actions, []);
    }

    #[test]
    fn test_gmp_state_delay_reset_timer() {
        let mut rng = new_rng(0);
        let (mut s, _actions) = DummyGmpStateMachine::join_group(&mut rng, DummyInstant::default());
        let Actions(actions) =
            s.query_received(&mut rng, Duration::from_millis(1), DummyInstant::default());
        assert_eq!(
            actions,
            [Action::Generic(GmpAction::ScheduleReportTimer(Duration::from_micros(1)))]
        );
    }

    #[test]
    fn test_gmp_state_delay_to_idle_with_report_no_flag() {
        let (mut s, _actions) =
            DummyGmpStateMachine::join_group(&mut new_rng(0), DummyInstant::default());
        let Actions(actions) = s.report_received();
        assert_eq!(actions, [Action::Generic(GmpAction::StopReportTimer)]);
        match s.get_inner() {
            MemberState::Idle(s) => {
                assert!(!s.get_state().last_reporter);
            }
            _ => panic!("Wrong State!"),
        }
    }

    #[test]
    fn test_gmp_state_delay_to_idle_without_report_set_flag() {
        let (mut s, _actions) =
            DummyGmpStateMachine::join_group(&mut new_rng(0), DummyInstant::default());
        let Actions(actions) = s.report_timer_expired();
        assert_eq!(actions, [Action::Generic(GmpAction::SendReport(DummyProtocolSpecific))]);
        match s.get_inner() {
            MemberState::Idle(s) => {
                assert!(s.get_state().last_reporter);
            }
            _ => panic!("Wrong State!"),
        }
    }

    #[test]
    fn test_gmp_state_leave_should_send_leave() {
        let mut rng = new_rng(0);
        let (s, _actions) = DummyGmpStateMachine::join_group(&mut rng, DummyInstant::default());
        let Actions(actions) = s.leave_group();
        assert_eq!(
            actions,
            [Action::Generic(GmpAction::SendLeave), Action::Generic(GmpAction::StopReportTimer)]
        );
        let (mut s, _actions) = DummyGmpStateMachine::join_group(&mut rng, DummyInstant::default());
        let Actions(actions) = s.report_timer_expired();
        assert_eq!(actions, [Action::Generic(GmpAction::SendReport(DummyProtocolSpecific))]);
        let Actions(actions) = s.leave_group();
        assert_eq!(actions, [Action::Generic(GmpAction::SendLeave)]);
    }

    #[test]
    fn test_gmp_state_delay_to_other_states_should_stop_timer() {
        let mut rng = new_rng(0);
        let (s, _actions) = DummyGmpStateMachine::join_group(&mut rng, DummyInstant::default());
        let Actions(actions) = s.leave_group();
        assert_eq!(
            actions,
            [Action::Generic(GmpAction::SendLeave), Action::Generic(GmpAction::StopReportTimer)]
        );
        let (mut s, _actions) = DummyGmpStateMachine::join_group(&mut rng, DummyInstant::default());
        let Actions(actions) = s.report_received();
        assert_eq!(actions, [Action::Generic(GmpAction::StopReportTimer)]);
    }

    #[test]
    fn test_gmp_state_other_states_to_delay_should_start_timer() {
        let mut rng = new_rng(0);
        let (mut s, Actions(actions)) =
            DummyGmpStateMachine::join_group(&mut rng, DummyInstant::default());
        assert_matches!(
            actions.as_slice(),
            [
                Action::Generic(GmpAction::SendReport(DummyProtocolSpecific)),
                Action::Generic(GmpAction::ScheduleReportTimer(d))
            ] if *d <= DEFAULT_UNSOLICITED_REPORT_INTERVAL
        );
        let Actions(actions) = s.report_received();
        assert_eq!(actions, [Action::Generic(GmpAction::StopReportTimer)]);
        let Actions(actions) =
            s.query_received(&mut rng, Duration::from_secs(1), DummyInstant::default());
        assert_eq!(
            actions,
            [Action::Generic(GmpAction::ScheduleReportTimer(Duration::from_micros(1)))]
        );
    }

    #[test]
    fn test_gmp_state_leave_send_anyway_do_send() {
        let (mut s, _actions) =
            DummyGmpStateMachine::join_group(&mut new_rng(0), DummyInstant::default());
        *s.get_config_mut() = true;
        let Actions(actions) = s.report_received();
        assert_eq!(actions, [Action::Generic(GmpAction::StopReportTimer)]);
        match s.get_inner() {
            MemberState::Idle(s) => assert!(!s.get_state().last_reporter),
            _ => panic!("Wrong State!"),
        }
        let Actions(actions) = s.leave_group();
        assert_eq!(actions, [Action::Generic(GmpAction::SendLeave)]);
    }

    #[test]
    fn test_gmp_state_leave_not_the_last_do_nothing() {
        let (mut s, _actions) =
            DummyGmpStateMachine::join_group(&mut new_rng(0), DummyInstant::default());
        let Actions(actions) = s.report_received();
        assert_eq!(actions, [Action::Generic(GmpAction::StopReportTimer)]);
        let actions = s.leave_group();
        for _ in actions {
            panic!("there should be no actions at all");
        }
    }
}
