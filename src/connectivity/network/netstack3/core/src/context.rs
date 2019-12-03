// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Execution contexts.
//!
//! This module defines "context" traits, which allow code in this crate to be
//! written agnostic to their execution context.
//!
//! All of the code in this crate operates in terms of "events". When an event
//! occurs (for example, a packet is received, an application makes a request,
//! or a timer fires), a function is called to handle that event. In response to
//! that event, the code may wish to emit new events (for example, to send a
//! packet, to respond to an application request, or to install a new timer).
//! The traits in this module provide the ability to emit new events. For
//! example, if, in order to handle some event, we need the ability to install
//! new timers, then the function to handle that event would take a
//! [`TimerContext`] parameter, which it could use to install new timers.
//!
//! Structuring code this way allows us to write code which is agnostic to
//! execution context - a test mock or any number of possible "real-world"
//! implementations of these traits all appear as indistinguishable, opaque
//! trait implementations to our code.
//!
//! The benefits are deeper than this, though. Large units of code can be
//! subdivided into smaller units that view each other as "contexts". For
//! example, the ARP implementation in the [`crate::device::arp`] module defines
//! the [`ArpContext`] trait, which is an execution context for ARP operations.
//! It is implemented both by the test mocks in that module, and also by the
//! Ethernet device implementation in the [`crate::device::ethernet`] module.
//!
//! This subdivision of code into small units in turn enables modularity. If,
//! for example, the IP code sees transport layer protocols as execution
//! contexts, then customizing which transport layer protocols are supported is
//! just a matter of providing a different implementation of the transport layer
//! context traits (this isn't what we do today, but we may in the future).
//!
//! [`ArpContext`]: crate::device::arp::ArpContext

use core::time::Duration;

use byteorder::{ByteOrder, NativeEndian};
use packet::{BufferMut, Serializer};
use rand::{CryptoRng, Rng, RngCore, SeedableRng};
use rand_xorshift::XorShiftRng;

use crate::{Context, EventDispatcher, Instant};

/// A context that provides access to a monotonic clock.
pub(crate) trait InstantContext {
    /// The type of an instant in time.
    ///
    /// All time is measured using `Instant`s, including scheduling timers
    /// through [`TimerContext`]. This type may represent some sort of
    /// real-world time (e.g., [`std::time::Instant`]), or may be mocked in
    /// testing using a fake clock.
    type Instant: Instant;

    /// Returns the current instant.
    ///
    /// `now` guarantees that two subsequent calls to `now` will return
    /// monotonically non-decreasing values.
    fn now(&self) -> Self::Instant;
}

// Temporary blanket impl until we switch over entirely to the traits defined in
// this module.
impl<D: EventDispatcher> InstantContext for Context<D> {
    type Instant = <D as EventDispatcher>::Instant;

    fn now(&self) -> Self::Instant {
        self.dispatcher().now()
    }
}

/// An [`InstantContext`] which stores a cached value for the current time.
///
/// `CachedInstantContext`s are constructed via [`new_cached_instant_context`].
pub(crate) struct CachedInstantContext<I>(I);

impl<I: Instant> InstantContext for CachedInstantContext<I> {
    type Instant = I;
    fn now(&self) -> I {
        self.0.clone()
    }
}

/// Construct a new `CachedInstantContext` from the current time.
///
/// This is a hack until we figure out a strategy for splitting context objects.
/// Currently, since most context methods take a `&mut self` argument, lifetimes
/// which don't need to conflict in principle - such as the lifetime of state
/// obtained mutably from [`StateContext`] and the lifetime required to call the
/// [`InstantContext::now`] method on the same object - do conflict, and thus
/// cannot overlap. Until we figure out an approach to deal with that problem,
/// this exists as a workaround.
pub(crate) fn new_cached_instant_context<I: InstantContext + ?Sized>(
    ctx: &I,
) -> CachedInstantContext<I::Instant> {
    CachedInstantContext(ctx.now())
}

/// A context that supports scheduling timers.
pub(crate) trait TimerContext<Id>: InstantContext {
    /// Schedule a timer to fire after some duration.
    ///
    /// `schedule_timer` schedules the given timer to be fired after `duration`
    /// has elapsed, overwriting any previous timer with the same ID.
    ///
    /// If there was previously a timer with that ID, return the time at which
    /// is was scheduled to fire.
    ///
    /// # Panics
    ///
    /// `schedule_timer` may panic if `duration` is large enough that
    /// `self.now() + duration` overflows.
    fn schedule_timer(&mut self, duration: Duration, id: Id) -> Option<Self::Instant> {
        self.schedule_timer_instant(self.now().checked_add(duration).unwrap(), id)
    }

    /// Schedule a timer to fire at some point in the future.
    ///
    /// `schedule_timer` schedules the given timer to be fired at `time`,
    /// overwriting any previous timer with the same ID.
    ///
    /// If there was previously a timer with that ID, return the time at which
    /// is was scheduled to fire.
    fn schedule_timer_instant(&mut self, time: Self::Instant, id: Id) -> Option<Self::Instant>;

    /// Cancel a timer.
    ///
    /// If a timer with the given ID exists, it is canceled and the instant at
    /// which it was scheduled to fire is returned.
    fn cancel_timer(&mut self, id: Id) -> Option<Self::Instant>;

    /// Cancel all timers which satisfy a predicate.
    ///
    /// `cancel_timers_with` calls `f` on each scheduled timer, and cancels any
    /// timer for which `f` returns true.
    fn cancel_timers_with<F: FnMut(&Id) -> bool>(&mut self, f: F);

