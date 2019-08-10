// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::hash_map::Entry;
use std::collections::HashMap;
use std::hash::Hash;
use std::sync::Arc;
use std::sync::Mutex;

use fuchsia_async as fasync;
use futures::channel::mpsc::UnboundedSender;
use futures::future::{AbortHandle, Abortable};
use futures::TryFutureExt;

use super::ZxTime;

/// A possible timer event that may be fulfilled by calling
/// [`TimerDispatcher::commit_timer`]
#[derive(Debug)]
pub struct TimerEvent<T> {
    inner: T,
    id: u64,
}

/// Internal information to keep tabs on timers.
struct TimerInfo {
    id: u64,
    instant: ZxTime,
    abort_handle: AbortHandle,
}

/// Helper struct to keep track of timers for the event loop.
pub(super) struct TimerDispatcher<T: Hash + Eq, E> {
    sender: UnboundedSender<E>,
    // Invariant: TimerDispatcher uses a HashMap keyed on an external identifier
    // T and assigns an internal "versioning" ID every time a timer is
    // scheduled. The "versioning" ID is just monotonically incremented and it
    // is used to disambiguate different scheduling events of the same timer T.
    // TimerInfo in the HashMap will always hold the latest allocated
    // "versioning" identifier, meaning that:
    //  - When a timer is rescheduled, we update TimerInfo::id to a new value
    //  - To "commit" a timer firing (through commit_timer), the TimerEvent
    //    given must carry the same "versioning" identifier as the one currently
    //    held by the HashMap in TimerInfo::id.
    // For external users, the only way to obtain T from a TimerEvent<T> is to
    // give it to TimerDispatcher::commit_timer, so that we can further the
    // invariant into: only ONE version of T that was scheduled by
    // TimerDispatcher is "valid" at a given point in time.
    timers: HashMap<T, TimerInfo>,
    next_id: u64,
}

