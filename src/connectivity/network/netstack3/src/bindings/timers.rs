// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::hash_map::Entry;
use std::collections::HashMap;
use std::fmt::Debug;
use std::hash::Hash;

use async_utils::futures::{FutureExt as _, ReplaceValue};
use fuchsia_async as fasync;
use futures::{
    channel::mpsc,
    future::{AbortHandle, Abortable, Aborted},
    stream::{FuturesUnordered, StreamExt as _},
};
use log::trace;

use super::{context::Lockable, StackTime};

/// A possible timer event that may be fulfilled by calling
/// [`TimerDispatcher::commit_timer`].
#[derive(Debug)]
struct TimerEvent<T> {
    inner: T,
    id: u64,
}

/// Internal information to keep tabs on timers.
struct TimerInfo {
    id: u64,
    instant: StackTime,
    abort_handle: AbortHandle,
}

/// A context for specified for a timer type `T` that provides asynchronous
/// locking to a [`TimerHandler`].
pub(crate) trait TimerContext<T: Hash + Eq>:
    'static + for<'a> Lockable<'a, <Self as TimerContext<T>>::Handler> + Clone
{
    type Handler: TimerHandler<T>;
}

/// An entity responsible for receiving expired timers.
///
/// `TimerHandler` is used to communicate expired timers from a
/// [`TimerDispatcher`] that was spawned with some [`TimerContext`].
pub(crate) trait TimerHandler<T: Hash + Eq>: Sized + 'static {
    /// The provided `timer` is expired (its deadline arrived and it wasn't
    /// cancelled or rescheduled).
    fn handle_expired_timer(&mut self, timer: T);
    /// Retrieve a mutable reference to the [`TimerDispatcher`] associated with
    /// this `TimerHandler`. It *must* be the same `TimerDispatcher` instance
    /// for which this handler's [`TimerContext`] was spawned with
    /// [`TimerDispatcher::spawn`].
    ///
    /// The provided `TimerDispatcher` must exist within the same lock context
    /// as the `TimerHandler` so it can ensure the contract that timers that are
    /// cancelled or rescheduled are *never* passed to
    /// [`TimerHandler::handle_expired_timer`].
    fn get_timer_dispatcher(&mut self) -> &mut TimerDispatcher<T>;
}

type TimerFut<T> = ReplaceValue<fasync::Timer, T>;

/// Shorthand for the type of futures used by [`TimerDispatcher`] internally.
type InternalFut<T> = Abortable<TimerFut<TimerEvent<T>>>;

/// Helper struct to keep track of timers for the event loop.
pub(crate) struct TimerDispatcher<T: Hash + Eq> {
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
    // The committing mechanism is invisible to external users, which just
    // receive the expired timers through TimerHandler::handle_expired_timer.
    // See TimerDispatcher::spawn for the critical section that makes versioning
    // required.
    timers: HashMap<T, TimerInfo>,
    next_id: u64,
    futures_sender: Option<mpsc::UnboundedSender<InternalFut<T>>>,
}

impl<T: Hash + Eq> Default for TimerDispatcher<T> {
    fn default() -> TimerDispatcher<T> {
        TimerDispatcher { timers: Default::default(), next_id: 0, futures_sender: None }
    }
}