    /// Get the instant a timer will fire, if one is scheduled.
    ///
    /// Returns the [`Instant`] a timer with ID `id` will be invoked. If no timer
    /// with the given ID exists, `scheduled_instant` will return `None`.
    fn scheduled_instant(&self, id: Id) -> Option<Self::Instant>;
}

/// A handler for timer firing events.
///
/// A `TimerHandler` is a type capable of handling the event of a timer firing.
pub(crate) trait TimerHandler<Ctx, Id> {
    /// Handle a timer firing.
    fn handle_timer(ctx: &mut Ctx, id: Id);
}

// NOTE:
// - Code in this crate is required to only obtain random values through an
//   `RngContext`. This allows a deterministic RNG to be provided when useful
//   (for example, in tests).
// - The CSPRNG requirement exists so that random values produced within the
//   network stack are not predictable by outside observers. This helps prevent
//   certain kinds of fingerprinting and denial of service attacks.

/// A context that provides a random number generator.
pub trait RngContext {
    // TODO(joshlf): If the CSPRNG requirement becomes a performance problem,
    // introduce a second, non-cryptographically secure, RNG.

    /// The random number generator (RNG) provided by this `RngContext`.
    ///
    /// The provided RNG must be cryptographically secure, and users may rely on
    /// that property for their correctness and security.
    type Rng: RngCore + CryptoRng;

    /// Get the random number generator (RNG).
    fn rng(&mut self) -> &mut Self::Rng;
}

pub(crate) trait RngContextExt: RngContext {
    /// Seed a new `XorShiftRng` from this [`RngContext`]'s RNG.
    ///
    /// This is a hack until we figure out a strategy for splitting context
    /// objects. Currently, since most context methods take a `&mut self`
    /// argument, lifetimes which don't need to conflict in principle - such as
    /// the lifetime of an RNG from [`RngContext`] and a state from
    /// [`StateContext`] - do conflict, and thus cannot overlap. Until we figure
    /// out an approach to deal with that problem, this exists as a workaround.
    fn new_xorshift_rng(&mut self) -> XorShiftRng {
        let mut seed: u64 = self.rng().gen();
        if seed == 0 {
            // XorShiftRng can't take 0 seeds
            seed = 1;
        }
        let mut bytes = [0; 16];
        NativeEndian::write_u32(&mut bytes[0..4], seed as u32);
        NativeEndian::write_u32(&mut bytes[4..8], (seed >> 32) as u32);
        NativeEndian::write_u32(&mut bytes[8..12], seed as u32);
        NativeEndian::write_u32(&mut bytes[12..16], (seed >> 32) as u32);
        XorShiftRng::from_seed(bytes)
    }
}

impl<C: RngContext> RngContextExt for C {}

// Temporary blanket impl until we switch over entirely to the traits defined in
// this module.
impl<D: EventDispatcher> RngContext for Context<D> {
    type Rng = D::Rng;

    fn rng(&mut self) -> &mut D::Rng {
        self.dispatcher_mut().rng()
    }
}

/// A context that provides access to state.
///
/// `StateContext` stores instances of `State` keyed by `Id`, and provides
/// getters for this state. If `Id` is `()`, then `StateContext` represents a
/// single instance of `State`.
pub trait StateContext<State, Id = ()> {
    /// Get the state immutably.
    fn get_state_with(&self, id: Id) -> &State;

    /// Get the state mutably.
    fn get_state_mut_with(&mut self, id: Id) -> &mut State;

    // TODO(joshlf): Change the `where` bounds in `get_state` and
    // `get_state_mut` to `where Id = ()` when equality bounds are supported.

    /// Get the state immutably when the `Id` type is `()`.
    ///
    /// `x.get_state()` is shorthand for `x.get_state_with(())`.
    fn get_state(&self) -> &State
    where
        Self: StateContext<State, ()>,
    {
        self.get_state_with(())
    }

    /// Get the state mutably when the `Id` type is `()`.
    ///
    /// `x.get_state_mut()` is shorthand for `x.get_state_mut_with(())`.
    fn get_state_mut(&mut self) -> &mut State
    where
        Self: StateContext<State, ()>,
    {
        self.get_state_mut_with(())
    }
}

/// A context for receiving frames.
pub trait RecvFrameContext<B: BufferMut, Meta> {
    /// Receive a frame.
    ///
    /// `receive_frame` receives a frame with the given metadata.
    fn receive_frame(&mut self, metadata: Meta, frame: B);
}

// TODO(joshlf): Rename `FrameContext` to `SendFrameContext`

/// A context for sending frames.
pub trait FrameContext<B: BufferMut, Meta> {
    // TODO(joshlf): Add an error type parameter or associated type once we need
    // different kinds of errors.

    /// Send a frame.
    ///
    /// `send_frame` sends a frame with the given metadata. The frame itself is
    /// passed as a [`Serializer`] which `send_frame` is responsible for
    /// serializing. If serialization fails for any reason, the original,
    /// unmodified `Serializer` is returned.
    ///
    /// [`Serializer`]: packet::Serializer
    fn send_frame<S: Serializer<Buffer = B>>(&mut self, metadata: Meta, frame: S) -> Result<(), S>;
}

/// A handler for frame events.
///
/// A `FrameHandler` is a type capable of handling the event of a frame being
/// received.
pub(crate) trait FrameHandler<Ctx, Id, Meta, B> {
    /// Handle a frame being received.
    fn handle_frame(ctx: &mut Ctx, id: Id, meta: Meta, buffer: B);
}

