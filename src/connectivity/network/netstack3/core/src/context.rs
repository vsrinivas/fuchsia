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

use packet::{BufferMut, Serializer};

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
    fn cancel_timer(&mut self, id: &Id) -> Option<Self::Instant>;
}

/// A handler for timer firing events.
///
/// A `TimerHandler` is a type capable of handling the event of a timer firing.
pub(crate) trait TimerHandler<Ctx, Id> {
    /// Handle a timer firing.
    fn handle_timer(ctx: &mut Ctx, id: Id);
}

/// A context that provides access to state.
///
/// `StateContext` stores instances of `State` keyed by `Id`, and provides
/// getters for this state. If `Id` is `()`, then `StateContext` represents a
/// single instance of `State`.
pub(crate) trait StateContext<Id, State> {
    /// Get the state immutably.
    fn get_state(&self, id: Id) -> &State;

    /// Get the state mutably.
    fn get_state_mut(&mut self, id: Id) -> &mut State;
}

impl<State, T: AsRef<State> + AsMut<State>> StateContext<(), State> for T {
    fn get_state(&self, _id: ()) -> &State {
        self.as_ref()
    }

    fn get_state_mut(&mut self, _id: ()) -> &mut State {
        self.as_mut()
    }
}

/// A context for sending frames.
pub(crate) trait FrameContext<B: BufferMut, Meta> {
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
    use std::ops;
    use std::time::Duration;

    use super::*;
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
        fn fmt(&self, f: &mut Formatter) -> fmt::Result {
            write!(f, "{:?}", self.offset)
        }
    }

    /// A dummy [`InstantContext`] which stores the current time as a
    /// [`DummyInstant`].
    #[derive(Default)]
    pub(crate) struct DummyInstantContext {
        time: DummyInstant,
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

    impl<Id: PartialEq> TimerContext<Id> for DummyTimerContext<Id> {
        fn schedule_timer_instant(&mut self, time: DummyInstant, id: Id) -> Option<DummyInstant> {
            let ret = self.cancel_timer(&id);
            self.timers.push(InstantAndData::new(time, id));
            ret
        }

        fn cancel_timer(&mut self, id: &Id) -> Option<DummyInstant> {
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

        /// Skip current time forward by `duration`, triggering all timers until
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
    }

    impl<Id, T: AsMut<DummyTimerContext<Id>>> DummyTimerContextExt<Id> for T {}

    impl<Id: PartialEq, T: AsRef<DummyInstantContext> + AsMut<DummyTimerContext<Id>>>
        TimerContext<Id> for T
    {
        fn schedule_timer_instant(&mut self, time: DummyInstant, id: Id) -> Option<DummyInstant> {
            self.as_mut().schedule_timer_instant(time, id)
        }

        fn cancel_timer(&mut self, id: &Id) -> Option<DummyInstant> {
            self.as_mut().cancel_timer(id)
        }
    }

    /// A dummy [`FrameContext`].
    pub(crate) struct DummyFrameContext<Meta> {
        frames: Vec<(Meta, Vec<u8>)>,
    }

    impl<Meta> Default for DummyFrameContext<Meta> {
        fn default() -> DummyFrameContext<Meta> {
            DummyFrameContext { frames: Vec::new() }
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
            self.frames.push(match frame.serialize_vec_outer() {
                Ok(buffer) => (metadata, buffer.as_ref().to_vec()),
                Err(_) => unreachable!(),
            });
            Ok(())
        }
    }

    impl<B: BufferMut, Meta, T: AsMut<DummyFrameContext<Meta>>> FrameContext<B, Meta> for T {
        fn send_frame<S: Serializer<Buffer = B>>(
            &mut self,
            metadata: Meta,
            frame: S,
        ) -> Result<(), S> {
            self.as_mut().send_frame(metadata, frame)
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
    }

    impl<S: Default, Id, Meta> Default for DummyContext<S, Id, Meta> {
        fn default() -> DummyContext<S, Id, Meta> {
            DummyContext {
                state: S::default(),
                timers: DummyTimerContext::default(),
                frames: DummyFrameContext::default(),
                counters: DummyCounterContext::default(),
            }
        }
    }

    impl<S, Id, Meta> DummyContext<S, Id, Meta> {
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
            ctx.schedule_timer(ONE_SEC, 0);
            assert!(ctx.trigger_next_timer::<()>());
            assert_eq!(ctx.get_ref().as_slice(), [(0, ONE_SEC_INSTANT)]);

            // The time should have been advanced.
            assert_eq!(ctx.now(), ONE_SEC_INSTANT);

            // Once it's been triggered, it should be canceled and not triggerable again.
            ctx = Default::default();
            assert!(!ctx.trigger_next_timer::<()>());
            assert_eq!(ctx.get_ref().as_slice(), []);

            // If we schedule a timer but then cancel it, it shouldn't fire.
            ctx = Default::default();
            ctx.schedule_timer(ONE_SEC, 0);
            assert_eq!(ctx.cancel_timer(&0), Some(ONE_SEC_INSTANT));
            assert!(!ctx.trigger_next_timer::<()>());
            assert_eq!(ctx.get_ref().as_slice(), []);

            // If we schedule a timer but then schedule the same ID again, the
            // second timer should overwrite the first one.
            ctx = Default::default();
            ctx.schedule_timer(Duration::from_secs(0), 0);
            ctx.schedule_timer(ONE_SEC, 0);
            assert_eq!(ctx.cancel_timer(&0), Some(ONE_SEC_INSTANT));

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
            assert!(ctx.cancel_timer(&0).is_none());
            assert!(ctx.cancel_timer(&1).is_none());

            // The clock should have been updated.
            assert_eq!(ctx.now(), ONE_SEC_INSTANT);

            // The last timer should not have fired.
            assert_eq!(ctx.cancel_timer(&2), Some(DummyInstant::from(Duration::from_secs(2))));
        }
    }
}