impl<T, E> TimerDispatcher<T, E>
where
    T: Hash + Eq + Clone,
    E: 'static + From<TimerEvent<T>> + Sync + Send,
{
    /// Creates a new `TimerDispatcher` that sends [`TimerEvent`]s over
    /// `sender`.
    pub(super) fn new(sender: UnboundedSender<E>) -> Self {
        Self { timers: HashMap::new(), sender, next_id: 0 }
    }

    /// Schedule a new timer with identifier `timer_id` at `time`.
    ///
    /// If a timer with the same `timer_id` was already scheduled, the old timer is
    /// unscheduled and its expiry time is returned.
    pub(super) fn schedule_timer(&mut self, timer_id: T, time: ZxTime) -> Option<ZxTime> {
        let next_id = self.next_id;
        // Overflowing next_id should be safe enough to hold TimerDispatcher's
        // invariant about around "versioning" timer identifiers. We'll
        // overlflow after 2^64 timers are scheduled (which can take a while)
        // and, even then, for it to break the invariant we'd need to still
        // have a timer scheduled from long ago and be unlucky enough that
        // ordering ends up giving it the same ID. That seems unlikely, so we
        // just wrap around and overflow next_id.
        self.next_id = self.next_id.overflowing_add(1).0;

        let timer_send = self.sender.clone();

        let event = E::from(TimerEvent { inner: timer_id.clone(), id: next_id });

        let timeout = async move {
            fasync::Timer::new(fasync::Time::from_zx(time.0)).await;
            timer_send.unbounded_send(event).unwrap();
        };

        let (abort_handle, abort_registration) = AbortHandle::new_pair();
        let timeout = Abortable::new(timeout, abort_registration);
        // An Abortable future's Output type is Result<_,Aborted>, but
        // spawn_local wants a future whose Output type is (). We achieve that
        // by discarding the abort information with unwrap_or_else below:
        let timeout = timeout.unwrap_or_else(|_| ());
        fasync::spawn_local(timeout);

        match self.timers.entry(timer_id) {
            Entry::Vacant(mut e) => {
                // If we don't have any currently scheduled timers with this
                // timer_id, we're just going to insert a new value into the
                // vacant entry, marking it with next_id.
                e.insert(TimerInfo { id: next_id, instant: time, abort_handle });
                None
            }
            Entry::Occupied(mut e) => {
                // If we already have a scheduled timer with this timer_id, we
                // must...
                let info = e.get_mut();
                // ...call the abort handle on the old timer, to prevent it from
                // firing if it hasn't already:
                info.abort_handle.abort();
                // ...update the abort handle with the new one:
                info.abort_handle = abort_handle;
                // ...store the new "versioning" timer_id next_id, effectively
                // marking this newest version as the only valid one, in case
                // the old timer had already fired as is currently waiting to be
                // commited.
                info.id = next_id;
                // ...finally, we get the old instant information to be returned
                // and update the TimerInfo entry with the new time value:
                let old = Some(info.instant);
                info.instant = time;
                old
            }
        }
    }

    /// Cancels a timer with identifier `timer_id`.
    ///
    /// If a timer with the provided `timer_id` was scheduled, returns the
    /// expiry time for it after having cancelled it.
    pub(super) fn cancel_timer(&mut self, timer_id: &T) -> Option<ZxTime> {
        if let Some(mut t) = self.timers.remove(timer_id) {
            // call the abort handle, in case the future hasn't fired yet:
            t.abort_handle.abort();
            Some(t.instant)
        } else {
            None
        }
    }

    /// Cancels all timers with given filter.
    ///
    /// `f` will be called sequentially for all the currently scheduled timers.
    /// If `f(id)` returns `true`, the timer with `id` will be cancelled.
    pub(super) fn cancel_timers_with<F: FnMut(&T) -> bool>(&mut self, mut f: F) {
        self.timers.retain(|id, info| {
            let discard = f(&id);
            if discard {
                info.abort_handle.abort();
            }
            !discard
        });
    }

    /// Retrieves the internal timer value of a [`TimerEvent`].
    ///
    /// `commit_timer` will "commit" `event` for consumption, if `event` is
    /// still valid to be triggered. If `commit_timer` returns `Some`, then
    /// `TimerDispatcher` will "forget" about the timer identifier contained in
    /// `event`, meaning subsequent calls to [`cancel_timer`] or
    /// [`schedule_timer`] will return `None`.
    ///
    /// [`cancel_timer`]: TimerDispatcher::cancel_timer
    /// [`schedule_timer`]: TimerDispatcher::schedule_timer
    pub(super) fn commit_timer(&mut self, event: TimerEvent<T>) -> Option<T> {
        match self.timers.entry(event.inner.clone()) {
            Entry::Occupied(mut e) => {
                // The event is only valid if its id matches the one in the
                // HashMap:
                if e.get().id == event.id {
                    e.remove();
                    Some(event.inner)
                } else {
                    None
                }
            }
            _ => None,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_zircon as zx;
    use futures::channel::mpsc;
    use futures::{Stream, StreamExt};

    struct OuterEvent(TimerEvent<usize>);

    impl From<TimerEvent<usize>> for OuterEvent {
        fn from(i: TimerEvent<usize>) -> Self {
            OuterEvent(i)
        }
    }

    fn nanos_from_now(nanos: i64) -> ZxTime {
        ZxTime(zx::Time::from_nanos(zx::Time::get(zx::ClockId::Monotonic).into_nanos() + nanos))
    }

    // NOTE(brunodalbo): In the tests below, we rely on the order of the tests
    // that come out, but the timers are scheduled just nanoseconds apart.
    // What's effectively giving us the ordering of the events coming out the
    // UnboundedReceiver is the scheduling order, and not really the actual time
    // value that we set on each timer. This is deterministic enough to avoid
    // flakiness with the singlethreaded and very controlled approach used in
    // the tests, but it stands to note that the spawn_local strategy that is
    // being used by TimerDispatcher does *not* 100% guarantee that timers will
    // actually be operated in order based on their scheduled times.

    #[fasync::run_singlethreaded(test)]
    async fn test_timers_fire() {
        let (snd, mut rcv) = mpsc::unbounded();
        let mut d = TimerDispatcher::<usize, OuterEvent>::new(snd);
        assert!(d.schedule_timer(1, nanos_from_now(1)).is_none());
        assert!(d.schedule_timer(2, nanos_from_now(2)).is_none());
        let t1 = rcv.next().await.unwrap();
        assert_eq!(d.commit_timer(t1.0).unwrap(), 1);
        let t2 = rcv.next().await.unwrap();
        assert_eq!(d.commit_timer(t2.0).unwrap(), 2);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_cancel() {
        let (snd, mut rcv) = mpsc::unbounded();
        let mut d = TimerDispatcher::<usize, OuterEvent>::new(snd);

        // timer 1 and 2 are scheduled.
        // timer 1 is going to be cancelled even before we allow the loop to
        // run, while timer 2 will be cancelled AFTER we retrieve it from
        // the event stream.
        let time1 = nanos_from_now(1);
        let time2 = nanos_from_now(2);
        assert!(d.schedule_timer(1, time1).is_none());
        assert!(d.schedule_timer(2, time2).is_none());

        assert_eq!(d.cancel_timer(&1).unwrap(), time1);
        let t = rcv.next().await.unwrap();
        assert_eq!(d.cancel_timer(&2).unwrap(), time2);
        // only event 2 should come out:
        assert_eq!(t.0.inner, 2);
        // Resolving it should fail:
        assert!(d.commit_timer(t.0).is_none());

        // schedule another timer and wait for it, just to prove that timer 1's
        // event never gets fired:
        assert!(d.schedule_timer(3, nanos_from_now(5)).is_none());
        let t = rcv.next().await.unwrap();
        assert_eq!(t.0.inner, 3);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_reschedule() {
        let (snd, mut rcv) = mpsc::unbounded();
        let mut d = TimerDispatcher::<usize, OuterEvent>::new(snd);

        // timer 1 and 2 are scheduled.
        // timer 1 is going to be rescheduled even before we allow the loop to
        // run, while timer 2 will be rescheduled AFTER we retrieve it from
        // the event stream.
        let time1 = nanos_from_now(1);
        let time2 = nanos_from_now(2);
        let resched1 = nanos_from_now(3);
        let resched2 = nanos_from_now(4);
        assert!(d.schedule_timer(1, time1).is_none());
        assert!(d.schedule_timer(2, time2).is_none());

        assert_eq!(d.schedule_timer(1, resched1).unwrap(), time1);
        let t = rcv.next().await.unwrap();
        assert_eq!(d.schedule_timer(2, resched2).unwrap(), time2);
        // only event 2 should come out:
        assert_eq!(t.0.inner, 2);
        // Resolving it should fail (because we rescheduled it):
        assert!(d.commit_timer(t.0).is_none());

        // now we can go at it again and get the rescheduled timer 1...:
        let t = rcv.next().await.unwrap();
        assert_eq!(t.0.inner, 1);
        assert_eq!(d.commit_timer(t.0).unwrap(), 1);

        // ... and timer 2:
        let t = rcv.next().await.unwrap();
        assert_eq!(t.0.inner, 2);
        assert_eq!(d.commit_timer(t.0).unwrap(), 2);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_cancel_with() {
        let (snd, mut rcv) = mpsc::unbounded();
        let mut d = TimerDispatcher::<usize, OuterEvent>::new(snd);

        // schedule 4 timers:
        assert!(d.schedule_timer(1, nanos_from_now(1)).is_none());
        assert!(d.schedule_timer(2, nanos_from_now(2)).is_none());
        assert!(d.schedule_timer(3, nanos_from_now(3)).is_none());
        assert!(d.schedule_timer(4, nanos_from_now(4)).is_none());

        // cancel timers 1, 3, and 4.
        d.cancel_timers_with(|id| *id != 2);
        // check that only one timer remains
        assert_eq!(d.timers.len(), 1);

        // get the timer and assert that it is the timer with id == 2.
        let t = rcv.next().await.unwrap();
        assert_eq!(t.0.inner, 2);
        assert_eq!(d.commit_timer(t.0).unwrap(), 2);
    }
}
