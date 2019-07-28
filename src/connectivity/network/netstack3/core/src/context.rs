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