/// A context that stores performance counters.
///
/// `CounterContext` allows counters keyed by string names to be incremented for
/// testing and debugging purposes. It is assumed that, if a no-op
/// implementation of [`increment_counter`] is provided, then calls will be
/// optimized out entirely by the compiler.
pub(crate) trait CounterContext {
    /// Increment the counter with the given key.
    fn increment_counter(&mut self, key: &'static str);
}

// Temporary blanket impl until we switch over entirely to the traits defined in
// this module.
impl<D: EventDispatcher> CounterContext for Context<D> {
    // TODO(rheacock): This is tricky because it's used in test only macro
    // code so the compiler thinks `key` is unused. Remove this when this is
    // no longer a problem.
    #[allow(unused)]
    fn increment_counter(&mut self, key: &'static str) {
        increment_counter!(self, key);
    }
}

/// Mock implementations of context traits.
///
/// Each trait `Xxx` has a mock called `DummyXxx`. `DummyXxx` implements `Xxx`,
/// and `impl<T> DummyXxx for T` where either `T: AsRef<DummyXxx>` or `T:
/// AsMut<DummyXxx>` or both (depending on the trait). This allows dummy
/// implementations to be composed easily - any container type need only provide
/// the appropriate `AsRef` and/or `AsMut` implementations, and the blanket impl
/// will take care of the rest.
#[cfg(test)]
pub(crate) mod testutil {
    use std::collections::{BinaryHeap, HashMap};
    use std::fmt::{self, Debug, Formatter};
    use std::hash::Hash;
    use std::ops;
    use std::time::Duration;

    use packet::Buf;
    use rand_xorshift::XorShiftRng;

    use super::*;
    use crate::testutil::FakeCryptoRng;
    use crate::Instant;

    /// A dummy implementation of `Instant` for use in testing.
    #[derive(Default, Copy, Clone, Eq, PartialEq, Ord, PartialOrd)]
    pub(crate) struct DummyInstant {
        // A DummyInstant is just an offset from some arbitrary epoch.
        offset: Duration,
    }

    impl From<Duration> for DummyInstant {
        fn from(offset: Duration) -> DummyInstant {
            DummyInstant { offset }
        }
    }

    impl Instant for DummyInstant {
        fn duration_since(&self, earlier: DummyInstant) -> Duration {
            self.offset.checked_sub(earlier.offset).unwrap()
        }

        fn checked_add(&self, duration: Duration) -> Option<DummyInstant> {
            self.offset.checked_add(duration).map(|offset| DummyInstant { offset })
        }

        fn checked_sub(&self, duration: Duration) -> Option<DummyInstant> {
            self.offset.checked_sub(duration).map(|offset| DummyInstant { offset })
        }
    }

    impl ops::Add<Duration> for DummyInstant {
        type Output = DummyInstant;

        fn add(self, other: Duration) -> DummyInstant {
            DummyInstant { offset: self.offset + other }
        }
    }

    impl ops::Sub<DummyInstant> for DummyInstant {
        type Output = Duration;

        fn sub(self, other: DummyInstant) -> Duration {
            self.offset - other.offset
        }
    }

    impl ops::Sub<Duration> for DummyInstant {
        type Output = DummyInstant;

        fn sub(self, other: Duration) -> DummyInstant {
            DummyInstant { offset: self.offset - other }
        }
    }

    impl Debug for DummyInstant {
        fn fmt(&self, f: &mut Formatter<'_>) -> fmt::Result {
            write!(f, "{:?}", self.offset)
        }
    }

    /// A dummy [`InstantContext`] which stores the current time as a
    /// [`DummyInstant`].
    #[derive(Default)]
    pub(crate) struct DummyInstantContext {
        time: DummyInstant,
    }

    impl DummyInstantContext {
        /// Advance the current time by the given duration.
        pub(crate) fn sleep(&mut self, dur: Duration) {
            self.time.offset += dur;
        }
    }

    impl InstantContext for DummyInstantContext {
        type Instant = DummyInstant;
        fn now(&self) -> DummyInstant {
            self.time
        }
    }

    impl<T: AsRef<DummyInstantContext>> InstantContext for T {
        type Instant = DummyInstant;
        fn now(&self) -> DummyInstant {
            self.as_ref().now()
        }
    }

    /// Arbitrary data of type `D` attached to a `DummyInstant`.
    ///
    /// `InstantAndData` implements `Ord` and `Eq` to be used in a `BinaryHeap`
    /// and ordered by `DummyInstant`.
    #[derive(Clone)]
    struct InstantAndData<D>(DummyInstant, D);

    impl<D> InstantAndData<D> {
        fn new(time: DummyInstant, data: D) -> Self {
            Self(time, data)
        }
    }

    impl<D> Eq for InstantAndData<D> {}

    impl<D> PartialEq for InstantAndData<D> {
        fn eq(&self, other: &Self) -> bool {
            self.0 == other.0
        }
    }

    impl<D> Ord for InstantAndData<D> {
        fn cmp(&self, other: &Self) -> std::cmp::Ordering {
            other.0.cmp(&self.0)
        }
    }

    impl<D> PartialOrd for InstantAndData<D> {
        fn partial_cmp(&self, other: &Self) -> Option<std::cmp::Ordering> {
            Some(self.cmp(other))
        }
    }

    /// A dummy [`TimerContext`] which stores time as a [`DummyInstantContext`].
    pub(crate) struct DummyTimerContext<Id> {
        instant: DummyInstantContext,
        timers: BinaryHeap<InstantAndData<Id>>,
    }

    impl<Id> Default for DummyTimerContext<Id> {
        fn default() -> DummyTimerContext<Id> {
            DummyTimerContext {
                instant: DummyInstantContext::default(),
                timers: BinaryHeap::default(),
            }
        }
    }

