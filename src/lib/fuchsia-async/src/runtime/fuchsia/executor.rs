// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::atomic_future::{AtomicFuture, AttemptPollResult};
use crate::runtime::DurationExt;
use crossbeam::queue::SegQueue;
use fuchsia_zircon::{self as zx, AsHandleRef};
use futures::future::{self, FutureObj, LocalFutureObj};
use futures::task::{waker_ref, ArcWake, AtomicWaker};
use futures::FutureExt;
use parking_lot::{Condvar, Mutex};
use pin_utils::pin_mut;
use std::cell::RefCell;
use std::collections::{BinaryHeap, HashMap};
use std::future::Future;
use std::marker::Unpin;
use std::ops::Deref;
use std::sync::atomic::{AtomicBool, AtomicI64, AtomicU32, AtomicUsize, Ordering};
use std::sync::{Arc, Weak};
use std::task::{Context, Poll, Waker};
use std::{cmp, fmt, mem, ops, thread, u64, usize};

const EMPTY_WAKEUP_ID: u64 = u64::MAX;
const TASK_READY_WAKEUP_ID: u64 = u64::MAX - 1;

/// Spawn a new task to be run on the global executor.
///
/// Tasks spawned using this method must be threadsafe (implement the `Send` trait),
/// as they may be run on either a singlethreaded or multithreaded executor.
pub(crate) fn spawn<F>(future: F)
where
    F: Future<Output = ()> + Send + 'static,
{
    Inner::spawn(&EHandle::local().inner, FutureObj::new(Box::new(future)));
}

/// Spawn a new task to be run on the global executor.
///
/// This is similar to the `spawn` function, but tasks spawned using this method
/// do not have to be threadsafe (implement the `Send` trait). In return, this method
/// requires that the current executor never be run in a multithreaded mode-- only
/// `run_singlethreaded` can be used.
pub(crate) fn spawn_local<F>(future: F)
where
    F: Future<Output = ()> + 'static,
{
    Inner::spawn_local(&EHandle::local().inner, LocalFutureObj::new(Box::new(future)));
}

/// A time relative to the executor's clock.
#[derive(Debug, Copy, Clone, Eq, PartialEq, Ord, PartialOrd, Hash)]
#[repr(transparent)]
pub struct Time(zx::Time);

impl Time {
    /// Return the current time according to the global executor.
    ///
    /// This function requires that an executor has been set up.
    pub fn now() -> Self {
        EHandle::local().inner.now()
    }

    /// Compute a deadline for the time in the future that is the
    /// given `Duration` away. Similarly to `zx::Time::after`,
    /// saturates on overflow instead of wrapping around.
    ///
    /// This function requires that an executor has been set up.
    pub fn after(duration: zx::Duration) -> Self {
        Self(zx::Time::from_nanos(Self::now().0.into_nanos().saturating_add(duration.into_nanos())))
    }

    /// Convert from `zx::Time`. This only makes sense if the time is
    /// taken from the same source (for the real clock, this is
    /// `zx::ClockId::Monotonic`).
    pub fn from_zx(t: zx::Time) -> Self {
        Time(t)
    }

    /// Convert into `zx::Time`. For the real clock, this will be a
    /// monotonic time.
    pub fn into_zx(self) -> zx::Time {
        self.0
    }

    /// Convert from nanoseconds.
    pub fn from_nanos(nanos: i64) -> Self {
        Self::from_zx(zx::Time::from_nanos(nanos))
    }

    /// Convert to nanoseconds.
    pub fn into_nanos(self) -> i64 {
        self.0.into_nanos()
    }

    /// The maximum time.
    pub const INFINITE: Time = Time(zx::Time::INFINITE);

    /// The minimum time.
    pub const INFINITE_PAST: Time = Time(zx::Time::INFINITE_PAST);
}

impl From<zx::Time> for Time {
    fn from(t: zx::Time) -> Time {
        Time(t)
    }
}

impl From<Time> for zx::Time {
    fn from(t: Time) -> zx::Time {
        t.0
    }
}

impl ops::Add<zx::Duration> for Time {
    type Output = Time;
    fn add(self, d: zx::Duration) -> Time {
        Time(self.0 + d)
    }
}

impl ops::Add<Time> for zx::Duration {
    type Output = Time;
    fn add(self, t: Time) -> Time {
        Time(self + t.0)
    }
}

impl ops::Sub<zx::Duration> for Time {
    type Output = Time;
    fn sub(self, d: zx::Duration) -> Time {
        Time(self.0 - d)
    }
}

impl ops::Sub<Time> for Time {
    type Output = zx::Duration;
    fn sub(self, t: Time) -> zx::Duration {
        self.0 - t.0
    }
}

