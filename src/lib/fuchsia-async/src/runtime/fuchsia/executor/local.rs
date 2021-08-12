// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::super::timer::TimerHeap;
use super::{
    common::{
        with_local_timer_heap, ExecutorTime, Inner, Notifier, EMPTY_WAKEUP_ID, MAIN_TASK_ID,
        TASK_READY_WAKEUP_ID,
    },
    time::Time,
};
use crate::runtime::fuchsia::executor::instrumentation::{LocalCollector, WakeupReason};
use fuchsia_zircon::{self as zx};
use futures::{task::ArcWake, FutureExt};
use pin_utils::pin_mut;
use std::{
    fmt,
    future::Future,
    marker::Unpin,
    sync::atomic::AtomicI64,
    sync::{Arc, Weak},
    task::{Context, Poll, Waker},
    usize,
};

/// A single-threaded port-based executor for Fuchsia OS.
///
/// Having a `LocalExecutor` in scope allows the creation and polling of zircon objects, such as
/// [`fuchsia_async::Channel`].
///
/// # Panics
///
/// `LocalExecutor` will panic on drop if any zircon objects attached to it are still alive. In
/// other words, zircon objects backed by a `LocalExecutor` must be dropped before it.
pub struct LocalExecutor {
    /// The inner executor state.
    inner: Arc<Inner>,
    // Synthetic main task, representing the main futures during the executor's lifetime.
    main_task: Arc<MainTask>,
    // Waker for the main task, cached for performance reasons.
    main_waker: Waker,
}

impl fmt::Debug for LocalExecutor {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("LocalExecutor").field("port", &self.inner.port).finish()
    }
}

impl LocalExecutor {
    /// Create a new single-threaded executor running with actual time.
    pub fn new() -> Result<Self, zx::Status> {
        let inner = Arc::new(Inner::new(ExecutorTime::RealTime, /* is_local */ true)?);
        inner.clone().set_local(TimerHeap::default());
        let main_task =
            Arc::new(MainTask { executor: Arc::downgrade(&inner), notifier: Notifier::default() });
        let main_waker = futures::task::waker(main_task.clone());
        Ok(Self { inner, main_task, main_waker })
    }

    /// Run a single future to completion on a single thread, also polling other active tasks.
    pub fn run_singlethreaded<F>(&mut self, main_future: F) -> F::Output
    where
        F: Future,
    {
        self.inner
            .require_real_time()
            .expect("Error: called `run_singlethreaded` on an executor using fake time");
        let mut local_collector = self.inner.collector.create_local_collector();

        pin_mut!(main_future);
        let mut res = self.main_task.poll(&mut main_future, &self.main_waker);
        local_collector.task_polled(
            MAIN_TASK_ID,
            self.inner.source,
            /* complete */ false,
            /* pending_tasks */ self.inner.ready_tasks.len(),
        );

        loop {
            if let Poll::Ready(res) = res {
                return res;
            }

            let packet = with_local_timer_heap(|timer_heap| {
                let deadline =
                    timer_heap.next_deadline().map(|t| t.time()).unwrap_or(Time::INFINITE);
                // into_zx: we are using real time, so the time is a monotonic time.
                local_collector.will_wait();
                match self.inner.port.wait(deadline.into_zx()) {
                    Ok(packet) => Some(packet),
                    Err(zx::Status::TIMED_OUT) => {
                        local_collector.woke_up(WakeupReason::Deadline);
                        let time_waker = timer_heap.pop().unwrap();
                        time_waker.wake();
                        None
                    }
                    Err(status) => {
                        panic!("Error calling port wait: {:?}", status);
                    }
                }
            });

            if let Some(packet) = packet {
                match packet.key() {
                    EMPTY_WAKEUP_ID => {
                        local_collector.woke_up(WakeupReason::Notification);
                        res = self.main_task.poll(&mut main_future, &self.main_waker);
                        local_collector.task_polled(
                            MAIN_TASK_ID,
                            self.inner.source,
                            /* complete */ false,
                            /* pending_tasks */ self.inner.ready_tasks.len(),
                        );
                    }
                    TASK_READY_WAKEUP_ID => {
                        local_collector.woke_up(WakeupReason::Notification);
                        self.inner.poll_ready_tasks(&mut local_collector);
                    }
                    receiver_key => {
                        local_collector.woke_up(WakeupReason::Io);
                        self.inner.deliver_packet(receiver_key as usize, packet);
                    }
                }
            }

            // we've just yielded out of a task, wake any timers that expired while we were polling
            with_local_timer_heap(|timer_heap| timer_heap.wake_expired_timers(self.inner.now()));
        }
    }