    impl<Id: Clone> DummyTimerContext<Id> {
        /// Get an ordered list of all currently-scheduled timers.
        pub(crate) fn timers(&self) -> Vec<(DummyInstant, Id)> {
            self.timers.clone().into_sorted_vec().into_iter().map(|t| (t.0, t.1)).collect()
        }
    }

    impl<Id> AsRef<DummyInstantContext> for DummyTimerContext<Id> {
        fn as_ref(&self) -> &DummyInstantContext {
            &self.instant
        }
    }

    impl<Id: PartialEq> DummyTimerContext<Id> {
        // Just like `TimerContext::cancel_timer`, but takes a reference to `Id`
        // rather than a value. This allows us to implement
        // `schedule_timer_instant`, which needs to retain ownership of the
        // `Id`.
        fn cancel_timer_inner(&mut self, id: &Id) -> Option<DummyInstant> {
            let mut r: Option<DummyInstant> = None;
            // NOTE(brunodalbo): Cancelling timers can be made a faster than
            // this if we keep two data structures and require that `Id: Hash`.
            self.timers = self
                .timers
                .drain()
                .filter(|t| {
                    if &t.1 == id {
                        r = Some(t.0);
                        false
                    } else {
                        true
                    }
                })
                .collect::<Vec<_>>()
                .into();
            r
        }
    }

    impl<Id: PartialEq> TimerContext<Id> for DummyTimerContext<Id> {
        fn schedule_timer_instant(&mut self, time: DummyInstant, id: Id) -> Option<DummyInstant> {
            let ret = self.cancel_timer_inner(&id);
            self.timers.push(InstantAndData::new(time, id));
            ret
        }

        fn cancel_timer(&mut self, id: Id) -> Option<DummyInstant> {
            self.cancel_timer_inner(&id)
        }

        fn cancel_timers_with<F: FnMut(&Id) -> bool>(&mut self, mut f: F) {
            self.timers = self.timers.drain().filter(|t| !f(&t.1)).collect::<Vec<_>>().into();
        }

        fn scheduled_instant(&self, id: Id) -> Option<DummyInstant> {
            self.timers.iter().find_map(|x| if x.1 == id { Some(x.0) } else { None })
        }
    }

    pub(crate) trait DummyTimerContextExt<Id>: AsMut<DummyTimerContext<Id>> + Sized {
        /// Trigger the next timer, if any.
        ///
        /// `trigger_next_timer` triggers the next timer, if any, and advances
        /// the internal clock to the timer's scheduled time. It returns whether
        /// a timer was triggered.
        fn trigger_next_timer<H: TimerHandler<Self, Id>>(&mut self) -> bool {
            match self.as_mut().timers.pop() {
                Some(InstantAndData(t, id)) => {
                    self.as_mut().instant.time = t;
                    H::handle_timer(self, id);
                    true
                }
                None => false,
            }
        }

        /// Skip current time forward until `instant`, triggering all timers until
        /// then, inclusive.
        ///
        /// Returns the number of timers triggered.
        ///
        /// # Panics
        ///
        /// Panics if `instant` is in the past.
        fn trigger_timers_until_instant<H: TimerHandler<Self, Id>>(
            &mut self,
            instant: DummyInstant,
        ) -> usize {
            assert!(instant > self.as_mut().now());
            let mut timers_fired = 0;

            while let Some(tmr) = self.as_mut().timers.peek() {
                if tmr.0 > instant {
                    break;
                }

                assert!(self.trigger_next_timer::<H>());
                timers_fired += 1;
            }

            assert!(self.as_mut().now() <= instant);
            self.as_mut().instant.time = instant;

            timers_fired
        }

        /// Skip current time forward by `duration`, triggering all timers until
        /// then, inclusive.
        ///
        /// Returns the number of timers triggered.
        fn trigger_timers_for<H: TimerHandler<Self, Id>>(&mut self, duration: Duration) -> usize {
            let instant = self.as_mut().now() + duration;
            // We know the call to `self.trigger_timers_until_instant` will not panic because
            // we provide an instant that is greater than or equal to the current time.
            self.trigger_timers_until_instant::<H>(instant)
        }
    }

    impl<Id, T: AsMut<DummyTimerContext<Id>>> DummyTimerContextExt<Id> for T {}

    /// A dummy [`FrameContext`].
    pub struct DummyFrameContext<Meta> {
        frames: Vec<(Meta, Vec<u8>)>,
        should_error_for_frame: Option<Box<dyn Fn(&Meta) -> bool>>,
    }

    impl<Meta> DummyFrameContext<Meta> {
        /// Closure which can decide to cause an error to be thrown when handling a
        /// frame, based on the metadata.
        pub fn set_should_error_for_frame<F: Fn(&Meta) -> bool + 'static>(&mut self, f: F) {
            self.should_error_for_frame = Some(Box::new(f));
        }
    }

    impl<Meta> Default for DummyFrameContext<Meta> {
        fn default() -> DummyFrameContext<Meta> {
            DummyFrameContext { frames: Vec::new(), should_error_for_frame: None }
        }
    }

    impl<Meta> DummyFrameContext<Meta> {
        /// Get the frames sent so far.
        pub(crate) fn frames(&self) -> &[(Meta, Vec<u8>)] {
            self.frames.as_slice()
        }
    }

    impl<B: BufferMut, Meta> FrameContext<B, Meta> for DummyFrameContext<Meta> {
        fn send_frame<S: Serializer<Buffer = B>>(
            &mut self,
            metadata: Meta,
            frame: S,
        ) -> Result<(), S> {
            if let Some(should_error_for_frame) = &self.should_error_for_frame {
                if should_error_for_frame(&metadata) {
                    return Err(frame);
                }
            }

            self.frames.push(match frame.serialize_vec_outer() {
                Ok(buffer) => (metadata, buffer.as_ref().to_vec()),
                Err(_) => unreachable!(),
            });
            Ok(())
        }
    }