impl<T> TimerDispatcher<T>
where
    T: Hash + Debug + Eq + Clone + Send + Sync + Unpin + 'static,
{
    /// Spawns a [`TimerContext`] that will observe events on this
    /// `TimerDispatcher` through its [`TimerHandler`].
    ///
    /// # Panics
    ///
    /// Panics if this `TimerDispatcher` was already spawned.
    pub(crate) fn spawn<C: TimerContext<T> + Send + Sync>(&mut self, ctx: C) {
        assert!(self.futures_sender.is_none(), "TimerDispatcher already spawned");
        let (sender, mut recv) = mpsc::unbounded();
        self.futures_sender = Some(sender);
        fasync::Task::spawn(async move {
            let mut futures = FuturesUnordered::<InternalFut<T>>::new();

            #[derive(Debug)]
            enum PollResult<T> {
                InstallFuture(InternalFut<T>),
                TimerFired(TimerEvent<T>),
                Aborted,
                ReceiverClosed,
                FuturesClosed,
            }

            loop {
                // avoid polling `futures` if it is empty
                let r = if futures.is_empty() {
                    match recv.next().await {
                        Some(next_fut) => PollResult::InstallFuture(next_fut),
                        None => PollResult::ReceiverClosed,
                    }
                } else {
                    futures::select! {
                        r = recv.next() => match r {
                            Some(next_fut) => PollResult::InstallFuture(next_fut),
                            None => PollResult::ReceiverClosed
                        },
                        t = futures.next() => match t {
                            Some(Ok(t)) => PollResult::TimerFired(t),
                            Some(Err(Aborted)) => PollResult::Aborted,
                            None => PollResult::FuturesClosed
                        }
                    }
                };
                // NB: This is the critical section that makes it so that we
                // need to version timers and verify the versioning through
                // `commit_timer` before passing those over to the handler. At
                // this point, the timer future has already resolved. It may
                // already have been aborted, in which case the version ID
                // doesn't matter. But it may also have been already fulfilled.
                // The race comes from the fact that we don't currently have a
                // lock on the context, we're going to acquire the lock in case
                // the `r` is `TimerFired` below. As we await on the lock, the
                // TimerEvent we're currently holding may have be invalidated by
                // another Task, so it must NOT be given to to the handler.

                trace!("TimerDispatcher work: {:?}", r);
                match r {
                    PollResult::InstallFuture(fut) => futures.push(fut),
                    PollResult::TimerFired(t) => {
                        let mut handler = ctx.lock().await;
                        let disp = handler.get_timer_dispatcher();

                        match disp.commit_timer(t) {
                            Ok(t) => {
                                trace!("TimerDispatcher: firing timer {:?}", t);
                                handler.handle_expired_timer(t);
                            }
                            Err(e) => {
                                trace!("TimerDispatcher: timer was stale {:?}", e);
                            }
                        }
                    }
                    PollResult::Aborted => {}
                    PollResult::ReceiverClosed | PollResult::FuturesClosed => break,
                }
            }
        })
        .detach();
    }

    /// Schedule a new timer with identifier `timer_id` at `time`.
    ///
    /// If a timer with the same `timer_id` was already scheduled, the old timer
    /// is unscheduled and its expiry time is returned.
    pub(crate) fn schedule_timer(&mut self, timer_id: T, time: StackTime) -> Option<StackTime> {
        let next_id = self.next_id;

        let sender = if let Some(s) = self.futures_sender.as_mut() {
            s
        } else {
            trace!("TimerDispatcher not spawned, ignoring timer {:?}", timer_id);
            return None;
        };

        // Overflowing next_id should be safe enough to hold TimerDispatcher's
        // invariant about around "versioning" timer identifiers. We'll
        // overlflow after 2^64 timers are scheduled (which can take a while)
        // and, even then, for it to break the invariant we'd need to still have
        // a timer scheduled from long ago and be unlucky enough that ordering
        // ends up giving it the same ID. That seems unlikely, so we just wrap
        // around and overflow next_id.
        self.next_id = self.next_id.overflowing_add(1).0;

        let event = TimerEvent { inner: timer_id.clone(), id: next_id };

        let (abort_handle, abort_registration) = AbortHandle::new_pair();
        let timeout = {
            let StackTime(time) = time;
            Abortable::new(fasync::Timer::new(time).replace_value(event), abort_registration)
        };

        sender.unbounded_send(timeout).expect("TimerDispatcher's task receiver is gone");

        match self.timers.entry(timer_id) {
            Entry::Vacant(e) => {
                // If we don't have any currently scheduled timers with this
                // timer_id, we're just going to insert a new value into the
                // vacant entry, marking it with next_id.
                let _: &mut TimerInfo =
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
    pub(crate) fn cancel_timer(&mut self, timer_id: &T) -> Option<StackTime> {
        if let Some(t) = self.timers.remove(timer_id) {
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
    pub(crate) fn cancel_timers_with<F: FnMut(&T) -> bool>(&mut self, mut f: F) {
        self.timers.retain(|id, info| {
            let discard = f(&id);
            if discard {
                info.abort_handle.abort();
            }
            !discard
        });
    }

    /// Gets the time a timer with identifier `timer_id` will be invoked.
    ///
    /// If a timer with the provided `timer_id` exists, returns the expiry
    /// time for it; `None` otherwise.
    pub(crate) fn scheduled_time(&self, timer_id: &T) -> Option<StackTime> {
        self.timers.get(timer_id).map(|t| t.instant)
    }

    /// Retrieves the internal timer value of a [`TimerEvent`].
    ///
    /// `commit_timer` will "commit" `event` for consumption, if `event` is
    /// still valid to be triggered. If `commit_timer` returns `Ok`, then
    /// `TimerDispatcher` will "forget" about the timer identifier contained in
    /// `event`, meaning subsequent calls to [`cancel_timer`] or
    /// [`schedule_timer`] will return `None`.
    ///
    /// [`cancel_timer`]: TimerDispatcher::cancel_timer
    /// [`schedule_timer`]: TimerDispatcher::schedule_timer
    fn commit_timer(&mut self, event: TimerEvent<T>) -> Result<T, TimerEvent<T>> {
        match self.timers.entry(event.inner.clone()) {
            Entry::Occupied(e) => {
                // The event is only valid if its id matches the one in the
                // HashMap:
                if e.get().id == event.id {
                    let _: TimerInfo = e.remove();
                    Ok(event.inner)
                } else {
                    Err(event)
                }
            }
            Entry::Vacant(_) => Err(event),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::bindings::{context::Lockable, integration_tests::set_logger_for_test};
    use assert_matches::assert_matches;
    use fuchsia_zircon::{self as zx, DurationNum};
    use futures::{channel::mpsc, lock::Mutex, task::Poll, Future, StreamExt};
    use std::sync::Arc;

    type TestDispatcher = TimerDispatcher<usize>;

    struct TimerData {
        dispatcher: TestDispatcher,
        fired: mpsc::UnboundedSender<usize>,
    }

    impl TimerHandler<usize> for TimerData {
        fn handle_expired_timer(&mut self, timer: usize) {
            self.fired.unbounded_send(timer).expect("Can't fire timer")
        }

        fn get_timer_dispatcher(&mut self) -> &mut TimerDispatcher<usize> {
            &mut self.dispatcher
        }
    }

    #[derive(Clone)]
    struct TestContext(Arc<Mutex<TimerData>>);

    impl TestContext {
        fn new() -> (Self, mpsc::UnboundedReceiver<usize>) {
            let (fired, receiver) = mpsc::unbounded();
            let inner =
                Arc::new(Mutex::new(TimerData { dispatcher: TestDispatcher::default(), fired }));
            inner.try_lock().unwrap().dispatcher.spawn(Self(inner.clone()));
            (Self(inner), receiver)
        }

        fn with_disp_sync<R, F: FnOnce(&mut TestDispatcher) -> R>(&mut self, f: F) -> R {
            f(&mut self.0.try_lock().expect("Failed to lock dispatcher synchronously").dispatcher)
        }
    }

    impl<'a> Lockable<'a, TimerData> for TestContext {
        type Guard = futures::lock::MutexGuard<'a, TimerData>;
        type Fut = futures::lock::MutexLockFuture<'a, TimerData>;
        fn lock(&'a self) -> Self::Fut {
            let Self(arc) = self;
            arc.lock()
        }
    }

    impl TimerContext<usize> for TestContext {
        type Handler = TimerData;
    }

    fn nanos_from_now(nanos: i64) -> StackTime {
        StackTime(fasync::Time::after(zx::Duration::from_nanos(nanos)))
    }

    fn run_in_executor<R, Fut: Future<Output = R>>(
        executor: &mut fasync::TestExecutor,
        f: Fut,
    ) -> R {
        futures::pin_mut!(f);
        loop {
            executor.wake_main_future();
            match executor.run_one_step(&mut f) {
                Some(Poll::Ready(x)) => break x,
                None => panic!("Executor stalled"),
                Some(Poll::Pending) => (),
            }
        }
    }

    fn run_until_stalled(executor: &mut fasync::TestExecutor) {
        let fut = futures::future::ready(());
        futures::pin_mut!(fut);
        executor.wake_main_future();
        loop {
            match executor.run_one_step(&mut fut) {
                Some(Poll::Ready(())) => (),
                None => break,
                Some(Poll::Pending) => (),
            }
        }
    }

    #[test]
    fn test_timers_fire() {
        set_logger_for_test();
        let mut executor = fasync::TestExecutor::new_with_fake_time().unwrap();

        let (t, mut fired) = TestContext::new();
        run_in_executor(&mut executor, async {
            let mut d = t.lock().await;
            assert_eq!(d.dispatcher.schedule_timer(1, nanos_from_now(1)), None);
            assert_eq!(d.dispatcher.schedule_timer(2, nanos_from_now(2)), None);
        });
        assert_matches!(fired.try_next(), Err(mpsc::TryRecvError { .. }));
        executor.set_fake_time(fasync::Time::after(1.nanos()));
        assert_eq!(run_in_executor(&mut executor, fired.next()).unwrap(), 1);
        assert_matches!(fired.try_next(), Err(mpsc::TryRecvError { .. }));
        executor.set_fake_time(fasync::Time::after(1.nanos()));
        assert_eq!(run_in_executor(&mut executor, fired.next()).unwrap(), 2);
    }

    #[test]
    fn test_get_scheduled_instant() {
        set_logger_for_test();
        let mut _executor = fasync::TestExecutor::new_with_fake_time().unwrap();
        let (t, _) = TestContext::new();

        let mut lock = t.0.try_lock().unwrap();
        let d = &mut lock.dispatcher;

        // Timer 1 is scheduled.
        let time1 = nanos_from_now(1);
        assert_eq!(d.schedule_timer(1, time1), None);
        assert_eq!(d.scheduled_time(&1).unwrap(), time1);

        // Timer 2 does not exist yet.
        assert_eq!(d.scheduled_time(&2), None);

        // Timer 1 is scheduled.
        let time2 = nanos_from_now(2);
        assert_eq!(d.schedule_timer(2, time2), None);
        assert_eq!(d.scheduled_time(&1).unwrap(), time1);
        assert_eq!(d.scheduled_time(&2).unwrap(), time2);

        // Cancel Timer 1.
        assert_eq!(d.cancel_timer(&1).unwrap(), time1);
        assert_eq!(d.scheduled_time(&1), None);

        // Timer 2 should still be scheduled.
        assert_eq!(d.scheduled_time(&2).unwrap(), time2);
    }

    #[test]
    fn test_cancel() {
        set_logger_for_test();
        let mut executor = fasync::TestExecutor::new_with_fake_time().unwrap();
        let (mut t, mut rcv) = TestContext::new();

        // timer 1 and 2 are scheduled.
        // timer 1 is going to be cancelled even before we allow the loop to
        // run.

        let time1 = nanos_from_now(1);
        let time2 = nanos_from_now(2);
        let time3 = nanos_from_now(5);
        t.with_disp_sync(|d| {
            assert_eq!(d.schedule_timer(1, time1), None);
            assert_eq!(d.schedule_timer(2, time2), None);

            assert_eq!(d.cancel_timer(&1).unwrap(), time1);
        });
        executor.set_fake_time(time2.0);
        let r = run_in_executor(&mut executor, rcv.next()).unwrap();

        t.with_disp_sync(|d| {
            // can't cancel 2 anymore, it has already fired
            assert_eq!(d.cancel_timer(&2), None);
        });
        // only event 2 should come out because 1 was cancelled:
        assert_eq!(r, 2);

        // schedule another timer and wait for it, just to prove that timer 1's
        // event never gets fired:
        t.with_disp_sync(|d| {
            assert_eq!(d.schedule_timer(3, time3), None);
        });
        executor.set_fake_time(time3.0);
        let r = run_in_executor(&mut executor, rcv.next()).unwrap();
        assert_eq!(r, 3);
    }

    #[test]
    fn test_late_cancel() {
        // test that late cancellation will work (meaning the internal timer
        // future will fire, but we'll cancel it before the timer dispatcher has
        // a chance to commit it).

        let mut executor = fasync::TestExecutor::new_with_fake_time().unwrap();

        let time1 = nanos_from_now(1);
        let time2 = nanos_from_now(2);
        let time3 = nanos_from_now(3);

        let (t, mut rcv) = TestContext::new();
        {
            let d = &mut t.0.try_lock().unwrap().dispatcher;
            assert_eq!(d.schedule_timer(1, time1), None);
            assert_eq!(d.schedule_timer(2, time2), None);
            executor.set_fake_time(time1.0);
            // run the executor until it's stalled. We're still locking the
            // context mutex, meaning the dispatcher task is waiting for us.
            run_until_stalled(&mut executor);
            // now we cancel the first timer
            assert_eq!(d.cancel_timer(&1).unwrap(), time1);
        }
        run_until_stalled(&mut executor);
        assert_matches!(rcv.try_next(), Err(mpsc::TryRecvError { .. }));
        {
            let d = &mut t.0.try_lock().unwrap().dispatcher;
            // do the same thing again, we'll let the timer expire, but we're
            // holding the lock so the executor will stall waiting for the
            // context lock.
            executor.set_fake_time(time2.0);
            run_until_stalled(&mut executor);
            // reschedule timer2
            assert_eq!(d.schedule_timer(2, time3).unwrap(), time2);
        }
        run_until_stalled(&mut executor);
        assert_matches!(rcv.try_next(), Err(mpsc::TryRecvError { .. }));

        // finally after setting the time to the rescheduled time, we should get
        // the rescheduled timer 2.
        executor.set_fake_time(time3.0);
        assert_eq!(run_in_executor(&mut executor, rcv.next()).unwrap(), 2);
    }

    #[test]
    fn test_reschedule() {
        set_logger_for_test();
        let mut executor = fasync::TestExecutor::new_with_fake_time().unwrap();
        let (mut t, mut rcv) = TestContext::new();

        // timer 1 and 2 are scheduled.
        // timer 1 is going to be rescheduled even before we allow the loop to
        // run.
        let time1 = nanos_from_now(1);
        let time2 = nanos_from_now(2);
        let resched1 = nanos_from_now(3);
        let resched2 = nanos_from_now(4);

        t.with_disp_sync(|d| {
            assert_eq!(d.schedule_timer(1, time1), None);
            assert_eq!(d.schedule_timer(2, time2), None);
            assert_eq!(d.schedule_timer(1, resched1).unwrap(), time1);
        });
        executor.set_fake_time(time2.0);
        let r = run_in_executor(&mut executor, rcv.next()).unwrap();
        // only event 2 should come out:
        assert_eq!(r, 2);

        t.with_disp_sync(|d| {
            // we can schedule timer 2 again, and it returns None because it has
            // already fired.
            assert_eq!(d.schedule_timer(2, resched2), None);
        });

        // now we can go at it again and get the rescheduled timers:
        executor.set_fake_time(resched2.0);
        assert_eq!(run_in_executor(&mut executor, rcv.next()).unwrap(), 1);
        assert_eq!(run_in_executor(&mut executor, rcv.next()).unwrap(), 2);
    }

    #[test]
    fn test_cancel_with() {
        set_logger_for_test();
        let mut executor = fasync::TestExecutor::new_with_fake_time().unwrap();
        let (mut t, mut rcv) = TestContext::new();

        t.with_disp_sync(|d| {
            // schedule 4 timers:
            assert_eq!(d.schedule_timer(1, nanos_from_now(1)), None);
            assert_eq!(d.schedule_timer(2, nanos_from_now(2)), None);
            assert_eq!(d.schedule_timer(3, nanos_from_now(3)), None);
            assert_eq!(d.schedule_timer(4, nanos_from_now(4)), None);

            // cancel timers 1, 3, and 4.
            d.cancel_timers_with(|id| *id != 2);
            // check that only one timer remains
            assert_eq!(d.timers.len(), 1);
        });
        // advance time so that all timers would've been fired.
        executor.set_fake_time(nanos_from_now(4).0);
        // get the timer and assert that it is the timer with id == 2.
        let r = run_in_executor(&mut executor, rcv.next()).unwrap();
        assert_eq!(r, 2);
    }
}