    #[cfg(test)]
    pub(crate) fn snapshot(&self) -> super::instrumentation::Snapshot {
        self.inner.collector.snapshot()
    }
}

impl Drop for LocalExecutor {
    fn drop(&mut self) {
        self.inner.mark_done();
        self.inner.on_parent_drop();
    }
}

/// A single-threaded executor for testing. Exposes additional APIs for manipulating executor state
/// and validating behavior of executed tasks.
pub struct TestExecutor {
    /// LocalExecutor used under the hood, since most of the logic is shared.
    local: LocalExecutor,

    // A packet that has been dequeued but not processed. This is used by `run_one_step`.
    next_packet: Option<zx::Packet>,
}

impl TestExecutor {
    /// Create a new executor for testing.
    pub fn new() -> Result<Self, zx::Status> {
        Ok(Self { local: LocalExecutor::new()?, next_packet: None })
    }

    /// Create a new single-threaded executor running with fake time.
    pub fn new_with_fake_time() -> Result<Self, zx::Status> {
        let inner = Arc::new(Inner::new(
            ExecutorTime::FakeTime(AtomicI64::new(Time::INFINITE_PAST.into_nanos())),
            /* is_local */ true,
        )?);
        inner.clone().set_local(TimerHeap::default());
        let main_task =
            Arc::new(MainTask { executor: Arc::downgrade(&inner), notifier: Notifier::default() });
        let main_waker = futures::task::waker(main_task.clone());
        Ok(Self { local: LocalExecutor { inner, main_task, main_waker }, next_packet: None })
    }

    /// Return the current time according to the executor.
    pub fn now(&self) -> Time {
        self.local.inner.now()
    }

    /// Set the fake time to a given value.
    ///
    /// # Panics
    ///
    /// If the executor was not created with fake time
    pub fn set_fake_time(&self, t: Time) {
        self.local.inner.set_fake_time(t)
    }

    /// Run a single future to completion on a single thread, also polling other active tasks.
    pub fn run_singlethreaded<F>(&mut self, main_future: F) -> F::Output
    where
        F: Future,
    {
        self.local.run_singlethreaded(main_future)
    }

    /// PollResult the future. If it is not ready, dispatch available packets and possibly try again.
    /// Timers will not fire. Never blocks.
    ///
    /// This function is for testing. DO NOT use this function in tests or applications that
    /// involve any interaction with other threads or processes, as those interactions
    /// may become stalled waiting for signals from "the outside world" which is beyond
    /// the knowledge of the executor.
    ///
    /// Unpin: this function requires all futures to be `Unpin`able, so any `!Unpin`
    /// futures must first be pinned using the `pin_mut!` macro from the `pin-utils` crate.
    pub fn run_until_stalled<F>(&mut self, main_future: &mut F) -> Poll<F::Output>
    where
        F: Future + Unpin,
    {
        let inner = self.local.inner.clone();
        let mut local_collector = inner.collector.create_local_collector();
        self.wake_main_future();
        while let NextStep::NextPacket =
            self.next_step(/*fire_timers:*/ false, &mut local_collector)
        {
            // Will not fail, because NextPacket means there is a
            // packet ready to be processed.
            let res = self.consume_packet(main_future, &mut local_collector);
            if res.is_ready() {
                return res;
            }
        }
        Poll::Pending
    }

    /// Schedule the main future for being woken up. This is useful in conjunction with
    /// `run_one_step`.
    pub fn wake_main_future(&mut self) {
        ArcWake::wake_by_ref(&self.local.main_task);
    }