impl ops::AddAssign<zx::Duration> for Time {
    fn add_assign(&mut self, d: zx::Duration) {
        self.0.add_assign(d)
    }
}

impl ops::SubAssign<zx::Duration> for Time {
    fn sub_assign(&mut self, d: zx::Duration) {
        self.0.sub_assign(d)
    }
}

impl DurationExt for zx::Duration {
    fn after_now(self) -> Time {
        Time::after(self)
    }
}

/// A trait for handling the arrival of a packet on a `zx::Port`.
///
/// This trait should be implemented by users who wish to write their own
/// types which receive asynchronous notifications from a `zx::Port`.
/// Implementors of this trait generally contain a `futures::task::AtomicWaker` which
/// is used to wake up the task which can make progress due to the arrival of
/// the packet.
///
/// `PacketReceiver`s should be registered with a `Core` using the
/// `register_receiver` method on `Core`, `Handle`, or `Remote`.
/// Upon registration, users will receive a `ReceiverRegistration`
/// which provides `key` and `port` methods. These methods can be used to wait on
/// asynchronous signals.
///
/// Note that `PacketReceiver`s may receive false notifications intended for a
/// previous receiver, and should handle these gracefully.
pub trait PacketReceiver: Send + Sync + 'static {
    /// Receive a packet when one arrives.
    fn receive_packet(&self, packet: zx::Packet);
}

pub(crate) fn need_signal(
    cx: &mut Context<'_>,
    task: &AtomicWaker,
    atomic_signals: &AtomicU32,
    signal: zx::Signals,
    clear_closed: bool,
    handle: zx::HandleRef<'_>,
    port: &zx::Port,
    key: u64,
) -> Result<(), zx::Status> {
    const OBJECT_PEER_CLOSED: zx::Signals = zx::Signals::OBJECT_PEER_CLOSED;

    task.register(cx.waker());
    let mut clear_signals = signal;
    if clear_closed {
        clear_signals |= OBJECT_PEER_CLOSED;
    }
    let old = zx::Signals::from_bits_truncate(
        atomic_signals.fetch_and(!clear_signals.bits(), Ordering::SeqCst),
    );
    // We only need to schedule a new packet if one isn't already scheduled.
    // If the bits were already false, a packet was already scheduled.
    let was_signal = old.contains(signal);
    let was_closed = old.contains(OBJECT_PEER_CLOSED);
    if was_closed || was_signal {
        let mut signals_to_schedule = zx::Signals::empty();
        if was_signal {
            signals_to_schedule |= signal;
        }
        if clear_closed && was_closed {
            signals_to_schedule |= OBJECT_PEER_CLOSED
        };
        schedule_packet(handle, port, key, signals_to_schedule)?;
    }
    if was_closed && !clear_closed {
        // We just missed a channel close-- go around again.
        cx.waker().wake_by_ref();
    }
    Ok(())
}

pub(crate) fn schedule_packet(
    handle: zx::HandleRef<'_>,
    port: &zx::Port,
    key: u64,
    signals: zx::Signals,
) -> Result<(), zx::Status> {
    handle.wait_async_handle(port, key, signals, zx::WaitAsyncOpts::empty())
}

/// A registration of a `PacketReceiver`.
/// When dropped, it will automatically deregister the `PacketReceiver`.
// NOTE: purposefully does not implement `Clone`.
#[derive(Debug)]
pub struct ReceiverRegistration<T: PacketReceiver> {
    receiver: Arc<T>,
    ehandle: EHandle,
    key: u64,
}

impl<T> ReceiverRegistration<T>
where
    T: PacketReceiver,
{
    /// The key with which `Packet`s destined for this receiver should be sent on the `zx::Port`.
    pub fn key(&self) -> u64 {
        self.key
    }

    /// The internal `PacketReceiver`.
    pub fn receiver(&self) -> &T {
        &*self.receiver
    }

    /// The `zx::Port` on which packets destined for this `PacketReceiver` should be queued.
    pub fn port(&self) -> &zx::Port {
        self.ehandle.port()
    }
}

impl<T: PacketReceiver> Deref for ReceiverRegistration<T> {
    type Target = T;
    fn deref(&self) -> &Self::Target {
        self.receiver()
    }
}

impl<T> Drop for ReceiverRegistration<T>
where
    T: PacketReceiver,
{
    fn drop(&mut self) {
        self.ehandle.deregister_receiver(self.key);
    }
}

/// A port-based executor for Fuchsia OS.
// NOTE: intentionally does not implement `Clone`.
pub struct Executor {
    inner: Arc<Inner>,
    // A packet that has been dequeued but not processed. This is used by `run_one_step`.
    next_packet: Option<zx::Packet>,
}