    /// A dummy [`CounterContext`].
    #[derive(Default)]
    pub(crate) struct DummyCounterContext {
        counters: HashMap<&'static str, usize>,
    }

    impl CounterContext for DummyCounterContext {
        fn increment_counter(&mut self, key: &'static str) {
            let val = self.counters.get(&key).cloned().unwrap_or(0);
            self.counters.insert(key, val + 1);
        }
    }

    impl<T: AsMut<DummyCounterContext>> CounterContext for T {
        // TODO(rheacock): This is tricky because it's used in test only macro
        // code so the compiler thinks `key` is unused. Remove this when this is
        // no longer a problem.
        #[allow(unused)]
        fn increment_counter(&mut self, key: &'static str) {
            self.as_mut().increment_counter(key);
        }
    }

    /// A wrapper for a [`DummyTimerContext`] and some other state.
    ///
    /// `DummyContext` pairs some arbitrary state, `S`, with a
    /// `DummyTimerContext`, a `DummyFrameContext`, and a `DummyCounterContext`.
    /// It implements [`InstantContext`], [`TimerContext`], [`FrameContext`],
    /// and [`CounterContext`]. It also provides getters for `S`. If the type,
    /// `S`, is meant to implement some other trait, then the caller is advised
    /// to instead implement that trait for `DummyContext<S, Id, Meta>`. This
    /// allows for full test mocks to be written with a minimum of boilerplate
    /// code.
    pub(crate) struct DummyContext<S, Id = (), Meta = ()> {
        state: S,
        timers: DummyTimerContext<Id>,
        frames: DummyFrameContext<Meta>,
        counters: DummyCounterContext,
        rng: FakeCryptoRng<XorShiftRng>,
    }

    impl<S: Default, Id, Meta> Default for DummyContext<S, Id, Meta> {
        fn default() -> DummyContext<S, Id, Meta> {
            DummyContext::with_state(S::default())
        }
    }

    impl<S, Id, Meta> DummyContext<S, Id, Meta> {
        /// Constructs a `DummyContext` with the given state and default
        /// `DummyTimerContext`, `DummyFrameContext`, and `DummyCounterContext`.
        pub(crate) fn with_state(state: S) -> DummyContext<S, Id, Meta> {
            DummyContext {
                state,
                timers: DummyTimerContext::default(),
                frames: DummyFrameContext::default(),
                counters: DummyCounterContext::default(),
                rng: FakeCryptoRng::new_xorshift(0),
            }
        }

        /// Move the clock forward by the given duration without firing any
        /// timers.
        ///
        /// If any timers are scheduled to fire in the given duration, future
        /// use of this `DummyContext` may have surprising or buggy behavior.
        pub(crate) fn sleep_skip_timers(&mut self, duration: Duration) {
            self.timers.instant.sleep(duration);
        }

        /// Get an immutable reference to the inner state.
        ///
        /// This method is provided instead of an [`AsRef`] impl to avoid
        /// conflicting with user-provided implementations of `AsRef<T> for
        /// DummyContext<S, Id, Meta>` for other types, `T`. It is named `get_ref`
        /// instead of `as_ref` so that programmer doesn't need to specify which
        /// `as_ref` method is intended.
        pub(crate) fn get_ref(&self) -> &S {
            &self.state
        }

        /// Get a mutable reference to the inner state.
        ///
        /// `get_mut` is like `get_ref`, but it returns a mutable reference.
        pub(crate) fn get_mut(&mut self) -> &mut S {
            &mut self.state
        }

        /// Get the list of frames sent so far.
        pub(crate) fn frames(&self) -> &[(Meta, Vec<u8>)] {
            self.frames.frames()
        }

        /// Get the value of the named counter.
        pub(crate) fn get_counter(&self, ctr: &str) -> usize {
            self.counters.counters.get(ctr).cloned().unwrap_or(0)
        }
    }

    impl<S, Id: Clone, Meta> DummyContext<S, Id, Meta> {
        /// Get an ordered list of all currently-scheduled timers.
        pub(crate) fn timers(&self) -> Vec<(DummyInstant, Id)> {
            self.timers.timers()
        }
    }

    impl<S, Id, Meta> AsRef<DummyInstantContext> for DummyContext<S, Id, Meta> {
        fn as_ref(&self) -> &DummyInstantContext {
            self.timers.as_ref()
        }
    }

    impl<S, Id, Meta> AsRef<DummyTimerContext<Id>> for DummyContext<S, Id, Meta> {
        fn as_ref(&self) -> &DummyTimerContext<Id> {
            &self.timers
        }
    }

    impl<S, Id, Meta> AsMut<DummyTimerContext<Id>> for DummyContext<S, Id, Meta> {
        fn as_mut(&mut self) -> &mut DummyTimerContext<Id> {
            &mut self.timers
        }
    }

    impl<S, Id, Meta> AsMut<DummyFrameContext<Meta>> for DummyContext<S, Id, Meta> {
        fn as_mut(&mut self) -> &mut DummyFrameContext<Meta> {
            &mut self.frames
        }
    }

    impl<S, Id, Meta> AsMut<DummyCounterContext> for DummyContext<S, Id, Meta> {
        fn as_mut(&mut self) -> &mut DummyCounterContext {
            &mut self.counters
        }
    }

    impl<S, Id: PartialEq, Meta> TimerContext<Id> for DummyContext<S, Id, Meta> {
        fn schedule_timer_instant(&mut self, time: DummyInstant, id: Id) -> Option<DummyInstant> {
            self.timers.schedule_timer_instant(time, id)
        }