    /// Run one iteration of the loop: dispatch the first available packet or timer. Returns `None`
    /// if nothing has been dispatched, `Some(Poll::Pending)` if execution made progress but the
    /// main future has not completed, and `Some(Poll::Ready(_))` if the main future has completed
    /// at this step.
    ///
    /// For the main future to run, `wake_main_future` needs to have been called first.
    /// This will fire timers that are in the past, but will not advance the executor's time.
    ///
    /// Unpin: this function requires all futures to be `Unpin`able, so any `!Unpin`
    /// futures must first be pinned using the `pin_mut!` macro from the `pin-utils` crate.
    ///
    /// This function is meant to be used for reproducible integration tests: multiple async
    /// processes can be run in a controlled way, dispatching events one at a time and randomly
    /// (but reproducibly) choosing which process gets to advance at each step.
    pub fn run_one_step<F>(&mut self, main_future: &mut F) -> Option<Poll<F::Output>>
    where
        F: Future + Unpin,
    {
        let inner = self.local.inner.clone();
        let mut local_collector = inner.collector.create_local_collector();
        match self.next_step(/*fire_timers:*/ true, &mut local_collector) {
            NextStep::WaitUntil(_) => None,
            NextStep::NextPacket => {
                // Will not fail because NextPacket means there is a
                // packet ready to be processed.
                Some(self.consume_packet(main_future, &mut local_collector))
            }
            NextStep::NextTimer => {
                let next_timer = with_local_timer_heap(|timer_heap| {
                    // unwrap: will not fail because NextTimer
                    // guarantees there is a timer in the heap.
                    timer_heap.pop().unwrap()
                });
                next_timer.wake();
                Some(Poll::Pending)
            }
        }
    }

    /// Consumes a packet that has already been dequeued from the port.
    /// This must only be called when there is a packet available.
    fn consume_packet<F>(
        &mut self,
        main_future: &mut F,
        mut local_collector: &mut LocalCollector<'_>,
    ) -> Poll<F::Output>
    where
        F: Future + Unpin,
    {
        let packet =
            self.next_packet.take().expect("consume_packet called but no packet available");
        match packet.key() {
            EMPTY_WAKEUP_ID => {
                let res = self.local.main_task.poll(main_future, &self.local.main_waker);
                local_collector.task_polled(
                    MAIN_TASK_ID,
                    self.local.inner.source,
                    /* complete */ false,
                    /* pending_tasks */ self.local.inner.ready_tasks.len(),
                );
                res
            }
            TASK_READY_WAKEUP_ID => {
                self.local.inner.poll_ready_tasks(&mut local_collector);
                Poll::Pending
            }
            receiver_key => {
                self.local.inner.deliver_packet(receiver_key as usize, packet);
                Poll::Pending
            }
        }
    }

    fn next_step(
        &mut self,
        fire_timers: bool,
        local_collector: &mut LocalCollector<'_>,
    ) -> NextStep {
        // If a packet is queued from a previous call to next_step, it must be executed first.
        if let Some(_) = self.next_packet {
            return NextStep::NextPacket;
        }
        // If we are past a deadline, run the corresponding timer.
        let next_deadline = with_local_timer_heap(|timer_heap| {
            timer_heap.next_deadline().map(|t| t.time()).unwrap_or(Time::INFINITE)
        });
        if fire_timers && next_deadline <= self.local.inner.now() {
            NextStep::NextTimer
        } else {
            local_collector.will_wait();
            // Try to unqueue a packet from the port.
            match self.local.inner.port.wait(zx::Time::INFINITE_PAST) {
                Ok(packet) => {
                    let reason = match packet.key() {
                        TASK_READY_WAKEUP_ID | EMPTY_WAKEUP_ID => WakeupReason::Notification,
                        _ => WakeupReason::Io,
                    };
                    local_collector.woke_up(reason);
                    self.next_packet = Some(packet);
                    NextStep::NextPacket
                }
                Err(zx::Status::TIMED_OUT) => {
                    local_collector.woke_up(WakeupReason::Deadline);
                    NextStep::WaitUntil(next_deadline)
                }
                Err(status) => {
                    panic!("Error calling port wait: {:?}", status);
                }
            }
        }
    }

