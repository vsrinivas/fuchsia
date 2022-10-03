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
//! # Synchronized vs Non-Synchronized Contexts
//!
//! Since Netstack3 aspires to be multi-threaded in the future, some resources
//! need to be shared between threads, including resources which are accessed
//! via context traits. Sometimes, this resource sharing has implications for
//! how the context's behavior is exposed via its API, and thus has implications
//! for how the consuming code needs to interact with that API.
//!
//! For this reason, some modules require two different contexts - a
//! "synchronized" context and a "non-synchronized" context. Traits implementing
//! a synchronized context are named `FooSyncContext` while traits implementing
//! a non-synchronized context are named `FooContext`. Note that the
//! implementation of a non-synchronized context trait may still provide access
//! to shared resources - the distinction is simply that the consumer doesn't
//! need to be aware that that's what's happening under the hood. As a result,
//! this shared access is not assumed by the context trait itself. Its API is
//! designed just as it would be if exclusive access were assumed. For example,
//! even though a multi-threaded implementation of [`TimerContext`] would need
//! to synchronize on a shared set of timers, the `TimerContext` trait is
//! designed as though it were providing a vanilla, unsynchronized container.
//!
//! When synchronized contexts provide access to state, they do it via a
//! `with`-style API:
//!
//! ```rust
//! trait FooSyncContext<S> {
//!     fn with_state<O, F: FnOnce(&mut S) -> O>(&mut self, f: F) -> O;
//! }
//! ```
//!
//! This style is easy to implement when state is shared and mutated via
//! interior mutability (e.g., using mutexes), and is also easy to implement
//! when state is accessed exclusively (e.g., when writing a test mock). It also
//! makes it clear that a critical section is starting and ending, and thus
//! makes it clear to the programmer that they're performing a potentially
//! expensive operation, and hopefully encourages them to minimize the duration
//! of the critical section.
//!
//! Since the `with_xxx` method operates on `&mut self`, it prevents other
//! operations on the context from happening concurrently. This prevents
//! deadlocks which occur as a result of a single mutex being locked while it is
//! held by the locking thread - in other words, it prevents lock reentrance.
//!
//! [`ArpContext`]: crate::device::arp::ArpContext

use core::time::Duration;

use packet::{BufferMut, Serializer};
use rand::{CryptoRng, RngCore};

use crate::{Instant, NonSyncContext, SyncCtx};

/// A marker trait indicating that the implementor is not the [`DummySyncCtx`]
/// type found in test environments.
///
/// See [this issue] for details on why this is needed.
///
/// [`DummySyncCtx`]: testutil::DummySyncCtx
/// [this issue]: https://github.com/rust-lang/rust/issues/97811
pub(crate) trait NonTestCtxMarker {}

impl<NonSyncCtx: NonSyncContext> NonTestCtxMarker for &'_ SyncCtx<NonSyncCtx> {}

/// A context that provides access to a monotonic clock.
pub trait InstantContext {
    /// The type of an instant in time.
    ///
    /// All time is measured using `Instant`s, including scheduling timers
    /// through [`TimerContext`]. This type may represent some sort of
    /// real-world time (e.g., [`std::time::Instant`]), or may be mocked in
    /// testing using a fake clock.
    type Instant: Instant + 'static;

    /// Returns the current instant.
    ///
    /// `now` guarantees that two subsequent calls to `now` will return
    /// monotonically non-decreasing values.
    fn now(&self) -> Self::Instant;
}

/// An [`InstantContext`] which stores a cached value for the current time.
///
/// `CachedInstantCtx`s are constructed via [`new_cached_instant_context`].
pub(crate) struct CachedInstantCtx<I>(I);

impl<I: Instant + 'static> InstantContext for CachedInstantCtx<I> {
    type Instant = I;
    fn now(&self) -> I {
        self.0.clone()
    }
}

/// Construct a new `CachedInstantCtx` from the current time.
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
) -> CachedInstantCtx<I::Instant> {
    CachedInstantCtx(ctx.now())
}

/// A context that supports scheduling timers.
pub trait TimerContext<Id>: InstantContext {
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
    /// Returns the [`Instant`] a timer with ID `id` will be invoked. If no
    /// timer with the given ID exists, `scheduled_instant` will return `None`.
    fn scheduled_instant(&self, id: Id) -> Option<Self::Instant>;
}

/// A handler for timer firing events.
///
/// A `TimerHandler` is a type capable of handling the event of a timer firing.
pub(crate) trait TimerHandler<C, Id> {
    /// Handle a timer firing.
    fn handle_timer(&mut self, ctx: &mut C, id: Id);
}

// NOTE:
// - Code in this crate is required to only obtain random values through an
//   `RngContext`. This allows a deterministic RNG to be provided when useful
//   (for example, in tests).
// - The CSPRNG requirement exists so that random values produced within the
//   network stack are not predictable by outside observers. This helps prevent
//   certain kinds of fingerprinting and denial of service attacks.

/// A context that provides a random number generator (RNG).
pub trait RngContext {
    // TODO(joshlf): If the CSPRNG requirement becomes a performance problem,
    // introduce a second, non-cryptographically secure, RNG.

    /// The random number generator (RNG) provided by this `RngContext`.
    ///
    /// The provided RNG must be cryptographically secure, and users may rely on
    /// that property for their correctness and security.
    type Rng: RngCore + CryptoRng;

    /// Gets the random number generator (RNG).
    fn rng(&self) -> &Self::Rng;

    /// Gets the random number generator (RNG) mutably.
    fn rng_mut(&mut self) -> &mut Self::Rng;
}

/// A context for receiving frames.
pub trait RecvFrameContext<C, B: BufferMut, Meta> {
    /// Receive a frame.
    ///
    /// `receive_frame` receives a frame with the given metadata.
    fn receive_frame(&mut self, ctx: &mut C, metadata: Meta, frame: B);
}

// TODO(joshlf): Rename `FrameContext` to `SendFrameContext`

/// A context for sending frames.
pub trait FrameContext<C, B: BufferMut, Meta> {
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
    fn send_frame<S: Serializer<Buffer = B>>(
        &mut self,
        ctx: &mut C,
        metadata: Meta,
        frame: S,
    ) -> Result<(), S>;
}

/// A context that stores performance counters.
///
/// `CounterContext` allows counters keyed by string names to be incremented for
/// testing and debugging purposes. It is assumed that, if a no-op
/// implementation of [`increment_counter`] is provided, then calls will be
/// optimized out entirely by the compiler.
pub trait CounterContext {
    /// Increment the counter with the given key.
    fn increment_counter(&mut self, key: &'static str);
}

/// A context for emitting events.
///
/// `EventContext` encodes the common pattern for emitting atomic events of type
/// `T` from core. An implementation of `EventContext` must guarantee that
/// events are processed in the order they are emitted.
pub trait EventContext<T> {
    /// Handles `event`.
    fn on_event(&mut self, event: T);
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
    use alloc::{
        boxed::Box,
        collections::{BinaryHeap, HashMap},
        format,
        string::String,
        vec,
        vec::Vec,
    };
    use core::{
        fmt::{self, Debug, Formatter},
        hash::Hash,
        iter::FromIterator,
        marker::PhantomData,
        ops::{self, RangeBounds},
    };

    use assert_matches::assert_matches;
    use packet::Buf;
    use rand_xorshift::XorShiftRng;

    use super::*;
    use crate::{
        data_structures::ref_counted_hash_map::{RefCountedHashSet, RemoveResult},
        device::DeviceId,
        testutil::FakeCryptoRng,
        Instant,
    };