        fn cancel_timer(&mut self, id: Id) -> Option<DummyInstant> {
            self.timers.cancel_timer(id)
        }

        fn cancel_timers_with<F: FnMut(&Id) -> bool>(&mut self, f: F) {
            self.timers.cancel_timers_with(f);
        }

        fn scheduled_instant(&self, id: Id) -> Option<DummyInstant> {
            self.timers.scheduled_instant(id)
        }
    }

    impl<B: BufferMut, S, Id, Meta> FrameContext<B, Meta> for DummyContext<S, Id, Meta> {
        fn send_frame<SS: Serializer<Buffer = B>>(
            &mut self,
            metadata: Meta,
            frame: SS,
        ) -> Result<(), SS> {
            self.frames.send_frame(metadata, frame)
        }
    }

    impl<S, Id, Meta> RngContext for DummyContext<S, Id, Meta> {
        type Rng = FakeCryptoRng<XorShiftRng>;

        fn rng(&mut self) -> &mut Self::Rng {
            &mut self.rng
        }
    }

    #[derive(Debug)]
    struct PendingFrameData<ContextId, DeviceId, Meta> {
        dst_context: ContextId,
        dst_device: DeviceId,
        meta: Meta,
        frame: Vec<u8>,
    }

    type PendingFrame<ContextId, DeviceId, Meta> =
        InstantAndData<PendingFrameData<ContextId, DeviceId, Meta>>;

    /// A dummy network, composed of many `DummyContext`s.
    ///
    /// Provides a utility to have many contexts keyed by `ContextId` that can
    /// exchange frames.
    pub(crate) struct DummyNetwork<ContextId, S, TimerId, DeviceId, SendMeta, RecvMeta, Links>
    where
        Links: DummyNetworkLinks<S, SendMeta, RecvMeta, ContextId, DeviceId>,
    {
        contexts: HashMap<ContextId, DummyContext<S, TimerId, SendMeta>>,
        current_time: DummyInstant,
        pending_frames: BinaryHeap<PendingFrame<ContextId, DeviceId, RecvMeta>>,
        links: Links,
    }

    /// A set of links in a `DummyNetwork`.
    ///
    /// A `DummyNetworkLinks` represents the set of links in a `DummyNetwork`.
    /// It exposes the link information by providing the ability to map from a
    /// frame's sending metadata - including its context, local state, and
    /// `SendMeta` - to the appropriate context ID, device ID, receive metadata,
    /// and latency that represent the receiver.
    pub(crate) trait DummyNetworkLinks<S, SendMeta, RecvMeta, ContextId, DeviceId> {
        fn map_link(
            &self,
            ctx: ContextId,
            state: &S,
            meta: SendMeta,
        ) -> (ContextId, DeviceId, RecvMeta, Option<Duration>);
    }

    impl<
            S,
            SendMeta,
            RecvMeta,
            ContextId,
            DeviceId,
            F: Fn(ContextId, &S, SendMeta) -> (ContextId, DeviceId, RecvMeta, Option<Duration>),
        > DummyNetworkLinks<S, SendMeta, RecvMeta, ContextId, DeviceId> for F
    {
        fn map_link(
            &self,
            ctx: ContextId,
            state: &S,
            meta: SendMeta,
        ) -> (ContextId, DeviceId, RecvMeta, Option<Duration>) {
            (self)(ctx, state, meta)
        }
    }

    /// The result of a single step in a `DummyNetwork`
    #[derive(Debug)]
    pub(crate) struct StepResult {
        time_delta: Duration,
        timers_fired: usize,
        frames_sent: usize,
    }

    impl StepResult {
        fn new(time_delta: Duration, timers_fired: usize, frames_sent: usize) -> Self {
            Self { time_delta, timers_fired, frames_sent }
        }

        fn new_idle() -> Self {
            Self::new(Duration::from_millis(0), 0, 0)
        }

        /// Returns the number of frames dispatched to their destinations in the
        /// last step.
        pub(crate) fn frames_sent(&self) -> usize {
            self.frames_sent
        }

        /// Returns the number of timers fired in the last step.
        pub(crate) fn timers_fired(&self) -> usize {
            self.timers_fired
        }
    }

    /// Error type that marks that one of the `run_until` family of functions
    /// reached a maximum number of iterations.
    #[derive(Debug)]
    pub(crate) struct LoopLimitReachedError;