    /// Return `Ready` if the executor has work to do, or `Waiting(next_deadline)` if there will be
    /// no work to do before `next_deadline` or an external event.
    ///
    /// If this returns `Ready`, `run_one_step` will return `Some(_)`. If there is no pending packet
    /// or timer, `Waiting(Time::INFINITE)` is returned.
    pub fn is_waiting(&mut self) -> WaitState {
        let inner = self.local.inner.clone();
        let mut local_collector = inner.collector.create_local_collector();
        match self.next_step(/*fire_timers:*/ true, &mut local_collector) {
            NextStep::NextPacket | NextStep::NextTimer => WaitState::Ready,
            NextStep::WaitUntil(t) => WaitState::Waiting(t),
        }
    }

    /// Wake all tasks waiting for expired timers, and return `true` if any task was woken.
    ///
    /// This is intended for use in test code in conjunction with fake time.
    pub fn wake_expired_timers(&mut self) -> bool {
        with_local_timer_heap(|timer_heap| timer_heap.wake_expired_timers(self.now()))
    }

    /// Wake up the next task waiting for a timer, if any, and return the time for which the
    /// timer was scheduled.
    ///
    /// This is intended for use in test code in conjunction with `run_until_stalled`.
    /// For example, here is how one could test that the Timer future fires after the given
    /// timeout:
    ///
    ///     let deadline = 5.seconds().after_now();
    ///     let mut future = Timer::<Never>::new(deadline);
    ///     assert_eq!(Poll::Pending, exec.run_until_stalled(&mut future));
    ///     assert_eq!(Some(deadline), exec.wake_next_timer());
    ///     assert_eq!(Poll::Ready(()), exec.run_until_stalled(&mut future));
    pub fn wake_next_timer(&mut self) -> Option<Time> {
        with_local_timer_heap(|timer_heap| {
            let deadline = timer_heap.next_deadline().map(|waker| {
                waker.wake();
                waker.time()
            });
            if deadline.is_some() {
                timer_heap.pop();
            }
            deadline
        })
    }

    #[cfg(test)]
    pub(crate) fn snapshot(&self) -> super::instrumentation::Snapshot {
        self.local.inner.collector.snapshot()
    }
}

enum NextStep {
    WaitUntil(Time),
    NextPacket,
    NextTimer,
}

/// Indicates whether the executor can run, or is stuck waiting.
#[derive(Clone, Copy, Eq, PartialEq, Debug)]
pub enum WaitState {
    /// The executor can run immediately.
    Ready,
    /// The executor will wait for the given time or an external event.
    Waiting(Time),
}

/// A synthetic main task which represents the "main future" as passed by the user.
/// The main future can change during the lifetime of the executor, but the notification
/// mechanism is shared.
struct MainTask {
    executor: Weak<Inner>,
    notifier: Notifier,
}

impl ArcWake for MainTask {
    fn wake_by_ref(arc_self: &Arc<Self>) {
        if arc_self.notifier.prepare_notify() {
            if let Some(executor) = Weak::upgrade(&arc_self.executor) {
                executor.notify_empty();
            }
        }
    }
}

impl MainTask {
    /// Poll the main future using the notification semantics of the main task.
    fn poll<F>(self: &Arc<Self>, main_future: &mut F, main_waker: &Waker) -> Poll<F::Output>
    where
        F: Future + Unpin,
    {
        self.notifier.reset();
        let main_cx = &mut Context::from_waker(&main_waker);
        main_future.poll_unpin(main_cx)
    }
}

#[cfg(test)]
mod tests {
    use super::{super::spawn, *};
    use crate::{handle::on_signals::OnSignals, Timer};
    use fuchsia_zircon::{self as zx, AsHandleRef, DurationNum};
    use futures::{future, Future};
    use pin_utils::pin_mut;
    use std::{
        cell::{Cell, RefCell},
        rc::Rc,
        sync::atomic::{AtomicBool, Ordering},
        task::{Context, Poll, Waker},
    };

    fn run_until_stalled<F>(executor: &mut TestExecutor, fut: &mut F)
    where
        F: Future + Unpin,
    {
        loop {
            match executor.run_one_step(fut) {
                None => return,
                Some(Poll::Pending) => { /* continue */ }
                Some(Poll::Ready(_)) => panic!("executor stopped"),
            }
        }
    }