impl fmt::Debug for Executor {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("Executor").field("port", &self.inner.port).finish()
    }
}

type TimerHeap = BinaryHeap<TimeWaker>;

thread_local!(
    static EXECUTOR: RefCell<Option<(Arc<Inner>, TimerHeap)>> = RefCell::new(None)
);

fn with_local_timer_heap<F, R>(f: F) -> R
where
    F: FnOnce(&mut TimerHeap) -> R,
{
    EXECUTOR.with(|e| {
        (f)(&mut e
            .borrow_mut()
            .as_mut()
            .expect("can't get timer heap before fuchsia_async::Executor is initialized")
            .1)
    })
}

impl Executor {
    fn new_with_time(time: ExecutorTime) -> Result<Self, zx::Status> {
        let executor = Executor {
            inner: Arc::new(Inner {
                port: zx::Port::create()?,
                done: AtomicBool::new(false),
                threadiness: Threadiness::default(),
                threads: Mutex::new(Vec::new()),
                receivers: Mutex::new(PacketReceiverMap::new()),
                task_count: AtomicUsize::new(0),
                active_tasks: Mutex::new(HashMap::new()),
                ready_tasks: SegQueue::new(),
                time: time,
            }),
            next_packet: None,
        };

        executor.ehandle().set_local(TimerHeap::new());

        Ok(executor)
    }

    /// Create a new executor running with actual time.
    pub fn new() -> Result<Self, zx::Status> {
        Self::new_with_time(ExecutorTime::RealTime)
    }

    /// Create a new executor running with fake time.
    pub fn new_with_fake_time() -> Result<Self, zx::Status> {
        Self::new_with_time(ExecutorTime::FakeTime(AtomicI64::new(
            Time::INFINITE_PAST.into_nanos(),
        )))
    }

    /// Return the current time according to the executor.
    pub fn now(&self) -> Time {
        self.inner.now()
    }

    /// Set the fake time to a given value.
    pub fn set_fake_time(&self, t: Time) {
        self.inner.set_fake_time(t)
    }

    /// Return a handle to the executor.
    pub fn ehandle(&self) -> EHandle {
        EHandle { inner: self.inner.clone() }
    }

    fn singlethreaded_main_task_wake(&self) -> Waker {
        futures::task::waker(Arc::new(SingleThreadedMainTaskWake(Arc::downgrade(&self.inner))))
    }