    impl<ContextId, S, TimerId, DeviceId, SendMeta, RecvMeta, Links>
        DummyNetwork<ContextId, S, TimerId, DeviceId, SendMeta, RecvMeta, Links>
    where
        ContextId: Eq + Hash + Copy + Debug,
        TimerId: Copy,
        Links: DummyNetworkLinks<S, SendMeta, RecvMeta, ContextId, DeviceId>,
    {
        /// Creates a new `DummyNetwork`.
        ///
        /// Creates a new `DummyNetwork` with the collection of `DummyContext`s
        /// in `contexts`. `Context`s are named by type parameter `ContextId`.
        ///
        /// # Panics
        ///
        /// Calls to `new` will panic if given a `DummyContext` with timer
        /// events. `DummyContext`s given to `DummyNetwork` **must not** have
        /// any timer events already attached to them, because `DummyNetwork`
        /// maintains all the internal timers in dispatchers in sync to enable
        /// synchronous simulation steps.
        pub(crate) fn new<
            I: IntoIterator<Item = (ContextId, DummyContext<S, TimerId, SendMeta>)>,
        >(
            contexts: I,
            links: Links,
        ) -> Self {
            let mut ret = Self {
                contexts: contexts.into_iter().collect(),
                current_time: DummyInstant::default(),
                pending_frames: BinaryHeap::new(),
                links,
            };

            // We can't guarantee that all contexts are safely running their timers
            // together if we receive a context with any timers already set.
            assert!(
                !ret.contexts.iter().any(|(_, ctx)| { !ctx.timers.timers.is_empty() }),
                "can't start network with contexts that already have timers set"
            );

            // synchronize all dispatchers' current time to the same value:
            for (_, ctx) in ret.contexts.iter_mut() {
                ctx.timers.instant.time = ret.current_time;
            }

            ret
        }

        /// Retrieves a `DummyContext` named `context`.
        pub(crate) fn context<K: Into<ContextId>>(
            &mut self,
            context: K,
        ) -> &mut DummyContext<S, TimerId, SendMeta> {
            self.contexts.get_mut(&context.into()).unwrap()
        }

        /// Performs a single step in network simulation.
        ///
        /// `step` performs a single logical step in the collection of
        /// `Context`s held by this `DummyNetwork`. A single step consists of
        /// the following operations:
        ///
        /// - All pending frames, kept in each `DummyContext`, are mapped to
        ///   their destination context/device pairs and moved to an internal
        ///   collection of pending frames.
        /// - The collection of pending timers and scheduled frames is inspected
        ///   and a simulation time step is retrieved, which will cause a next
        ///   event to trigger. The simulation time is updated to the new time.
        /// - All scheduled frames whose deadline is less than or equal to the
        ///   new simulation time are sent to their destinations, handled using
        ///   the `FH` type parameter.
        /// - All timer events whose deadline is less than or equal to the new
        ///   simulation time are fired, handled using the `TH` type parameter.
        ///
        /// If any new events are created during the operation of frames or
        /// timers, they **will not** be taken into account in the current
        /// `step`. That is, `step` collects all the pending events before
        /// dispatching them, ensuring that an infinite loop can't be created as
        /// a side effect of calling `step`.
        ///
        /// The return value of `step` indicates which of the operations were
        /// performed.
        ///
        /// # Panics
        ///
        /// If `DummyNetwork` was set up with a bad `links`, calls to `step` may
        /// panic when trying to route frames to their context/device
        /// destinations.
        pub(crate) fn step<
            FH: FrameHandler<DummyContext<S, TimerId, SendMeta>, DeviceId, RecvMeta, Buf<Vec<u8>>>,
            TH: TimerHandler<DummyContext<S, TimerId, SendMeta>, TimerId>,
        >(
            &mut self,
        ) -> StepResult {
            self.collect_frames();

            let next_step = if let Some(t) = self.next_step() {
                t
            } else {
                return StepResult::new_idle();
            };

            // This assertion holds the contract that `next_step` does not
            // return a time in the past.
            assert!(next_step >= self.current_time);
            let mut ret = StepResult::new(next_step.duration_since(self.current_time), 0, 0);
            // Move time forward:
            self.current_time = next_step;
            for (_, ctx) in self.contexts.iter_mut() {
                ctx.timers.instant.time = next_step;
            }

            // Dispatch all pending frames:
            while let Some(InstantAndData(t, _)) = self.pending_frames.peek() {
                // TODO(brunodalbo): Remove this break once let_chains is
                // stable.
                if *t > self.current_time {
                    break;
                }
                // We can unwrap because we just peeked.
                let frame = self.pending_frames.pop().unwrap().1;
                FH::handle_frame(
                    self.context(frame.dst_context),
                    frame.dst_device,
                    frame.meta,
                    Buf::new(frame.frame, ..),
                );
                ret.frames_sent += 1;
            }

            // Dispatch all pending timers.
            for (_, ctx) in self.contexts.iter_mut() {
                // We have to collect the timers before dispatching them, to
                // avoid an infinite loop in case handle_timer schedules another
                // timer for the same or older DummyInstant.
                let mut timers = Vec::<TimerId>::new();
                while let Some(InstantAndData(t, id)) = ctx.timers.timers.peek() {
                    // TODO(brunodalbo): remove this break once let_chains is stable
                    if *t > ctx.now() {
                        break;
                    }
                    timers.push(*id);
                    ctx.timers.timers.pop();
                }

                for t in timers {
                    TH::handle_timer(ctx, t);
                    ret.timers_fired += 1;
                }
            }

            ret
        }

        /// Collects all queued frames.
        ///
        /// Collects all pending frames and schedules them for delivery to the
        /// destination context/device based on the result of `links`. The
        /// collected frames are queued for dispatching in the `DummyNetwork`,
        /// ordered by their scheduled delivery time given by the latency result
        /// provided by `links`.
        fn collect_frames(&mut self) {
            let all_frames: Vec<(ContextId, Vec<(SendMeta, Vec<u8>)>)> = self
                .contexts
                .iter_mut()
                .filter_map(|(n, ctx)| {
                    if ctx.frames.frames.is_empty() {
                        None
                    } else {
                        Some((n.clone(), ctx.frames.frames.drain(..).collect()))
                    }
                })
                .collect();

            for (src_context, frames) in all_frames.into_iter() {
                for (send_meta, frame) in frames.into_iter() {
                    let (dst_context, dst_device, recv_meta, latency) = self.links.map_link(
                        src_context,
                        self.contexts.get(&src_context).unwrap().get_ref(),
                        send_meta,
                    );
                    self.pending_frames.push(PendingFrame::new(
                        self.current_time + latency.unwrap_or(Duration::from_millis(0)),
                        PendingFrameData { frame, dst_context, dst_device, meta: recv_meta },
                    ));
                }
            }
        }

        /// Calculates the next `DummyInstant` when events are available.
        ///
        /// Returns the smallest `DummyInstant` greater than or equal to the
        /// current time for which an event is available. If no events are
        /// available, returns `None`.
        fn next_step(&self) -> Option<DummyInstant> {
            // get earliest timer in all contexts
            let next_timer = self
                .contexts
                .iter()
                .filter_map(|(_, ctx)| match ctx.timers.timers.peek() {
                    Some(tmr) => Some(tmr.0),
                    None => None,
                })
                .min();
            // get the instant for the next packet
            let next_packet_due = self.pending_frames.peek().map(|t| t.0);

            // Return the earliest of them both, and protect against returning a
            // time in the past.
            match next_timer {
                Some(t) if next_packet_due.is_some() => Some(t).min(next_packet_due),
                Some(t) => Some(t),
                None => next_packet_due,
            }
            .map(|t| t.max(self.current_time))
        }
    }