    fn run_until_done<F>(executor: &mut TestExecutor, fut: &mut F) -> F::Output
    where
        F: Future + Unpin,
    {
        loop {
            match executor.run_one_step(fut) {
                None => panic!("executor stalled"),
                Some(Poll::Pending) => { /* continue */ }
                Some(Poll::Ready(res)) => return res,
            }
        }
    }

    // Runs a future that suspends and returns after being resumed.
    #[test]
    fn stepwise_two_steps() {
        let fut_step = Cell::new(0);
        let fut_waker: Rc<RefCell<Option<Waker>>> = Rc::new(RefCell::new(None));
        let fut_fn = |cx: &mut Context<'_>| {
            fut_waker.borrow_mut().replace(cx.waker().clone());
            match fut_step.get() {
                0 => {
                    fut_step.set(1);
                    Poll::Pending
                }
                1 => {
                    fut_step.set(2);
                    Poll::Ready(())
                }
                _ => panic!("future called after done"),
            }
        };
        let fut = future::poll_fn(fut_fn);
        pin_mut!(fut);
        let mut executor = TestExecutor::new_with_fake_time().unwrap();
        executor.wake_main_future();
        assert_eq!(executor.is_waiting(), WaitState::Ready);
        assert_eq!(fut_step.get(), 0);
        assert_eq!(executor.run_one_step(&mut fut), Some(Poll::Pending));
        assert_eq!(executor.is_waiting(), WaitState::Waiting(Time::INFINITE));
        assert_eq!(executor.run_one_step(&mut fut), None);
        assert_eq!(fut_step.get(), 1);