    /// Run a single future to completion on a single thread.
    // Takes `&mut self` to ensure that only one thread-manager is running at a time.
    pub fn run_singlethreaded<F>(&mut self, main_future: F) -> F::Output
    where
        F: Future,
    {
        self.inner
            .require_real_time()
            .expect("Error: called `run_singlethreaded` on an executor using fake time");
        if let Some(_) = self.next_packet {
            panic!("Error: called `run_singlethreaded` on an executor with a packet waiting");
        }

        pin_mut!(main_future);
        let waker = self.singlethreaded_main_task_wake();
        let main_cx = &mut Context::from_waker(&waker);
        let mut res = main_future.as_mut().poll(main_cx);

        loop {
            if let Poll::Ready(res) = res {
                return res;
            }

            let packet = with_local_timer_heap(|timer_heap| {
                let deadline = next_deadline(timer_heap).map(|t| t.time).unwrap_or(Time::INFINITE);
                // into_zx: we are using real time, so the time is a monotonic time.
                match self.inner.port.wait(deadline.into_zx()) {
                    Ok(packet) => Some(packet),
                    Err(zx::Status::TIMED_OUT) => {
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
                        res = main_future.as_mut().poll(main_cx);
                    }
                    TASK_READY_WAKEUP_ID => self.inner.poll_ready_tasks(),
                    receiver_key => {
                        self.inner.deliver_packet(receiver_key as usize, packet);
                    }
                }
            }
        }
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
        self.wake_main_future();
        while let NextStep::NextPacket = self.next_step(/*fire_timers:*/ false) {
            // Will not fail, because NextPacket means there is a
            // packet ready to be processed.
            let res = self.consume_packet(main_future);
            if res.is_ready() {
                return res;
            }
        }
        Poll::Pending
    }

    /// Schedule the main future for being woken up. This is useful in conjunction with
    /// `run_one_step`.
    pub fn wake_main_future(&mut self) {
        self.inner.notify_empty()
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
        match self.next_step(/*fire_timers:*/ true) {
            NextStep::WaitUntil(_) => None,
            NextStep::NextPacket => {
                // Will not fail because NextPacket means there is a
                // packet ready to be processed.
                Some(self.consume_packet(main_future))
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
    fn consume_packet<F>(&mut self, main_future: &mut F) -> Poll<F::Output>
    where
        F: Future + Unpin,
    {
        let packet =
            self.next_packet.take().expect("consume_packet called but no packet available");
        match packet.key() {
            EMPTY_WAKEUP_ID => self.poll_main_future(main_future),
            TASK_READY_WAKEUP_ID => {
                self.inner.poll_ready_tasks();
                Poll::Pending
            }
            receiver_key => {
                self.inner.deliver_packet(receiver_key as usize, packet);
                Poll::Pending
            }
        }
    }

    fn poll_main_future<F>(&mut self, main_future: &mut F) -> Poll<F::Output>
    where
        F: Future + Unpin,
    {
        let waker = self.singlethreaded_main_task_wake();
        let main_cx = &mut Context::from_waker(&waker);
        main_future.poll_unpin(main_cx)
    }

    fn next_step(&mut self, fire_timers: bool) -> NextStep {
        // If a packet is queued from a previous call to next_step, it must be executed first.
        if let Some(_) = self.next_packet {
            return NextStep::NextPacket;
        }
        // If we are past a deadline, run the corresponding timer.
        let next_deadline = with_local_timer_heap(|timer_heap| {
            next_deadline(timer_heap).map(|t| t.time).unwrap_or(Time::INFINITE)
        });
        if fire_timers && next_deadline <= self.inner.now() {
            NextStep::NextTimer
        } else {
            // Try to unqueue a packet from the port.
            match self.inner.port.wait(zx::Time::INFINITE_PAST) {
                Ok(packet) => {
                    self.next_packet = Some(packet);
                    NextStep::NextPacket
                }
                Err(zx::Status::TIMED_OUT) => NextStep::WaitUntil(next_deadline),
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
        match self.next_step(/*fire_timers:*/ true) {
            NextStep::NextPacket | NextStep::NextTimer => WaitState::Ready,
            NextStep::WaitUntil(t) => WaitState::Waiting(t),
        }
    }

    /// Wake all tasks waiting for expired timers, and return `true` if any task was woken.
    ///
    /// This is intended for use in test code in conjunction with fake time.
    pub fn wake_expired_timers(&mut self) -> bool {
        let now = self.now();
        with_local_timer_heap(|timer_heap| {
            let mut ret = false;
            while let Some(waker) = next_deadline(timer_heap).filter(|waker| waker.time <= now) {
                waker.wake();
                timer_heap.pop();
                ret = true;
            }
            ret
        })
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
            let deadline = next_deadline(timer_heap).map(|waker| {
                waker.wake();
                waker.time
            });
            if deadline.is_some() {
                timer_heap.pop();
            }
            deadline
        })
    }

    /// Run a single future to completion using multiple threads.
    // Takes `&mut self` to ensure that only one thread-manager is running at a time.
    pub fn run<F>(&mut self, future: F, num_threads: usize) -> F::Output
    where
        F: Future + Send + 'static,
        F::Output: Send + 'static,
    {
        self.inner.require_real_time().expect("Error: called `run` on an executor using fake time");
        self.inner.threadiness.require_multithreaded().expect(
            "Error: called `run` on executor after using `spawn_local`. \
             Use `run_singlethreaded` instead.",
        );
        if let Some(_) = self.next_packet {
            panic!("Error: called `run` on an executor with a packet waiting");
        }

        let pair = Arc::new((Mutex::new(None), Condvar::new()));
        let pair2 = pair.clone();

        // Spawn a future which will set the result upon completion.
        Inner::spawn(
            &self.inner,
            FutureObj::new(Box::new(future.then(move |fut_result| {
                let (lock, cvar) = &*pair2;
                let mut result = lock.lock();
                *result = Some(fut_result);
                cvar.notify_one();
                future::ready(())
            }))),
        );

        // Start worker threads, handing off timers from the current thread.
        self.inner.done.store(false, Ordering::SeqCst);
        with_local_timer_heap(|timer_heap| {
            let timer_heap = mem::replace(timer_heap, TimerHeap::new());
            self.create_worker_threads(num_threads, Some(timer_heap));
        });

        // Wait until the signal the future has completed.
        let (lock, cvar) = &*pair;
        let mut result = lock.lock();
        while result.is_none() {
            cvar.wait(&mut result);
        }

        // Spin down worker threads
        self.inner.done.store(true, Ordering::SeqCst);
        self.join_all();

        // Unwrap is fine because of the check to `is_none` above.
        result.take().unwrap()
    }

    /// Add `num_workers` worker threads to the executor's thread pool.
    /// `timers`: timers from the "master" thread which would otherwise be lost.
    fn create_worker_threads(&self, num_workers: usize, mut timers: Option<TimerHeap>) {
        let mut threads = self.inner.threads.lock();
        for _ in 0..num_workers {
            threads.push(self.new_worker(timers.take()));
        }
    }

    fn join_all(&self) {
        let mut threads = self.inner.threads.lock();

        // Send a user packet to wake up all the threads
        for _thread in threads.iter() {
            self.inner.notify_empty();
        }

        // Join the worker threads
        for thread in threads.drain(..) {
            thread.join().expect("Couldn't join worker thread.");
        }
    }

    fn new_worker(&self, timers: Option<TimerHeap>) -> thread::JoinHandle<()> {
        let inner = self.inner.clone();
        thread::spawn(move || Self::worker_lifecycle(inner, timers))
    }

    fn worker_lifecycle(inner: Arc<Inner>, timers: Option<TimerHeap>) {
        let executor: EHandle = EHandle { inner: inner.clone() };
        executor.set_local(timers.unwrap_or(TimerHeap::new()));
        loop {
            if inner.done.load(Ordering::SeqCst) {
                EHandle::rm_local();
                return;
            }

            let packet = with_local_timer_heap(|timer_heap| {
                let deadline = next_deadline(timer_heap).map(|t| t.time).unwrap_or(Time::INFINITE);
                // into_zx: we are using real time, so the time is a monotonic time.
                match inner.port.wait(deadline.into_zx()) {
                    Ok(packet) => Some(packet),
                    Err(zx::Status::TIMED_OUT) => {
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
                    EMPTY_WAKEUP_ID => {}
                    TASK_READY_WAKEUP_ID => inner.poll_ready_tasks(),
                    receiver_key => {
                        inner.deliver_packet(receiver_key as usize, packet);
                    }
                }
            }
        }
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

fn next_deadline(heap: &mut TimerHeap) -> Option<&TimeWaker> {
    while is_defunct_timer(heap.peek()) {
        heap.pop();
    }
    heap.peek()
}

fn is_defunct_timer(timer: Option<&TimeWaker>) -> bool {
    match timer {
        None => false,
        Some(timer) => timer.waker_and_bool.upgrade().is_none(),
    }
}

// Since there are no other threads running, we don't have to use the EMPTY_WAKEUP_ID,
// so instead we save it for use as the main task wakeup id.
struct SingleThreadedMainTaskWake(Weak<Inner>);
impl ArcWake for SingleThreadedMainTaskWake {
    fn wake_by_ref(arc_self: &Arc<Self>) {
        if let Some(executor) = Weak::upgrade(&arc_self.0) {
            executor.notify_empty();
        }
    }
}

impl Drop for Executor {
    fn drop(&mut self) {
        // Done flag must be set before dropping packet receivers
        // so that future receivers that attempt to deregister themselves
        // know that it's okay if their entries are already missing.
        self.inner.done.store(true, Ordering::SeqCst);

        // Wake the threads so they can kill themselves.
        self.join_all();

        // Drop all of the packet receivers
        self.inner.receivers.lock().clear();

        // Drop all of the uncompleted tasks
        while let Ok(_) = self.inner.ready_tasks.pop() {}

        // Drop all tasks
        self.inner.active_tasks.lock().clear();

        // Remove the thread-local executor set in `new`.
        EHandle::rm_local();
    }
}

/// A handle to an executor.
#[derive(Clone)]
pub struct EHandle {
    inner: Arc<Inner>,
}

impl fmt::Debug for EHandle {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("EHandle").field("port", &self.inner.port).finish()
    }
}

impl EHandle {
    /// Returns the thread-local executor.
    pub fn local() -> Self {
        let inner = EXECUTOR
            .with(|e| e.borrow().as_ref().map(|x| x.0.clone()))
            .expect("Fuchsia Executor must be created first");

        EHandle { inner }
    }

    fn set_local(self, timers: TimerHeap) {
        let inner = self.inner.clone();
        EXECUTOR.with(|e| {
            let mut e = e.borrow_mut();
            assert!(e.is_none(), "Cannot create multiple Fuchsia Executors");
            *e = Some((inner, timers));
        });
    }

    fn rm_local() {
        EXECUTOR.with(|e| *e.borrow_mut() = None);
    }

    /// Get a reference to the Fuchsia `zx::Port` being used to listen for events.
    pub fn port(&self) -> &zx::Port {
        &self.inner.port
    }

    /// Registers a `PacketReceiver` with the executor and returns a registration.
    /// The `PacketReceiver` will be deregistered when the `Registration` is dropped.
    pub fn register_receiver<T>(&self, receiver: Arc<T>) -> ReceiverRegistration<T>
    where
        T: PacketReceiver,
    {
        let key = self.inner.receivers.lock().insert(receiver.clone()) as u64;

        ReceiverRegistration { ehandle: self.clone(), key, receiver }
    }

    fn deregister_receiver(&self, key: u64) {
        let key = key as usize;
        let mut lock = self.inner.receivers.lock();
        if lock.contains(key) {
            lock.remove(key);
        } else {
            // The executor is shutting down and already removed the entry.
            assert!(self.inner.done.load(Ordering::SeqCst), "Missing receiver to deregister");
        }
    }

    pub(crate) fn register_timer(
        &self,
        time: Time,
        waker_and_bool: &Arc<(AtomicWaker, AtomicBool)>,
    ) {
        with_local_timer_heap(|timer_heap| {
            let waker_and_bool = Arc::downgrade(waker_and_bool);
            timer_heap.push(TimeWaker { time, waker_and_bool })
        })
    }
}

/// The executor has not been run in multithreaded mode and no thread-unsafe
/// futures have been spawned.
const THREADINESS_ANY: usize = 0;
/// The executor has not been run in multithreaded mode, but thread-unsafe
/// futures have been spawned, so it cannot ever be run in multithreaded mode.
const THREADINESS_SINGLE: usize = 1;
/// The executor has been run in multithreaded mode.
/// No thread-unsafe futures can be spawned.
const THREADINESS_MULTI: usize = 2;

/// Tracks the multithreaded-compatibility state of the executor.
struct Threadiness(AtomicUsize);

impl Default for Threadiness {
    fn default() -> Self {
        Threadiness(AtomicUsize::new(THREADINESS_ANY))
    }
}

impl Threadiness {
    fn try_become(&self, target: usize) -> Result<(), ()> {
        match self.0.compare_exchange(
            /* current */ THREADINESS_ANY,
            /* new */ target,
            Ordering::Relaxed,
            Ordering::Relaxed,
        ) {
            Ok(_) => Ok(()),
            Err(x) if x == target => Ok(()),
            Err(_) => Err(()),
        }
    }

    /// Attempts to switch the threadiness to singlethreaded-only mode.
    /// Will fail iff a prior call to `require_multithreaded` was made.
    fn require_singlethreaded(&self) -> Result<(), ()> {
        self.try_become(THREADINESS_SINGLE)
    }

    /// Attempts to switch the threadiness to multithreaded mode.
    /// Will fail iff a prior call to `require_singlethreaded` was made.
    fn require_multithreaded(&self) -> Result<(), ()> {
        self.try_become(THREADINESS_MULTI)
    }
}

enum ExecutorTime {
    RealTime,
    FakeTime(AtomicI64),
}

// Simple slab::Slab replacement that doesn't re-use keys
// TODO(fxbug.dev/43101): figure out how to safely cancel async waits so we can re-use keys again.
struct PacketReceiverMap<T> {
    next_key: usize,
    mapping: HashMap<usize, T>,
}

impl<T> PacketReceiverMap<T> {
    fn new() -> Self {
        Self { next_key: 0, mapping: HashMap::new() }
    }

    fn clear(&mut self) {
        self.mapping.clear()
    }

    fn get(&self, key: usize) -> Option<&T> {
        self.mapping.get(&key)
    }

    fn insert(&mut self, val: T) -> usize {
        let key = self.next_key;
        self.next_key = self.next_key.checked_add(1).expect("ran out of keys");
        self.mapping.insert(key, val);
        key
    }

    fn remove(&mut self, key: usize) -> T {
        self.mapping.remove(&key).unwrap_or_else(|| panic!("invalid key"))
    }

    fn contains(&self, key: usize) -> bool {
        self.mapping.contains_key(&key)
    }
}

struct Inner {
    port: zx::Port,
    done: AtomicBool,
    threadiness: Threadiness,
    threads: Mutex<Vec<thread::JoinHandle<()>>>,
    receivers: Mutex<PacketReceiverMap<Arc<dyn PacketReceiver>>>,
    task_count: AtomicUsize,
    active_tasks: Mutex<HashMap<usize, Arc<Task>>>,
    ready_tasks: SegQueue<Arc<Task>>,
    time: ExecutorTime,
}

struct TimeWaker {
    time: Time,
    waker_and_bool: Weak<(AtomicWaker, AtomicBool)>,
}

impl TimeWaker {
    fn wake(&self) {
        if let Some(wb) = self.waker_and_bool.upgrade() {
            wb.1.store(true, Ordering::SeqCst);
            wb.0.wake();
        }
    }
}

impl Ord for TimeWaker {
    fn cmp(&self, other: &Self) -> cmp::Ordering {
        self.time.cmp(&other.time).reverse() // Reverse to get min-heap rather than max
    }
}

impl PartialOrd for TimeWaker {
    fn partial_cmp(&self, other: &Self) -> Option<cmp::Ordering> {
        Some(self.cmp(other))
    }
}

impl Eq for TimeWaker {}

// N.B.: two TimerWakers can be equal even if they don't have the same
// waker_and_bool. This is fine since BinaryHeap doesn't deduplicate.
impl PartialEq for TimeWaker {
    fn eq(&self, other: &Self) -> bool {
        self.time == other.time
    }
}

impl Inner {
    fn poll_ready_tasks(&self) {
        // TODO: loop but don't starve
        if let Ok(task) = self.ready_tasks.pop() {
            if task.try_poll() {
                // Completed
                self.active_tasks.lock().remove(&task.id);
            }
        }
    }

    fn spawn(self: &Arc<Self>, future: FutureObj<'static, ()>) {
        // Prevent a deadlock in `.active_tasks` when a task is spawned from a custom
        // Drop impl while the executor is being torn down.
        if self.done.load(Ordering::SeqCst) {
            return;
        }
        let next_id = self.task_count.fetch_add(1, Ordering::Relaxed);
        let task = Task::new(next_id, future, self.clone());
        let waker = task.waker();
        self.active_tasks.lock().insert(next_id, task);
        ArcWake::wake_by_ref(&waker);
    }

    fn spawn_local(self: &Arc<Self>, future: LocalFutureObj<'static, ()>) {
        self.threadiness.require_singlethreaded().expect(
            "Error: called `spawn_local` after calling `run` on executor. \
             Use `spawn` or `run_singlethreaded` instead.",
        );
        Inner::spawn(
            self,
            // Unsafety: we've confirmed that the boxed futures here will never be used
            // across multiple threads, so we can safely convert from a non-`Send`able
            // future to a `Send`able one.
            unsafe { future.into_future_obj() },
        )
    }

    fn notify_task_ready(&self) {
        // TODO: optimize so that this function doesn't push new items onto
        // the queue if all worker threads are already awake
        self.notify_id(TASK_READY_WAKEUP_ID);
    }

    fn notify_empty(&self) {
        self.notify_id(EMPTY_WAKEUP_ID);
    }

    fn notify_id(&self, id: u64) {
        let up = zx::UserPacket::from_u8_array([0; 32]);
        let packet = zx::Packet::from_user_packet(id, 0 /* status??? */, up);
        if let Err(e) = self.port.queue(&packet) {
            // TODO: logging
            eprintln!("Failed to queue notify in port: {:?}", e);
        }
    }

    fn deliver_packet(&self, key: usize, packet: zx::Packet) {
        let receiver = match self.receivers.lock().get(key) {
            // Clone the `Arc` so that we don't hold the lock
            // any longer than absolutely necessary.
            // The `receive_packet` impl may be arbitrarily complex.
            Some(receiver) => receiver.clone(),
            None => return,
        };
        receiver.receive_packet(packet);
    }

    fn now(&self) -> Time {
        match &self.time {
            ExecutorTime::RealTime => Time::from_zx(zx::Time::get(zx::ClockId::Monotonic)),
            ExecutorTime::FakeTime(t) => Time::from_nanos(t.load(Ordering::Relaxed)),
        }
    }

    fn set_fake_time(&self, new: Time) {
        match &self.time {
            ExecutorTime::RealTime => {
                panic!("Error: called `advance_fake_time` on an executor using actual time.")
            }
            ExecutorTime::FakeTime(t) => t.store(new.into_nanos(), Ordering::Relaxed),
        }
    }

    fn require_real_time(&self) -> Result<(), ()> {
        match self.time {
            ExecutorTime::RealTime => Ok(()),
            ExecutorTime::FakeTime(_) => Err(()),
        }
    }
}

struct Task {
    id: usize,
    future: AtomicFuture,
    executor: Arc<Inner>,
}

impl Task {
    fn new(id: usize, future: FutureObj<'static, ()>, executor: Arc<Inner>) -> Arc<Self> {
        Arc::new(Self { id, future: AtomicFuture::new(future), executor })
    }

    fn waker(self: &Arc<Self>) -> Arc<TaskWaker> {
        Arc::new(TaskWaker { task: Arc::downgrade(self) })
    }

    fn try_poll(self: &Arc<Self>) -> bool {
        let task_waker = self.waker();
        let w = waker_ref(&task_waker);
        self.future.try_poll(&mut Context::from_waker(&w)) == AttemptPollResult::IFinished
    }
}

struct TaskWaker {
    task: Weak<Task>,
}

impl ArcWake for TaskWaker {
    fn wake_by_ref(arc_self: &Arc<Self>) {
        if let Some(task) = Weak::upgrade(&arc_self.task) {
            task.executor.ready_tasks.push(task.clone());
            task.executor.notify_task_ready();
        }
    }
}

#[cfg(test)]
mod tests {
    use core::task::{Context, Waker};
    use fuchsia_zircon::{self as zx, AsHandleRef, DurationNum};
    use futures::{future::poll_fn, Future};
    use std::cell::{Cell, RefCell};
    use std::rc::Rc;
    use std::task::Poll;

    use super::*;
    use crate::{handle::on_signals::OnSignals, Timer};

    fn time_operations_param(zxt1: zx::Time, zxt2: zx::Time, d: zx::Duration) {
        let t1 = Time::from_zx(zxt1);
        let t2 = Time::from_zx(zxt2);
        assert_eq!(t1.into_zx(), zxt1);

        assert_eq!(Time::from_zx(zx::Time::INFINITE), Time::INFINITE);
        assert_eq!(Time::from_zx(zx::Time::INFINITE_PAST), Time::INFINITE_PAST);
        assert_eq!(zxt1 - zxt2, t1 - t2);
        assert_eq!(zxt1 + d, (t1 + d).into_zx());
        assert_eq!(d + zxt1, (d + t1).into_zx());
        assert_eq!(zxt1 - d, (t1 - d).into_zx());

        let mut zxt = zxt1;
        let mut t = t1;
        t += d;
        zxt += d;
        assert_eq!(zxt, t.into_zx());
        t -= d;
        zxt -= d;
        assert_eq!(zxt, t.into_zx());
    }

    #[test]
    fn time_operations() {
        time_operations_param(zx::Time::from_nanos(0), zx::Time::from_nanos(1000), 12.seconds());
        time_operations_param(
            zx::Time::from_nanos(-100000),
            zx::Time::from_nanos(65324),
            (-785).hours(),
        );
    }

    #[test]
    fn time_now_real_time() {
        let _executor = Executor::new().unwrap();
        let t1 = zx::Time::after(0.seconds());
        let t2 = Time::now().into_zx();
        let t3 = zx::Time::after(0.seconds());
        assert!(t1 <= t2);
        assert!(t2 <= t3);
    }

    #[test]
    fn time_now_fake_time() {
        let executor = Executor::new_with_fake_time().unwrap();
        let t1 = Time::from_zx(zx::Time::from_nanos(0));
        executor.set_fake_time(t1);
        assert_eq!(Time::now(), t1);

        let t2 = Time::from_zx(zx::Time::from_nanos(1000));
        executor.set_fake_time(t2);
        assert_eq!(Time::now(), t2);
    }

    #[test]
    fn time_after_overflow() {
        let executor = Executor::new_with_fake_time().unwrap();

        executor.set_fake_time(Time::INFINITE - 100.nanos());
        assert_eq!(Time::after(200.seconds()), Time::INFINITE);

        executor.set_fake_time(Time::INFINITE_PAST + 100.nanos());
        assert_eq!(Time::after((-200).seconds()), Time::INFINITE_PAST);
    }

    fn run_until_stalled<F>(executor: &mut Executor, fut: &mut F)
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

    fn run_until_done<F>(executor: &mut Executor, fut: &mut F) -> F::Output
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
        let fut = poll_fn(fut_fn);
        pin_mut!(fut);
        let mut executor = Executor::new_with_fake_time().unwrap();
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
        let mut executor = Executor::new_with_fake_time().unwrap();
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
        let mut executor = Executor::new_with_fake_time().unwrap();
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
        let mut executor = Executor::new_with_fake_time().unwrap();
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
    fn packet_receiver_map_does_not_reuse_keys() {
        #[derive(Debug, Copy, Clone, PartialEq)]
        struct DummyPacketReceiver {
            id: i32,
        }
        let mut map = PacketReceiverMap::<DummyPacketReceiver>::new();
        let e1 = DummyPacketReceiver { id: 1 };
        assert_eq!(map.insert(e1), 0);
        assert_eq!(map.insert(e1), 1);

        // Still doesn't reuse IDs after one is removed
        map.remove(1);
        assert_eq!(map.insert(e1), 2);

        // Still doesn't reuse IDs after map is cleared
        map.clear();
        assert_eq!(map.insert(e1), 3);
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
                super::spawn(async {
                    // Hold on to a reference here to verify that it, too, is destroyed later
                    let _dropped_clone = dropped_clone;
                    panic!("task spawned in drop shouldn't be polled");
                });
            }
        }
        let mut dropped = Arc::new(AtomicBool::new(false));
        let drop_spawner = DropSpawner { dropped: dropped.clone() };
        let mut executor = Executor::new().unwrap();
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
}