    mod tests {
        use super::*;

        #[test]
        fn test_instant_and_data() {
            // verify implementation of InstantAndData to be used as a complex type
            // in a BinaryHeap:
            let mut heap = BinaryHeap::<InstantAndData<usize>>::new();
            let now = DummyInstant::default();

            fn new_data(time: DummyInstant, id: usize) -> InstantAndData<usize> {
                InstantAndData::new(time, id)
            }

            heap.push(new_data(now + Duration::from_secs(1), 1));
            heap.push(new_data(now + Duration::from_secs(2), 2));

            // earlier timer is popped first
            assert!(heap.pop().unwrap().1 == 1);
            assert!(heap.pop().unwrap().1 == 2);
            assert!(heap.pop().is_none());

            heap.push(new_data(now + Duration::from_secs(1), 1));
            heap.push(new_data(now + Duration::from_secs(1), 1));

            // can pop twice with identical data:
            assert!(heap.pop().unwrap().1 == 1);
            assert!(heap.pop().unwrap().1 == 1);
            assert!(heap.pop().is_none());
        }

        #[test]
        fn test_dummy_timer_context() {
            // An implementation of `TimerContext` that uses `usize` timer IDs
            // and stores every timer in a `Vec`.
            impl TimerHandler<DummyContext<Vec<(usize, DummyInstant)>, usize>, usize> for () {
                fn handle_timer(
                    ctx: &mut DummyContext<Vec<(usize, DummyInstant)>, usize>,
                    id: usize,
                ) {
                    let now = ctx.now();
                    ctx.get_mut().push((id, now));
                }
            }

            let mut ctx = DummyContext::<Vec<(usize, DummyInstant)>, usize>::default();

            // When no timers are installed, `trigger_next_timer` should return
            // `false`.
            assert!(!ctx.trigger_next_timer::<()>());
            assert_eq!(ctx.get_ref().as_slice(), []);

            const ONE_SEC: Duration = Duration::from_secs(1);
            const ONE_SEC_INSTANT: DummyInstant = DummyInstant { offset: ONE_SEC };

            // When one timer is installed, it should be triggered.
            ctx = Default::default();

            // No timer with id `0` exists yet.
            assert!(ctx.scheduled_instant(0).is_none());

            ctx.schedule_timer(ONE_SEC, 0);

            // Timer with id `0` scheduled to execute at `ONE_SEC_INSTANT`.
            assert_eq!(ctx.scheduled_instant(0).unwrap(), ONE_SEC_INSTANT);

            assert!(ctx.trigger_next_timer::<()>());
            assert_eq!(ctx.get_ref().as_slice(), [(0, ONE_SEC_INSTANT)]);

            // After the timer fires, it should not still be scheduled at some instant.
            assert!(ctx.scheduled_instant(0).is_none());

            // The time should have been advanced.
            assert_eq!(ctx.now(), ONE_SEC_INSTANT);

            // Once it's been triggered, it should be canceled and not triggerable again.
            ctx = Default::default();
            assert!(!ctx.trigger_next_timer::<()>());
            assert_eq!(ctx.get_ref().as_slice(), []);

            // If we schedule a timer but then cancel it, it shouldn't fire.
            ctx = Default::default();
            ctx.schedule_timer(ONE_SEC, 0);
            assert_eq!(ctx.cancel_timer(0), Some(ONE_SEC_INSTANT));
            assert!(!ctx.trigger_next_timer::<()>());
            assert_eq!(ctx.get_ref().as_slice(), []);

            // If we schedule a timer but then schedule the same ID again, the
            // second timer should overwrite the first one.
            ctx = Default::default();
            ctx.schedule_timer(Duration::from_secs(0), 0);
            ctx.schedule_timer(ONE_SEC, 0);
            assert_eq!(ctx.cancel_timer(0), Some(ONE_SEC_INSTANT));

            // If we schedule three timers and then run `trigger_timers_until`
            // with the appropriate value, only two of them should fire.
            ctx = Default::default();
            ctx.schedule_timer(Duration::from_secs(0), 0);
            ctx.schedule_timer(Duration::from_secs(1), 1);
            ctx.schedule_timer(Duration::from_secs(2), 2);
            ctx.trigger_timers_until_instant::<()>(ONE_SEC_INSTANT);

            // The first two timers should have fired.
            assert_eq!(
                ctx.get_ref().as_slice(),
                [(0, DummyInstant::from(Duration::from_secs(0))), (1, ONE_SEC_INSTANT)]
            );

            // They should be canceled now.
            assert!(ctx.cancel_timer(0).is_none());
            assert!(ctx.cancel_timer(1).is_none());

            // The clock should have been updated.
            assert_eq!(ctx.now(), ONE_SEC_INSTANT);

            // The last timer should not have fired.
            assert_eq!(ctx.cancel_timer(2), Some(DummyInstant::from(Duration::from_secs(2))));
        }
    }
}