        fut_waker.borrow_mut().take().unwrap().wake();
        assert_eq!(executor.is_waiting(), WaitState::Ready);
        assert_eq!(executor.run_one_step(&mut fut), Some(Poll::Ready(())));
        assert_eq!(fut_step.get(), 2);
    }

    #[test]
    // Runs a future that waits on a timer.
    fn stepwise_timer() {
        let mut executor = TestExecutor::new_with_fake_time().unwrap();
        executor.set_fake_time(Time::from_nanos(0));
        let fut = Timer::new(Time::after(1000.nanos()));
        pin_mut!(fut);
        executor.wake_main_future();

        run_until_stalled(&mut executor, &mut fut);
        assert_eq!(Time::now(), Time::from_nanos(0));
        assert_eq!(executor.is_waiting(), WaitState::Waiting(Time::from_nanos(1000)));

        executor.set_fake_time(Time::from_nanos(1000));
        assert_eq!(Time::now(), Time::from_nanos(1000));
        assert_eq!(executor.is_waiting(), WaitState::Ready);
        assert_eq!(run_until_done(&mut executor, &mut fut), ());
    }

    // Runs a future that waits on an event.
    #[test]
    fn stepwise_event() {
        let mut executor = TestExecutor::new_with_fake_time().unwrap();
        let event = zx::Event::create().unwrap();
        let fut = OnSignals::new(&event, zx::Signals::USER_0);
        pin_mut!(fut);
        executor.wake_main_future();

        run_until_stalled(&mut executor, &mut fut);
        assert_eq!(executor.is_waiting(), WaitState::Waiting(Time::INFINITE));

        event.signal_handle(zx::Signals::NONE, zx::Signals::USER_0).unwrap();
        assert!(run_until_done(&mut executor, &mut fut).is_ok());
    }

    // Using `run_until_stalled` does not modify the order of events
    // compared to normal execution.
    #[test]
    fn run_until_stalled_preserves_order() {
        let mut executor = TestExecutor::new_with_fake_time().unwrap();
        let spawned_fut_completed = Arc::new(AtomicBool::new(false));
        let spawned_fut_completed_writer = spawned_fut_completed.clone();
        let spawned_fut = Box::pin(async move {
            Timer::new(Time::after(5.seconds())).await;
            spawned_fut_completed_writer.store(true, Ordering::SeqCst);
        });
        let main_fut = async {
            Timer::new(Time::after(10.seconds())).await;
        };
        pin_mut!(main_fut);
        spawn(spawned_fut);
        assert_eq!(executor.run_until_stalled(&mut main_fut), Poll::Pending);
        executor.set_fake_time(Time::after(15.seconds()));
        executor.wake_expired_timers();
        // The timer in `spawned_fut` should fire first, then the
        // timer in `main_fut`.
        assert_eq!(executor.run_until_stalled(&mut main_fut), Poll::Ready(()));
        assert_eq!(spawned_fut_completed.load(Ordering::SeqCst), true);
    }

    #[test]
    fn task_destruction() {
        struct DropSpawner {
            dropped: Arc<AtomicBool>,
        }
        impl Drop for DropSpawner {
            fn drop(&mut self) {
                self.dropped.store(true, Ordering::SeqCst);
                let dropped_clone = self.dropped.clone();
                spawn(async {
                    // Hold on to a reference here to verify that it, too, is destroyed later
                    let _dropped_clone = dropped_clone;
                    panic!("task spawned in drop shouldn't be polled");
                });
            }
        }
        let mut dropped = Arc::new(AtomicBool::new(false));
        let drop_spawner = DropSpawner { dropped: dropped.clone() };
        let mut executor = TestExecutor::new().unwrap();
        let main_fut = async move {
            spawn(async move {
                // Take ownership of the drop spawner
                let _drop_spawner = drop_spawner;
                future::pending::<()>().await;
            });
        };
        pin_mut!(main_fut);
        assert!(executor.run_until_stalled(&mut main_fut).is_ready());
        assert_eq!(
            dropped.load(Ordering::SeqCst),
            false,
            "executor dropped pending task before destruction"
        );

        // Should drop the pending task and it's owned drop spawner,
        // as well as gracefully drop the future spawned from the drop spawner.
        drop(executor);
        let dropped = Arc::get_mut(&mut dropped)
            .expect("someone else is unexpectedly still holding on to a reference");
        assert_eq!(
            dropped.load(Ordering::SeqCst),
            true,
            "executor did not drop pending task during destruction"
        );
    }

    #[test]
    fn time_now_real_time() {
        let _executor = LocalExecutor::new().unwrap();
        let t1 = zx::Time::after(0.seconds());
        let t2 = Time::now().into_zx();
        let t3 = zx::Time::after(0.seconds());
        assert!(t1 <= t2);
        assert!(t2 <= t3);
    }

    #[test]
    fn time_now_fake_time() {
        let executor = TestExecutor::new_with_fake_time().unwrap();
        let t1 = Time::from_zx(zx::Time::from_nanos(0));
        executor.set_fake_time(t1);
        assert_eq!(Time::now(), t1);

        let t2 = Time::from_zx(zx::Time::from_nanos(1000));
        executor.set_fake_time(t2);
        assert_eq!(Time::now(), t2);
    }

    #[test]
    fn time_after_overflow() {
        let executor = TestExecutor::new_with_fake_time().unwrap();

        executor.set_fake_time(Time::INFINITE - 100.nanos());
        assert_eq!(Time::after(200.seconds()), Time::INFINITE);

        executor.set_fake_time(Time::INFINITE_PAST + 100.nanos());
        assert_eq!(Time::after((-200).seconds()), Time::INFINITE_PAST);
    }

    // This future wakes itself up a number of times during the same cycle
    async fn multi_wake(n: usize) {
        let mut done = false;
        futures::future::poll_fn(|cx| {
            if done {
                return Poll::Ready(());
            }
            for _ in 1..n {
                cx.waker().wake_by_ref()
            }
            done = true;
            Poll::Pending
        })
        .await;
    }

    #[test]
    fn dedup_wakeups() {
        let run = |n| {
            let mut executor = LocalExecutor::new().unwrap();
            executor.run_singlethreaded(multi_wake(n));
            let snapshot = executor.inner.collector.snapshot();
            snapshot.wakeups_notification
        };
        assert_eq!(run(5), run(10)); // Same number of notifications independent of wakeup calls
    }

    // Ensure that a large amount of wakeups does not exhaust kernel resources,
    // such as the zx port queue limit.
    #[test]
    fn many_wakeups() {
        let mut executor = LocalExecutor::new().unwrap();
        executor.run_singlethreaded(multi_wake(4096 * 2));
    }
}