    /// A dummy implementation of `Instant` for use in testing.
    #[derive(Default, Copy, Clone, Eq, PartialEq, Ord, PartialOrd)]
    pub struct DummyInstant {
        // A DummyInstant is just an offset from some arbitrary epoch.
        offset: Duration,
    }

    impl DummyInstant {
        pub(crate) const LATEST: DummyInstant = DummyInstant { offset: Duration::MAX };

        fn saturating_add(self, dur: Duration) -> DummyInstant {
            DummyInstant { offset: self.offset.saturating_add(dur) }
        }
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

        fn add(self, dur: Duration) -> DummyInstant {
            DummyInstant { offset: self.offset + dur }
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

        fn sub(self, dur: Duration) -> DummyInstant {
            DummyInstant { offset: self.offset - dur }
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
    pub struct DummyInstantCtx {
        time: DummyInstant,
    }

    impl DummyInstantCtx {
        /// Advance the current time by the given duration.
        pub(crate) fn sleep(&mut self, dur: Duration) {
            self.time.offset += dur;
        }
    }

    impl InstantContext for DummyInstantCtx {
        type Instant = DummyInstant;
        fn now(&self) -> DummyInstant {
            self.time
        }
    }

    impl<T: AsRef<DummyInstantCtx>> InstantContext for T {
        type Instant = DummyInstant;
        fn now(&self) -> DummyInstant {
            self.as_ref().now()
        }
    }

    /// Arbitrary data of type `D` attached to a `DummyInstant`.
    ///
    /// `InstantAndData` implements `Ord` and `Eq` to be used in a `BinaryHeap`
    /// and ordered by `DummyInstant`.
    #[derive(Clone, Debug)]
    pub(crate) struct InstantAndData<D>(pub(crate) DummyInstant, pub(crate) D);

    impl<D> InstantAndData<D> {
        pub(crate) fn new(time: DummyInstant, data: D) -> Self {
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
        fn cmp(&self, other: &Self) -> core::cmp::Ordering {
            other.0.cmp(&self.0)
        }
    }

    impl<D> PartialOrd for InstantAndData<D> {
        fn partial_cmp(&self, other: &Self) -> Option<core::cmp::Ordering> {
            Some(self.cmp(other))
        }
    }

    /// A dummy [`TimerContext`] which stores time as a [`DummyInstantCtx`].
    pub(crate) struct DummyTimerCtx<Id> {
        instant: DummyInstantCtx,
        timers: BinaryHeap<InstantAndData<Id>>,
    }

    impl<Id> Default for DummyTimerCtx<Id> {
        fn default() -> DummyTimerCtx<Id> {
            DummyTimerCtx { instant: DummyInstantCtx::default(), timers: BinaryHeap::default() }
        }
    }

    impl<Id> AsMut<DummyTimerCtx<Id>> for DummyTimerCtx<Id> {
        fn as_mut(&mut self) -> &mut DummyTimerCtx<Id> {
            self
        }
    }

    impl<Id: Clone> DummyTimerCtx<Id> {
        /// Get an ordered list of all currently-scheduled timers.
        pub(crate) fn timers(&self) -> Vec<(DummyInstant, Id)> {
            self.timers
                .clone()
                .into_sorted_vec()
                .into_iter()
                .map(|InstantAndData(i, id)| (i, id))
                .collect()
        }
    }

    pub(crate) trait DummyInstantRange: Debug {
        fn contains(&self, i: DummyInstant) -> bool;

        /// Converts `&self` to a type-erased trait object reference.
        ///
        /// This makes it more ergonomic to construct a `&dyn
        /// DummyInstantRange`, which is necessary in order to use different
        /// range types in a context in which a single concrete type is
        /// expected.
        fn as_dyn(&self) -> &dyn DummyInstantRange
        where
            Self: Sized,
        {
            self
        }
    }

    impl DummyInstantRange for DummyInstant {
        fn contains(&self, i: DummyInstant) -> bool {
            self == &i
        }
    }

    impl<B: RangeBounds<DummyInstant> + Debug> DummyInstantRange for B {
        fn contains(&self, i: DummyInstant) -> bool {
            RangeBounds::contains(self, &i)
        }
    }

    // This impl is necessary in order to allow passing different range types
    // to `assert_timers_installed` and friends in a single call.
    impl<'a> DummyInstantRange for &'a dyn DummyInstantRange {
        fn contains(&self, i: DummyInstant) -> bool {
            <dyn DummyInstantRange as DummyInstantRange>::contains(*self, i)
        }
    }

    impl<Id: Debug + Clone + Hash + Eq> DummyTimerCtx<Id> {
        /// Asserts that `self` contains exactly the timers in `timers`.
        ///
        /// Each timer must be present, and its deadline must fall into the
        /// specified range. Ranges may be specified either as a specific
        /// [`DummyInstant`] or as any [`RangeBounds<DummyInstant>`].
        ///
        /// # Panics
        ///
        /// Panics if `timers` contains the same ID more than once or if `self`
        /// does not contain exactly the timers in `timers`.
        ///
        /// [`RangeBounds<DummyInstant>`]: core::ops::RangeBounds
        #[track_caller]
        pub(crate) fn assert_timers_installed<
            R: DummyInstantRange,
            I: IntoIterator<Item = (Id, R)>,
        >(
            &self,
            timers: I,
        ) {
            self.assert_timers_installed_inner(timers, true);
        }

        /// Asserts that `self` contains at least the timers in `timers`.
        ///
        /// Like [`assert_timers_installed`], but only asserts that `timers` is
        /// a subset of the timers installed; other timers may be installed in
        /// addition to those in `timers`.
        #[track_caller]
        pub(crate) fn assert_some_timers_installed<
            R: DummyInstantRange,
            I: IntoIterator<Item = (Id, R)>,
        >(
            &self,
            timers: I,
        ) {
            self.assert_timers_installed_inner(timers, false);
        }

        /// Asserts that no timers are installed.
        ///
        /// # Panics
        ///
        /// Panics if any timers are installed.
        #[track_caller]
        pub(crate) fn assert_no_timers_installed(&self) {
            self.assert_timers_installed::<DummyInstant, _>([]);
        }

        #[track_caller]
        fn assert_timers_installed_inner<R: DummyInstantRange, I: IntoIterator<Item = (Id, R)>>(
            &self,
            timers: I,
            exact: bool,
        ) {
            let mut timers = timers.into_iter().fold(HashMap::new(), |mut timers, (id, range)| {
                assert_matches!(timers.insert(id, range), None);
                timers
            });

            enum Error<Id, R: DummyInstantRange> {
                ExpectedButMissing { id: Id, range: R },
                UnexpectedButPresent { id: Id, instant: DummyInstant },
                UnexpectedInstant { id: Id, range: R, instant: DummyInstant },
            }

            let mut errors = Vec::new();

            // Make sure that all installed timers were expected (present in
            // `timers`).
            for InstantAndData(instant, id) in self.timers.iter().cloned() {
                match timers.remove(&id) {
                    None => {
                        if exact {
                            errors.push(Error::UnexpectedButPresent { id, instant })
                        }
                    }
                    Some(range) => {
                        if !range.contains(instant) {
                            errors.push(Error::UnexpectedInstant { id, range, instant })
                        }
                    }
                }
            }

            // Make sure that all expected timers were already found in
            // `self.timers` (and removed from `timers`).
            errors
                .extend(timers.drain().map(|(id, range)| Error::ExpectedButMissing { id, range }));

            if errors.len() > 0 {
                let mut s = String::from("Unexpected timer contents:");
                for err in errors {
                    s += &match err {
                        Error::ExpectedButMissing { id, range } => {
                            format!("\n\tMissing timer {:?} with deadline {:?}", id, range)
                        }
                        Error::UnexpectedButPresent { id, instant } => {
                            format!("\n\tUnexpected timer {:?} with deadline {:?}", id, instant)
                        }
                        Error::UnexpectedInstant { id, range, instant } => format!(
                            "\n\tTimer {:?} has unexpected deadline {:?} (wanted {:?})",
                            id, instant, range
                        ),
                    };
                }
                panic!("{}", s);
            }
        }
    }

    impl<Id> AsRef<DummyInstantCtx> for DummyTimerCtx<Id> {
        fn as_ref(&self) -> &DummyInstantCtx {
            &self.instant
        }
    }

    impl<Id: PartialEq> DummyTimerCtx<Id> {
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

    impl<Id: PartialEq> TimerContext<Id> for DummyTimerCtx<Id> {
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

    pub(crate) trait DummyTimerCtxExt<Id: Clone>: AsMut<DummyTimerCtx<Id>> + Sized {
        /// Triggers the next timer, if any, by calling `f` on it.
        ///
        /// `trigger_next_timer` triggers the next timer, if any, advances the
        /// internal clock to the timer's scheduled time, and returns its ID.
        fn trigger_next_timer<C, F: FnMut(C, &mut Self, Id)>(
            &mut self,
            ctx: C,
            mut f: F,
        ) -> Option<Id> {
            self.as_mut().timers.pop().map(|InstantAndData(t, id)| {
                self.as_mut().instant.time = t;
                f(ctx, self, id.clone());
                id
            })
        }

        /// Skips the current time forward until `instant`, triggering all
        /// timers until then, inclusive, by calling `f` on them.
        ///
        /// Returns the timers which were triggered.
        ///
        /// # Panics
        ///
        /// Panics if `instant` is in the past.
        fn trigger_timers_until_instant<F: FnMut(&mut Self, Id)>(
            &mut self,
            instant: DummyInstant,
            mut f: F,
        ) -> Vec<Id> {
            assert!(instant >= self.as_mut().now());
            let mut timers = Vec::new();

            while self
                .as_mut()
                .timers
                .peek()
                .map(|InstantAndData(i, _id)| i <= &instant)
                .unwrap_or(false)
            {
                timers.push(self.trigger_next_timer(&mut (), |_: &mut (), s, id| f(s, id)).unwrap())
            }

            assert!(self.as_mut().now() <= instant);
            self.as_mut().instant.time = instant;

            timers
        }

        /// Skips the current time forward by `duration`, triggering all timers
        /// until then, inclusive, by calling `f` on them.
        ///
        /// Returns the timers which were triggered.
        fn trigger_timers_for<F: FnMut(&mut Self, Id)>(
            &mut self,
            duration: Duration,
            f: F,
        ) -> Vec<Id> {
            let instant = self.as_mut().now().saturating_add(duration);
            // We know the call to `self.trigger_timers_until_instant` will not
            // panic because we provide an instant that is greater than or equal
            // to the current time.
            self.trigger_timers_until_instant(instant, f)
        }

        /// Triggers timers and expects them to be the given timers.
        ///
        /// The number of timers to be triggered is taken to be the number of
        /// timers produced by `timers`. Timers may be triggered in any order.
        ///
        /// # Panics
        ///
        /// Panics under the following conditions:
        /// - Fewer timers could be triggered than expected
        /// - Timers were triggered that were not expected
        /// - Timers that were expected were not triggered
        #[track_caller]
        fn trigger_timers_and_expect_unordered<
            I: IntoIterator<Item = Id>,
            F: FnMut(&mut Self, Id),
        >(
            &mut self,
            timers: I,
            mut f: F,
        ) where
            Id: Debug + Hash + Eq,
        {
            let mut timers = RefCountedHashSet::from_iter(timers);

            for _ in 0..timers.len() {
                let id = self
                    .trigger_next_timer(&mut (), |_: &mut (), s, id| f(s, id))
                    .expect("ran out of timers to trigger");
                match timers.remove(id.clone()) {
                    RemoveResult::Removed(()) | RemoveResult::StillPresent => {}
                    RemoveResult::NotPresent => panic!("triggered unexpected timer: {:?}", id),
                }
            }

            if timers.len() > 0 {
                let mut s = String::from("Expected timers did not trigger:");
                for (id, count) in timers.iter_counts() {
                    s += &format!("\n\t{count}x {id:?}");
                }
                panic!("{}", s);
            }
        }

        /// Triggers timers until `instant` and expects them to be the given
        /// timers.
        ///
        /// Like `trigger_timers_and_expect_unordered`, except that timers will
        /// only be triggered until `instant` (inclusive).
        fn trigger_timers_until_and_expect_unordered<
            I: IntoIterator<Item = Id>,
            F: FnMut(&mut Self, Id),
        >(
            &mut self,
            instant: DummyInstant,
            timers: I,
            f: F,
        ) where
            Id: Debug + Hash + Eq,
        {
            let mut timers = RefCountedHashSet::from_iter(timers);

            let triggered_timers = self.trigger_timers_until_instant(instant, f);

            for id in triggered_timers {
                match timers.remove(id.clone()) {
                    RemoveResult::Removed(()) | RemoveResult::StillPresent => {}
                    RemoveResult::NotPresent => panic!("triggered unexpected timer: {:?}", id),
                }
            }

            if timers.len() > 0 {
                let mut s = String::from("Expected timers did not trigger:");
                for (id, count) in timers.iter_counts() {
                    s += &format!("\n\t{count}x {id:?}");
                }
                panic!("{}", s);
            }
        }

        /// Triggers timers for `duration` and expects them to be the given
        /// timers.
        ///
        /// Like `trigger_timers_and_expect_unordered`, except that timers will
        /// only be triggered for `duration` (inclusive).
        fn trigger_timers_for_and_expect<I: IntoIterator<Item = Id>, F: FnMut(&mut Self, Id)>(
            &mut self,
            duration: Duration,
            timers: I,
            f: F,
        ) where
            Id: Debug + Hash + Eq,
        {
            let instant = self.as_mut().now().saturating_add(duration);
            self.trigger_timers_until_and_expect_unordered(instant, timers, f);
        }
    }

    pub(crate) fn handle_timer_helper_with_sc_ref_mut<
        'a,
        Id,
        SC,
        C,
        F: FnMut(&mut SC, &mut C, Id) + 'a,
    >(
        sync_ctx: &'a mut SC,
        mut f: F,
    ) -> impl FnMut(&mut C, Id) + 'a {
        move |non_sync_ctx, id| f(sync_ctx, non_sync_ctx, id)
    }

    pub(crate) fn handle_timer_helper_with_sc_ref<'a, Id, SC, C, F: FnMut(&SC, &mut C, Id) + 'a>(
        sync_ctx: &'a SC,
        mut f: F,
    ) -> impl FnMut(&mut C, Id) + 'a {
        move |non_sync_ctx, id| f(sync_ctx, non_sync_ctx, id)
    }

    impl<Id: Clone, T: AsMut<DummyTimerCtx<Id>>> DummyTimerCtxExt<Id> for T {}

    /// A dummy [`FrameContext`].
    pub struct DummyFrameCtx<Meta> {
        frames: Vec<(Meta, Vec<u8>)>,
        should_error_for_frame: Option<Box<dyn Fn(&Meta) -> bool>>,
    }

    impl<Meta> DummyFrameCtx<Meta> {
        /// Closure which can decide to cause an error to be thrown when
        /// handling a frame, based on the metadata.
        pub fn set_should_error_for_frame<F: Fn(&Meta) -> bool + 'static>(&mut self, f: F) {
            self.should_error_for_frame = Some(Box::new(f));
        }
    }

    impl<Meta> Default for DummyFrameCtx<Meta> {
        fn default() -> DummyFrameCtx<Meta> {
            DummyFrameCtx { frames: Vec::new(), should_error_for_frame: None }
        }
    }

    impl<Meta> DummyFrameCtx<Meta> {
        /// Take all frames sent so far.
        pub(crate) fn take_frames(&mut self) -> Vec<(Meta, Vec<u8>)> {
            core::mem::take(&mut self.frames)
        }

        /// Get the frames sent so far.
        pub(crate) fn frames(&self) -> &[(Meta, Vec<u8>)] {
            self.frames.as_slice()
        }
    }

    impl<C, B: BufferMut, Meta> FrameContext<C, B, Meta> for DummyFrameCtx<Meta> {
        fn send_frame<S: Serializer<Buffer = B>>(
            &mut self,
            _ctx: &mut C,
            metadata: Meta,
            frame: S,
        ) -> Result<(), S> {
            if let Some(should_error_for_frame) = &self.should_error_for_frame {
                if should_error_for_frame(&metadata) {
                    return Err(frame);
                }
            }

            let buffer = frame.serialize_vec_outer().map_err(|(_err, s)| s)?;
            self.frames.push((metadata, buffer.as_ref().to_vec()));
            Ok(())
        }
    }

    /// A dummy [`EventContext`].
    pub struct DummyEventCtx<E: Debug> {
        events: Vec<E>,
        must_watch_all_events: bool,
    }

    impl<E: Debug> EventContext<E> for DummyEventCtx<E> {
        fn on_event(&mut self, event: E) {
            self.events.push(event)
        }
    }

    impl<E: Debug> Drop for DummyEventCtx<E> {
        fn drop(&mut self) {
            if self.must_watch_all_events {
                assert!(
                    self.events.is_empty(),
                    "dropped context with unacknowledged events: {:?}",
                    self.events
                );
            }
        }
    }

    impl<E: Debug> Default for DummyEventCtx<E> {
        fn default() -> Self {
            Self { events: Default::default(), must_watch_all_events: false }
        }
    }

    impl<E: Debug> DummyEventCtx<E> {
        pub fn take(&mut self) -> Vec<E> {
            // Any client that calls `take()` is opting into watching events
            // and must watch them all.
            self.must_watch_all_events = true;
            core::mem::take(&mut self.events)
        }
    }

    /// A dummy [`CounterContext`].
    #[derive(Default)]
    pub struct DummyCounterCtx {
        counters: HashMap<&'static str, usize>,
    }

    impl DummyCounterCtx {
        pub(crate) fn get_counter_val(&self, key: &str) -> usize {
            *self.counters.get(key).unwrap_or(&0)
        }
    }

    impl CounterContext for DummyCounterCtx {
        fn increment_counter(&mut self, key: &'static str) {
            let val = self.counters.entry(key).or_insert(0);
            *val += 1;
        }
    }

    impl<T: AsMut<DummyCounterCtx>> CounterContext for T {
        fn increment_counter(&mut self, key: &'static str) {
            self.as_mut().increment_counter(key);
        }
    }

    /// A test helper used to provide an implementation of a non-synchronized
    /// context.
    pub(crate) struct DummyNonSyncCtx<TimerId, Event: Debug, State> {
        rng: FakeCryptoRng<XorShiftRng>,
        timers: DummyTimerCtx<TimerId>,
        events: DummyEventCtx<Event>,
        frames: DummyFrameCtx<DeviceId>,
        counters: DummyCounterCtx,
        state: State,
    }

    impl<TimerId, Event: Debug, State: Default> Default for DummyNonSyncCtx<TimerId, Event, State> {
        fn default() -> Self {
            Self {
                rng: FakeCryptoRng::new_xorshift(0),
                timers: DummyTimerCtx::default(),
                events: DummyEventCtx::default(),
                frames: DummyFrameCtx::default(),
                counters: DummyCounterCtx::default(),
                state: Default::default(),
            }
        }
    }

    impl<TimerId, Event: Debug, State> DummyNonSyncCtx<TimerId, Event, State> {
        /// Seed the testing RNG with a specific value.
        pub(crate) fn seed_rng(&mut self, seed: u128) {
            self.rng = FakeCryptoRng::new_xorshift(seed);
        }

        /// Move the clock forward by the given duration without firing any
        /// timers.
        ///
        /// If any timers are scheduled to fire in the given duration, future
        /// use of this `DummySyncCtx` may have surprising or buggy behavior.
        pub(crate) fn sleep_skip_timers(&mut self, duration: Duration) {
            self.timers.instant.sleep(duration);
        }

        pub(crate) fn timer_ctx(&self) -> &DummyTimerCtx<TimerId> {
            &self.timers
        }

        pub(crate) fn take_events(&mut self) -> Vec<Event> {
            self.events.take()
        }

        pub(crate) fn frame_ctx(&self) -> &DummyFrameCtx<DeviceId> {
            &self.frames
        }

        pub(crate) fn frame_ctx_mut(&mut self) -> &mut DummyFrameCtx<DeviceId> {
            &mut self.frames
        }

        pub(crate) fn state(&self) -> &State {
            &self.state
        }

        pub(crate) fn state_mut(&mut self) -> &mut State {
            &mut self.state
        }

        pub(crate) fn counter_ctx(&self) -> &DummyCounterCtx {
            &self.counters
        }
    }

    impl<TimerId, Event: Debug, State> RngContext for DummyNonSyncCtx<TimerId, Event, State> {
        type Rng = FakeCryptoRng<XorShiftRng>;

        fn rng(&self) -> &Self::Rng {
            &self.rng
        }

        fn rng_mut(&mut self) -> &mut Self::Rng {
            &mut self.rng
        }
    }

    impl<Id, Event: Debug, State> AsRef<DummyInstantCtx> for DummyNonSyncCtx<Id, Event, State> {
        fn as_ref(&self) -> &DummyInstantCtx {
            self.timers.as_ref()
        }
    }

    impl<Id, Event: Debug, State> AsMut<DummyCounterCtx> for DummyNonSyncCtx<Id, Event, State> {
        fn as_mut(&mut self) -> &mut DummyCounterCtx {
            &mut self.counters
        }
    }

    impl<Id, Event: Debug, State> AsRef<DummyTimerCtx<Id>> for DummyNonSyncCtx<Id, Event, State> {
        fn as_ref(&self) -> &DummyTimerCtx<Id> {
            &self.timers
        }
    }

    impl<Id, Event: Debug, State> AsMut<DummyTimerCtx<Id>> for DummyNonSyncCtx<Id, Event, State> {
        fn as_mut(&mut self) -> &mut DummyTimerCtx<Id> {
            &mut self.timers
        }
    }

    impl<Id: Debug + PartialEq, Event: Debug, State> TimerContext<Id>
        for DummyNonSyncCtx<Id, Event, State>
    {
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

    impl<Id, Event: Debug, State> EventContext<Event> for DummyNonSyncCtx<Id, Event, State> {
        fn on_event(&mut self, event: Event) {
            self.events.on_event(event)
        }
    }

    #[derive(Default)]
    pub(crate) struct DummyCtxWithSyncCtx<SC, TimerId, Event: Debug, NonSyncCtxState> {
        pub(crate) sync_ctx: SC,
        pub(crate) non_sync_ctx: DummyNonSyncCtx<TimerId, Event, NonSyncCtxState>,
    }

    pub(crate) type DummyCtx<S, TimerId, Meta, Event, DeviceId, NonSyncCtxState> =
        DummyCtxWithSyncCtx<DummySyncCtx<S, Meta, DeviceId>, TimerId, Event, NonSyncCtxState>;

    impl<S, Id, Meta, Event: Debug, DeviceId, NonSyncCtxState> DummyNetworkContext
        for DummyCtx<S, Id, Meta, Event, DeviceId, NonSyncCtxState>
    {
        type TimerId = Id;
        type SendMeta = Meta;
    }

    impl<SC, Id, Event: Debug, NonSyncCtxState> AsRef<DummyInstantCtx>
        for DummyCtxWithSyncCtx<SC, Id, Event, NonSyncCtxState>
    {
        fn as_ref(&self) -> &DummyInstantCtx {
            self.non_sync_ctx.timers.as_ref()
        }
    }

    impl<SC, Id, Event: Debug, NonSyncCtxState> AsRef<DummyTimerCtx<Id>>
        for DummyCtxWithSyncCtx<SC, Id, Event, NonSyncCtxState>
    {
        fn as_ref(&self) -> &DummyTimerCtx<Id> {
            &self.non_sync_ctx.timers
        }
    }

    impl<SC, Id, Event: Debug, NonSyncCtxState> AsMut<DummyTimerCtx<Id>>
        for DummyCtxWithSyncCtx<SC, Id, Event, NonSyncCtxState>
    {
        fn as_mut(&mut self) -> &mut DummyTimerCtx<Id> {
            &mut self.non_sync_ctx.timers
        }
    }

    impl<S, Id, Meta, Event: Debug, DeviceId, NonSyncCtxState> AsMut<DummyFrameCtx<Meta>>
        for DummyCtx<S, Id, Meta, Event, DeviceId, NonSyncCtxState>
    {
        fn as_mut(&mut self) -> &mut DummyFrameCtx<Meta> {
            &mut self.sync_ctx.frames
        }
    }

    impl<S, Id, Meta, Event: Debug, DeviceId, NonSyncCtxState: Default>
        DummyCtx<S, Id, Meta, Event, DeviceId, NonSyncCtxState>
    {
        /// Constructs a `DummyCtx` with the given state and default
        /// `DummyTimerCtx`, and `DummyFrameCtx`.
        pub(crate) fn with_state(state: S) -> Self {
            DummyCtx {
                sync_ctx: DummySyncCtx {
                    state,
                    frames: DummyFrameCtx::default(),
                    _devices_marker: PhantomData,
                },
                non_sync_ctx: DummyNonSyncCtx::default(),
            }
        }
    }

    impl<SC, Id, Event: Debug, NonSyncCtxState: Default>
        DummyCtxWithSyncCtx<SC, Id, Event, NonSyncCtxState>
    {
        pub(crate) fn with_sync_ctx(sync_ctx: SC) -> Self {
            DummyCtxWithSyncCtx { sync_ctx, non_sync_ctx: DummyNonSyncCtx::default() }
        }
    }

    #[derive(Default)]
    pub(crate) struct WrappedDummySyncCtx<Outer, S, Meta, DeviceId> {
        pub(crate) inner: DummySyncCtx<S, Meta, DeviceId>,
        pub(crate) outer: Outer,
    }

    impl<Outer, S, Meta, DeviceId> WrappedDummySyncCtx<Outer, S, Meta, DeviceId> {
        pub(crate) fn with_inner_and_outer_state(inner: S, outer: Outer) -> Self {
            Self { inner: DummySyncCtx::with_state(inner), outer }
        }
    }

    impl<Outer, S, Meta, DeviceId> AsRef<DummySyncCtx<S, Meta, DeviceId>>
        for WrappedDummySyncCtx<Outer, S, Meta, DeviceId>
    {
        fn as_ref(&self) -> &DummySyncCtx<S, Meta, DeviceId> {
            &self.inner
        }
    }

    impl<Outer, S, Meta, DeviceId> AsMut<DummySyncCtx<S, Meta, DeviceId>>
        for WrappedDummySyncCtx<Outer, S, Meta, DeviceId>
    {
        fn as_mut(&mut self) -> &mut DummySyncCtx<S, Meta, DeviceId> {
            &mut self.inner
        }
    }

    /// A test helper used to provide an implementation of a synchronized
    /// context.
    pub(crate) struct DummySyncCtx<S, Meta, DeviceId> {
        state: S,
        frames: DummyFrameCtx<Meta>,
        _devices_marker: PhantomData<DeviceId>,
    }

    impl<S, Meta, DeviceId> AsRef<DummySyncCtx<S, Meta, DeviceId>> for DummySyncCtx<S, Meta, DeviceId> {
        fn as_ref(&self) -> &DummySyncCtx<S, Meta, DeviceId> {
            self
        }
    }

    impl<S, Meta, DeviceId> AsMut<DummySyncCtx<S, Meta, DeviceId>> for DummySyncCtx<S, Meta, DeviceId> {
        fn as_mut(&mut self) -> &mut DummySyncCtx<S, Meta, DeviceId> {
            self
        }
    }

    impl<S: Default, Meta, DeviceId> Default for DummySyncCtx<S, Meta, DeviceId> {
        fn default() -> DummySyncCtx<S, Meta, DeviceId> {
            DummySyncCtx::with_state(S::default())
        }
    }

    impl<S, Meta, DeviceId> DummySyncCtx<S, Meta, DeviceId> {
        /// Constructs a `DummySyncCtx` with the given state and default
        /// `DummyTimerCtx`, and `DummyFrameCtx`.
        pub(crate) fn with_state(state: S) -> Self {
            DummySyncCtx { state, frames: DummyFrameCtx::default(), _devices_marker: PhantomData }
        }

        /// Get an immutable reference to the inner state.
        ///
        /// This method is provided instead of an [`AsRef`] impl to avoid
        /// conflicting with user-provided implementations of `AsRef<T> for
        /// DummyCtx<S, Id, Meta, Event>` for other types, `T`. It is named
        /// `get_ref` instead of `as_ref` so that programmer doesn't need to
        /// specify which `as_ref` method is intended.
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

        /// Take the list of frames sent so far.
        pub(crate) fn take_frames(&mut self) -> Vec<(Meta, Vec<u8>)> {
            self.frames.take_frames()
        }
    }

    impl<S, Meta, DeviceId> AsMut<DummyFrameCtx<Meta>> for DummySyncCtx<S, Meta, DeviceId> {
        fn as_mut(&mut self) -> &mut DummyFrameCtx<Meta> {
            &mut self.frames
        }
    }

    impl<Outer, S, Meta, DeviceId> AsMut<DummyFrameCtx<Meta>>
        for WrappedDummySyncCtx<Outer, S, Meta, DeviceId>
    {
        fn as_mut(&mut self) -> &mut DummyFrameCtx<Meta> {
            &mut self.inner.frames
        }
    }

    impl<B: BufferMut, S, Id, Meta, Event: Debug, DeviceId, NonSyncCtxState>
        FrameContext<DummyNonSyncCtx<Id, Event, NonSyncCtxState>, B, Meta>
        for DummySyncCtx<S, Meta, DeviceId>
    {
        fn send_frame<SS: Serializer<Buffer = B>>(
            &mut self,
            ctx: &mut DummyNonSyncCtx<Id, Event, NonSyncCtxState>,
            metadata: Meta,
            frame: SS,
        ) -> Result<(), SS> {
            self.frames.send_frame(ctx, metadata, frame)
        }
    }

    #[derive(Debug)]
    pub(crate) struct PendingFrameData<CtxId, Meta> {
        pub(crate) dst_context: CtxId,
        pub(crate) meta: Meta,
        pub(crate) frame: Vec<u8>,
    }

    pub(crate) type PendingFrame<CtxId, Meta> = InstantAndData<PendingFrameData<CtxId, Meta>>;

    /// A dummy network, composed of many `DummySyncCtx`s.
    ///
    /// Provides a utility to have many contexts keyed by `CtxId` that can
    /// exchange frames.
    pub(crate) struct DummyNetwork<CtxId, RecvMeta, Ctx: DummyNetworkContext, Links>
    where
        Links: DummyNetworkLinks<Ctx::SendMeta, RecvMeta, CtxId>,
    {
        contexts: HashMap<CtxId, Ctx>,
        current_time: DummyInstant,
        pending_frames: BinaryHeap<PendingFrame<CtxId, RecvMeta>>,
        links: Links,
    }

    /// A context which can be used with a [`DummyNetwork`].
    pub(crate) trait DummyNetworkContext {
        /// The type of timer IDs installed by this context.
        type TimerId;
        /// The type of metadata associated with frames sent by this context.
        type SendMeta;
    }

    /// A set of links in a `DummyNetwork`.
    ///
    /// A `DummyNetworkLinks` represents the set of links in a `DummyNetwork`.
    /// It exposes the link information by providing the ability to map from a
    /// frame's sending metadata - including its context, local state, and
    /// `SendMeta` - to the set of appropriate receivers, each represented by
    /// a context ID, receive metadata, and latency.
    pub(crate) trait DummyNetworkLinks<SendMeta, RecvMeta, CtxId> {
        fn map_link(&self, ctx: CtxId, meta: SendMeta) -> Vec<(CtxId, RecvMeta, Option<Duration>)>;
    }

    impl<
            SendMeta,
            RecvMeta,
            CtxId,
            F: Fn(CtxId, SendMeta) -> Vec<(CtxId, RecvMeta, Option<Duration>)>,
        > DummyNetworkLinks<SendMeta, RecvMeta, CtxId> for F
    {
        fn map_link(&self, ctx: CtxId, meta: SendMeta) -> Vec<(CtxId, RecvMeta, Option<Duration>)> {
            (self)(ctx, meta)
        }
    }

    /// The result of a single step in a `DummyNetwork`
    #[derive(Debug)]
    pub(crate) struct StepResult {
        pub(crate) timers_fired: usize,
        pub(crate) frames_sent: usize,
    }

    impl StepResult {
        fn new(timers_fired: usize, frames_sent: usize) -> Self {
            Self { timers_fired, frames_sent }
        }

        fn new_idle() -> Self {
            Self::new(0, 0)
        }

        /// Returns `true` if the last step did not perform any operations.
        pub(crate) fn is_idle(&self) -> bool {
            return self.timers_fired == 0 && self.frames_sent == 0;
        }
    }

    /// Error type that marks that one of the `run_until` family of functions
    /// reached a maximum number of iterations.
    #[derive(Debug)]
    pub(crate) struct LoopLimitReachedError;

    impl<CtxId, RecvMeta, Ctx, Links> DummyNetwork<CtxId, RecvMeta, Ctx, Links>
    where
        CtxId: Eq + Hash + Copy + Debug,
        Ctx: DummyNetworkContext
            + AsRef<DummyTimerCtx<Ctx::TimerId>>
            + AsMut<DummyTimerCtx<Ctx::TimerId>>
            + AsMut<DummyFrameCtx<Ctx::SendMeta>>,
        Ctx::TimerId: Copy,
        Links: DummyNetworkLinks<Ctx::SendMeta, RecvMeta, CtxId>,
    {
        /// Creates a new `DummyNetwork`.
        ///
        /// Creates a new `DummyNetwork` with the collection of `DummySyncCtx`s in
        /// `contexts`. `Ctx`s are named by type parameter `CtxId`.
        ///
        /// # Panics
        ///
        /// Calls to `new` will panic if given a `DummySyncCtx` with timer events.
        /// `DummySyncCtx`s given to `DummyNetwork` **must not** have any timer
        /// events already attached to them, because `DummyNetwork` maintains
        /// all the internal timers in dispatchers in sync to enable synchronous
        /// simulation steps.
        pub(crate) fn new<I: IntoIterator<Item = (CtxId, Ctx)>>(contexts: I, links: Links) -> Self {
            let mut contexts = contexts.into_iter().collect::<HashMap<_, _>>();
            // Take the current time to be the latest of the times of any of the
            // contexts. This ensures that no context has state which is based
            // on having observed a time in the future, which could cause bugs.
            // For any contexts which have a time further in the past, it will
            // appear as though time has jumped forwards, but that's fine. The
            // only way that this could be a problem would be if a timer were
            // installed which should have fired in the interim (code might
            // become buggy in this case). However, we assert below that no
            // timers are installed.
            let latest_time = contexts
                .iter()
                .map(|(_, ctx)| ctx.as_ref().instant.time)
                .max()
                // If `max` returns `None`, it means that we were called with no
                // contexts. That's kind of silly, but whatever - arbitrarily
                // choose the current time as the epoch.
                .unwrap_or(DummyInstant::default());

            assert!(
                !contexts.iter().any(|(_, ctx)| { !ctx.as_ref().timers.is_empty() }),
                "can't start network with contexts that already have timers set"
            );

            // Synchronize all contexts' current time to the latest time of any
            // of the contexts. See comment above for more details.
            for (_, ctx) in contexts.iter_mut() {
                AsMut::<DummyTimerCtx<_>>::as_mut(ctx).instant.time = latest_time;
            }

            Self { contexts, current_time: latest_time, pending_frames: BinaryHeap::new(), links }
        }

        /// Retrieves a `DummySyncCtx` named `context`.
        pub(crate) fn context<K: Into<CtxId>>(&mut self, context: K) -> &mut Ctx {
            self.contexts.get_mut(&context.into()).unwrap()
        }

        /// Iterates over pending frames in an arbitrary order.
        pub(crate) fn iter_pending_frames(
            &self,
        ) -> impl Iterator<Item = &PendingFrame<CtxId, RecvMeta>> {
            self.pending_frames.iter()
        }

        /// Drops all pending frames; they will not be delivered.
        pub(crate) fn drop_pending_frames(&mut self) {
            self.pending_frames.clear();
        }

        /// Performs a single step in network simulation.
        ///
        /// `step` performs a single logical step in the collection of `Ctx`s
        /// held by this `DummyNetwork`. A single step consists of the following
        /// operations:
        ///
        /// - All pending frames, kept in each `DummySyncCtx`, are mapped to their
        ///   destination context/device pairs and moved to an internal
        ///   collection of pending frames.
        /// - The collection of pending timers and scheduled frames is inspected
        ///   and a simulation time step is retrieved, which will cause a next
        ///   event to trigger. The simulation time is updated to the new time.
        /// - All scheduled frames whose deadline is less than or equal to the
        ///   new simulation time are sent to their destinations, handled using
        ///   `handle_frame`.
        /// - All timer events whose deadline is less than or equal to the new
        ///   simulation time are fired, handled using `handle_timer`.
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
            FH: FnMut(&mut Ctx, RecvMeta, Buf<Vec<u8>>),
            FT: FnMut(&mut Ctx, &mut (), Ctx::TimerId),
        >(
            &mut self,
            mut handle_frame: FH,
            mut handle_timer: FT,
        ) -> StepResult
        where
            Ctx::TimerId: core::fmt::Debug,
        {
            self.collect_frames();

            let next_step = if let Some(t) = self.next_step() {
                t
            } else {
                return StepResult::new_idle();
            };

            // This assertion holds the contract that `next_step` does not
            // return a time in the past.
            assert!(next_step >= self.current_time);
            let mut ret = StepResult::new(0, 0);
            // Move time forward:
            self.current_time = next_step;
            for (_, ctx) in self.contexts.iter_mut() {
                AsMut::<DummyTimerCtx<_>>::as_mut(ctx).instant.time = next_step;
            }

            // Dispatch all pending frames:
            while let Some(InstantAndData(t, _)) = self.pending_frames.peek() {
                // TODO(https://github.com/rust-lang/rust/issues/53667): Remove
                // this break once let_chains is stable.
                if *t > self.current_time {
                    break;
                }
                // We can unwrap because we just peeked.
                let frame = self.pending_frames.pop().unwrap().1;
                handle_frame(
                    self.context(frame.dst_context),
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
                let mut timers = Vec::<Ctx::TimerId>::new();
                while let Some(InstantAndData(t, id)) = ctx.as_ref().timers.peek() {
                    // TODO(https://github.com/rust-lang/rust/issues/53667):
                    // Remove this break once let_chains is stable.
                    if *t > ctx.as_ref().now() {
                        break;
                    }
                    timers.push(*id);
                    assert_ne!(AsMut::<DummyTimerCtx<_>>::as_mut(ctx).timers.pop(), None);
                }

                for t in timers {
                    handle_timer(ctx, &mut (), t);
                    ret.timers_fired += 1;
                }
            }

            ret
        }

        /// Runs the network until it is starved of events.
        ///
        /// # Panics
        ///
        /// Panics if 1,000,000 steps are performed without becoming idle.
        /// Also panics under the same conditions as [`step`].
        pub(crate) fn run_until_idle<
            FH: FnMut(&mut Ctx, RecvMeta, Buf<Vec<u8>>),
            FT: FnMut(&mut Ctx, &mut (), Ctx::TimerId),
        >(
            &mut self,
            mut handle_frame: FH,
            mut handle_timer: FT,
        ) where
            Ctx::TimerId: core::fmt::Debug,
        {
            for _ in 0..1_000_000 {
                if self.step(&mut handle_frame, &mut handle_timer).is_idle() {
                    return;
                }
            }
            panic!("DummyNetwork seems to have gotten stuck in a loop.");
        }

        /// Collects all queued frames.
        ///
        /// Collects all pending frames and schedules them for delivery to the
        /// destination context/device based on the result of `links`. The
        /// collected frames are queued for dispatching in the `DummyNetwork`,
        /// ordered by their scheduled delivery time given by the latency result
        /// provided by `links`.
        pub(crate) fn collect_frames(&mut self) {
            let all_frames: Vec<(CtxId, Vec<(Ctx::SendMeta, Vec<u8>)>)> = self
                .contexts
                .iter_mut()
                .filter_map(|(n, ctx)| {
                    let ctx: &mut DummyFrameCtx<_> = ctx.as_mut();
                    if ctx.frames.is_empty() {
                        None
                    } else {
                        Some((n.clone(), ctx.frames.drain(..).collect()))
                    }
                })
                .collect();

            for (src_context, frames) in all_frames.into_iter() {
                for (send_meta, frame) in frames.into_iter() {
                    for (dst_context, recv_meta, latency) in
                        self.links.map_link(src_context, send_meta)
                    {
                        self.pending_frames.push(PendingFrame::new(
                            self.current_time + latency.unwrap_or(Duration::from_millis(0)),
                            PendingFrameData { frame: frame.clone(), dst_context, meta: recv_meta },
                        ));
                    }
                }
            }
        }

        /// Calculates the next `DummyInstant` when events are available.
        ///
        /// Returns the smallest `DummyInstant` greater than or equal to the
        /// current time for which an event is available. If no events are
        /// available, returns `None`.
        fn next_step(&self) -> Option<DummyInstant> {
            // Get earliest timer in all contexts.
            let next_timer = self
                .contexts
                .iter()
                .filter_map(|(_, ctx)| match ctx.as_ref().timers.peek() {
                    Some(tmr) => Some(tmr.0),
                    None => None,
                })
                .min();
            // Get the instant for the next packet.
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

    impl<CtxId, Links> DummyNetwork<CtxId, DeviceId, crate::testutil::DummyCtx, Links>
    where
        CtxId: Eq + Hash + Copy + Debug,
        Links: DummyNetworkLinks<DeviceId, DeviceId, CtxId>,
    {
        pub(crate) fn with_context<
            K: Into<CtxId>,
            O,
            F: FnOnce(&mut crate::testutil::DummyCtx) -> O,
        >(
            &mut self,
            context: K,
            f: F,
        ) -> O {
            f(self.context(context))
        }

        /// Retrieves a `DummySyncCtx` named `context`.
        pub(crate) fn sync_ctx<K: Into<CtxId>>(
            &mut self,
            context: K,
        ) -> &mut crate::testutil::DummySyncCtx {
            let crate::testutil::DummyCtx { sync_ctx, non_sync_ctx: _ } = self.context(context);
            sync_ctx
        }

        /// Retrieves a `DummyNonSyncCtx` named `context`.
        pub(crate) fn non_sync_ctx<K: Into<CtxId>>(
            &mut self,
            context: K,
        ) -> &mut crate::testutil::DummyNonSyncCtx {
            let crate::testutil::DummyCtx { sync_ctx: _, non_sync_ctx } = self.context(context);
            non_sync_ctx
        }
    }

    impl<RecvMeta, SC, Id, Event, CtxId, Links, NonSyncCtxState>
        DummyNetwork<
            CtxId,
            RecvMeta,
            DummyCtxWithSyncCtx<SC, Id, Event, NonSyncCtxState>,
            Links,
        >
    where
        Id: Copy,
        Event: Debug,
        CtxId: Eq + Hash + Copy + Debug,
        Links: DummyNetworkLinks<<DummyCtxWithSyncCtx<SC, Id, Event, NonSyncCtxState> as DummyNetworkContext>::SendMeta, RecvMeta, CtxId>,
        DummyCtxWithSyncCtx<SC, Id, Event, NonSyncCtxState>: DummyNetworkContext<TimerId=Id> + AsMut<DummyFrameCtx<<DummyCtxWithSyncCtx<SC, Id, Event, NonSyncCtxState> as DummyNetworkContext>::SendMeta>>
    {
        pub(crate) fn with_context<
            K: Into<CtxId>,
            O,
            F: FnOnce(&mut DummyCtxWithSyncCtx<SC, Id, Event, NonSyncCtxState>) -> O,
        >(
            &mut self,
            context: K,
            f: F,
        ) -> O {
            f(self.context(context))
        }

        /// Retrieves a `DummySyncCtx` named `context`.
        pub(crate) fn sync_ctx<K: Into<CtxId>>(
            &mut self,
            context: K,
        ) -> &mut SC {
            let DummyCtxWithSyncCtx { sync_ctx, non_sync_ctx: _ } = self.context(context);
            sync_ctx
        }
    }

    /// Creates a new [`DummyNetwork`] of legacy [`Ctx`] contexts in a simple
    /// two-host configuration.
    ///
    /// Two hosts are created with the given names. Packets emitted by one
    /// arrive at the other and vice-versa.
    pub(crate) fn new_legacy_simple_dummy_network<CtxId: Copy + Debug + Hash + Eq>(
        a_id: CtxId,
        a: crate::testutil::DummyCtx,
        b_id: CtxId,
        b: crate::testutil::DummyCtx,
    ) -> DummyNetwork<
        CtxId,
        DeviceId,
        crate::testutil::DummyCtx,
        impl DummyNetworkLinks<DeviceId, DeviceId, CtxId>,
    > {
        let contexts = vec![(a_id, a), (b_id, b)].into_iter();
        let device_id = DeviceId::new_ethernet(0);
        DummyNetwork::new(contexts, move |net, _device_id: DeviceId| {
            if net == a_id {
                vec![(b_id, device_id, None)]
            } else {
                vec![(a_id, device_id, None)]
            }
        })
    }

    mod tests {
        use crate::ip::testutil::DummyDeviceId;

        use super::*;

        const ONE_SEC: Duration = Duration::from_secs(1);
        const ONE_SEC_INSTANT: DummyInstant = DummyInstant { offset: ONE_SEC };

        #[test]
        fn test_instant_and_data() {
            // Verify implementation of InstantAndData to be used as a complex
            // type in a BinaryHeap.
            let mut heap = BinaryHeap::<InstantAndData<usize>>::new();
            let now = DummyInstant::default();

            fn new_data(time: DummyInstant, id: usize) -> InstantAndData<usize> {
                InstantAndData::new(time, id)
            }

            heap.push(new_data(now + Duration::from_secs(1), 1));
            heap.push(new_data(now + Duration::from_secs(2), 2));

            // Earlier timer is popped first.
            assert_eq!(heap.pop().unwrap().1, 1);
            assert_eq!(heap.pop().unwrap().1, 2);
            assert_eq!(heap.pop(), None);

            heap.push(new_data(now + Duration::from_secs(1), 1));
            heap.push(new_data(now + Duration::from_secs(1), 1));

            // Can pop twice with identical data.
            assert_eq!(heap.pop().unwrap().1, 1);
            assert_eq!(heap.pop().unwrap().1, 1);
            assert_eq!(heap.pop(), None);
        }

        #[test]
        fn test_dummy_timer_context() {
            // An implementation of `TimerContext` that uses `usize` timer IDs
            // and stores every timer in a `Vec`.
            impl<M, E: Debug, D, S> TimerHandler<DummyNonSyncCtx<usize, E, S>, usize>
                for DummySyncCtx<Vec<(usize, DummyInstant)>, M, D>
            {
                fn handle_timer(&mut self, ctx: &mut DummyNonSyncCtx<usize, E, S>, id: usize) {
                    let now = ctx.now();
                    self.get_mut().push((id, now));
                }
            }

            let new_ctx = || {
                DummyCtx::<Vec<(usize, DummyInstant)>, usize, (), (), DummyDeviceId, ()>::default()
            };

            let DummyCtx { mut sync_ctx, mut non_sync_ctx } = new_ctx();

            // When no timers are installed, `trigger_next_timer` should return
            // `false`.
            assert_eq!(
                non_sync_ctx.trigger_next_timer(&mut sync_ctx, TimerHandler::handle_timer),
                None
            );
            assert_eq!(sync_ctx.get_ref().as_slice(), []);

            // When one timer is installed, it should be triggered.
            let DummyCtx { mut sync_ctx, mut non_sync_ctx } = new_ctx();

            // No timer with id `0` exists yet.
            assert_eq!(non_sync_ctx.scheduled_instant(0), None);

            assert_eq!(non_sync_ctx.schedule_timer(ONE_SEC, 0), None);

            // Timer with id `0` scheduled to execute at `ONE_SEC_INSTANT`.
            assert_eq!(non_sync_ctx.scheduled_instant(0).unwrap(), ONE_SEC_INSTANT);

            assert_eq!(
                non_sync_ctx.trigger_next_timer(&mut sync_ctx, TimerHandler::handle_timer),
                Some(0)
            );
            assert_eq!(sync_ctx.get_ref().as_slice(), [(0, ONE_SEC_INSTANT)]);

            // After the timer fires, it should not still be scheduled at some
            // instant.
            assert_eq!(non_sync_ctx.scheduled_instant(0), None);

            // The time should have been advanced.
            assert_eq!(non_sync_ctx.now(), ONE_SEC_INSTANT);

            // Once it's been triggered, it should be canceled and not
            // triggerable again.
            let DummyCtx { mut sync_ctx, mut non_sync_ctx } = new_ctx();
            assert_eq!(
                non_sync_ctx.trigger_next_timer(&mut sync_ctx, TimerHandler::handle_timer),
                None
            );
            assert_eq!(sync_ctx.get_ref().as_slice(), []);

            // If we schedule a timer but then cancel it, it shouldn't fire.
            let DummyCtx { mut sync_ctx, mut non_sync_ctx } = new_ctx();

            assert_eq!(non_sync_ctx.schedule_timer(ONE_SEC, 0), None);
            assert_eq!(non_sync_ctx.cancel_timer(0), Some(ONE_SEC_INSTANT));
            assert_eq!(
                non_sync_ctx.trigger_next_timer(&mut sync_ctx, TimerHandler::handle_timer),
                None
            );
            assert_eq!(sync_ctx.get_ref().as_slice(), []);

            // If we schedule a timer but then schedule the same ID again, the
            // second timer should overwrite the first one.
            let DummyCtx { sync_ctx: _, mut non_sync_ctx } = new_ctx();
            assert_eq!(non_sync_ctx.schedule_timer(Duration::from_secs(0), 0), None);
            assert_eq!(
                non_sync_ctx.schedule_timer(ONE_SEC, 0),
                Some(Duration::from_secs(0).into())
            );
            assert_eq!(non_sync_ctx.cancel_timer(0), Some(ONE_SEC_INSTANT));

            // If we schedule three timers and then run `trigger_timers_until`
            // with the appropriate value, only two of them should fire.
            let DummyCtx { mut sync_ctx, mut non_sync_ctx } = new_ctx();
            assert_eq!(non_sync_ctx.schedule_timer(Duration::from_secs(0), 0), None,);
            assert_eq!(non_sync_ctx.schedule_timer(Duration::from_secs(1), 1), None,);
            assert_eq!(non_sync_ctx.schedule_timer(Duration::from_secs(2), 2), None,);
            assert_eq!(
                non_sync_ctx.trigger_timers_until_instant(
                    ONE_SEC_INSTANT,
                    handle_timer_helper_with_sc_ref_mut(&mut sync_ctx, TimerHandler::handle_timer)
                ),
                vec![0, 1],
            );

            // The first two timers should have fired.
            assert_eq!(
                sync_ctx.get_ref().as_slice(),
                [(0, DummyInstant::from(Duration::from_secs(0))), (1, ONE_SEC_INSTANT)]
            );

            // They should be canceled now.
            assert_eq!(non_sync_ctx.cancel_timer(0), None);
            assert_eq!(non_sync_ctx.cancel_timer(1), None);

            // The clock should have been updated.
            assert_eq!(non_sync_ctx.now(), ONE_SEC_INSTANT);

            // The last timer should not have fired.
            assert_eq!(
                non_sync_ctx.cancel_timer(2),
                Some(DummyInstant::from(Duration::from_secs(2)))
            );
        }

        #[test]
        fn test_trigger_timers_until_and_expect_unordered() {
            // If the requested instant does not coincide with a timer trigger
            // point, the time should still be advanced.
            let DummyCtx { mut sync_ctx, mut non_sync_ctx } =
                DummyCtx::<Vec<(usize, DummyInstant)>, usize, (), (), DummyDeviceId, ()>::default();
            assert_eq!(non_sync_ctx.schedule_timer(Duration::from_secs(0), 0), None);
            assert_eq!(non_sync_ctx.schedule_timer(Duration::from_secs(2), 1), None);
            non_sync_ctx.trigger_timers_until_and_expect_unordered(
                ONE_SEC_INSTANT,
                vec![0],
                |non_sync_ctx, id| TimerHandler::handle_timer(&mut sync_ctx, non_sync_ctx, id),
            );
            assert_eq!(non_sync_ctx.now(), ONE_SEC_INSTANT);
        }
    }
}
