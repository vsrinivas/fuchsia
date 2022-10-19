// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::super::timer::{TimerHandle, TimerHeap};
use super::{
    instrumentation::{Collector, LocalCollector},
    packets::{PacketReceiver, PacketReceiverMap, ReceiverRegistration},
    time::Time,
};
use crate::atomic_future::{AtomicFuture, AttemptPollResult};
use crossbeam::queue::SegQueue;
use fuchsia_zircon::{self as zx};
use futures::{
    future::{FutureObj, LocalFutureObj},
    task::{waker_ref, ArcWake},
};
use parking_lot::Mutex;
use std::{
    cell::RefCell,
    collections::HashMap,
    fmt,
    panic::Location,
    sync::atomic::{AtomicBool, AtomicI64, AtomicUsize, Ordering},
    sync::{Arc, Weak},
    task::Context,
    u64, usize,
};

pub(crate) const EMPTY_WAKEUP_ID: u64 = u64::MAX;
pub(crate) const TASK_READY_WAKEUP_ID: u64 = u64::MAX - 1;

/// The id of the main task, which is a virtual task that lives from construction
/// to destruction of the executor. The main task may correspond to multiple
/// main futures, in cases where the executor runs multiple times during its lifetime.
pub(crate) const MAIN_TASK_ID: usize = 0;

thread_local!(
    static EXECUTOR: RefCell<Option<(Arc<Inner>, TimerHeap)>> = RefCell::new(None)
);

pub(crate) fn with_local_timer_heap<F, R>(f: F) -> R
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

pub enum ExecutorTime {
    RealTime,
    FakeTime(AtomicI64),
}

pub(super) struct Inner {
    pub(super) port: zx::Port,
    pub(super) done: AtomicBool,
    is_local: bool,
    receivers: Mutex<PacketReceiverMap<Arc<dyn PacketReceiver>>>,
    task_count: AtomicUsize,
    active_tasks: Mutex<HashMap<usize, Arc<Task>>>,
    pub(super) ready_tasks: SegQueue<Arc<Task>>,
    time: ExecutorTime,
    pub(super) collector: Collector,
    pub(super) source: Option<&'static Location<'static>>,
}

impl Inner {
    #[cfg_attr(trace_level_logging, track_caller)]
    pub fn new(time: ExecutorTime, is_local: bool) -> Result<Self, zx::Status> {
        #[cfg(trace_level_logging)]
        let source = Some(Location::caller());
        #[cfg(not(trace_level_logging))]
        let source = None;

        let collector = Collector::new();
        collector.task_created(MAIN_TASK_ID, source);
        Ok(Inner {
            port: zx::Port::create()?,
            done: AtomicBool::new(false),
            is_local,
            receivers: Mutex::new(PacketReceiverMap::new()),
            task_count: AtomicUsize::new(MAIN_TASK_ID + 1),
            active_tasks: Mutex::new(HashMap::new()),
            ready_tasks: SegQueue::new(),
            time,
            collector,
            source,
        })
    }

    pub fn set_local(self: Arc<Self>, timers: TimerHeap) {
        EXECUTOR.with(|e| {
            let mut e = e.borrow_mut();
            assert!(e.is_none(), "Cannot create multiple Fuchsia Executors");
            *e = Some((self, timers));
        });
    }

    pub fn poll_ready_tasks(&self, local_collector: &mut LocalCollector<'_>) {
        // TODO: loop but don't starve
        if let Some(task) = self.ready_tasks.pop() {
            let complete = task.try_poll();
            local_collector.task_polled(task.id, task.source, complete, self.ready_tasks.len());
            if complete {
                // Completed
                self.active_tasks.lock().remove(&task.id);
            }
        }
    }

    #[cfg_attr(trace_level_logging, track_caller)]
    pub fn spawn(self: &Arc<Self>, future: FutureObj<'static, ()>) {
        // Prevent a deadlock in `.active_tasks` when a task is spawned from a custom
        // Drop impl while the executor is being torn down.
        if self.done.load(Ordering::SeqCst) {
            return;
        }
        let next_id = self.task_count.fetch_add(1, Ordering::Relaxed);
        let task = Task::new(next_id, future, self.clone());
        self.collector.task_created(next_id, task.source);
        let waker = task.waker();
        self.active_tasks.lock().insert(next_id, task);
        ArcWake::wake_by_ref(&waker);
    }

    #[cfg_attr(trace_level_logging, track_caller)]
    pub fn spawn_local(self: &Arc<Self>, future: LocalFutureObj<'static, ()>) {
        if !self.is_local {
            panic!(
                "Error: called `spawn_local` on multithreaded executor. \
                Use `spawn` or a `LocalExecutor` instead."
            );
        }
        Inner::spawn(
            self,
            // Unsafety: we've confirmed that the boxed futures here will never be used
            // across multiple threads, so we can safely convert from a non-`Send`able
            // future to a `Send`able one.
            unsafe { future.into_future_obj() },
        )
    }

    pub fn notify_task_ready(&self) {
        // TODO: optimize so that this function doesn't push new items onto
        // the queue if all worker threads are already awake
        self.notify_id(TASK_READY_WAKEUP_ID);
    }

    pub fn notify_empty(&self) {
        self.notify_id(EMPTY_WAKEUP_ID);
    }

    pub fn notify_id(&self, id: u64) {
        let up = zx::UserPacket::from_u8_array([0; 32]);
        let packet = zx::Packet::from_user_packet(id, 0 /* status??? */, up);
        if let Err(e) = self.port.queue(&packet) {
            // TODO: logging
            eprintln!("Failed to queue notify in port: {:?}", e);
        }
    }

    pub fn deliver_packet(&self, key: usize, packet: zx::Packet) {
        let receiver = match self.receivers.lock().get(key) {
            // Clone the `Arc` so that we don't hold the lock
            // any longer than absolutely necessary.
            // The `receive_packet` impl may be arbitrarily complex.
            Some(receiver) => receiver.clone(),
            None => return,
        };
        receiver.receive_packet(packet);
    }

    pub fn now(&self) -> Time {
        match &self.time {
            ExecutorTime::RealTime => Time::from_zx(zx::Time::get_monotonic()),
            ExecutorTime::FakeTime(t) => Time::from_nanos(t.load(Ordering::Relaxed)),
        }
    }

    pub fn set_fake_time(&self, new: Time) {
        match &self.time {
            ExecutorTime::RealTime => {
                panic!("Error: called `advance_fake_time` on an executor using actual time.")
            }
            ExecutorTime::FakeTime(t) => t.store(new.into_nanos(), Ordering::Relaxed),
        }
    }

    pub fn require_real_time(&self) -> Result<(), ()> {
        match self.time {
            ExecutorTime::RealTime => Ok(()),
            ExecutorTime::FakeTime(_) => Err(()),
        }
    }

    /// Must be called before `on_parent_drop`.
    ///
    /// Done flag must be set before dropping packet receivers
    /// so that future receivers that attempt to deregister themselves
    /// know that it's okay if their entries are already missing.
    pub fn mark_done(&self) {
        self.done.store(true, Ordering::SeqCst);
    }

    /// Notes about the lifecycle of an Executor.
    ///
    /// a) The Executor stands as the only way to run a reactor based on a Fuchsia port, but the
    /// lifecycle of the port itself is not currently tied to it. Executor vends clones of its
    /// inner Arc structure to all receivers, so we don't have a type-safe way of ensuring that
    /// the port is dropped alongside the Executor as it should.
    /// TODO(https://fxbug.dev/75075): Ensure the port goes away with the executor.
    ///
    /// b) The Executor's lifetime is also tied to the thread-local variable pointing to the
    /// "current" executor being set, and that's unset when the executor is dropped.
    ///
    /// Point (a) is related to "what happens if I use a receiver after the executor is dropped",
    /// and point (b) is related to "what happens when I try to create a new receiver when there
    /// is no executor".
    ///
    /// Tokio, for example, encodes the lifetime of the reactor separately from the thread-local
    /// storage [1]. And the reactor discourages usage of strong references to it by vending weak
    /// references to it [2] instead of strong.
    ///
    /// There are pros and cons to both strategies. For (a), tokio encourages (but doesn't
    /// enforce [3]) type-safety by vending weak pointers, but those add runtime overhead when
    /// upgrading pointers. For (b) the difference mostly stand for "when is it safe to use IO
    /// objects/receivers". Tokio says it's only safe to use them whenever a guard is in scope.
    /// Fuchsia-async says it's safe to use them when a fuchsia_async::Executor is still in scope
    /// in that thread.
    ///
    /// This acts as a prelude to the panic encoded in Executor::drop when receivers haven't
    /// unregistered themselves when the executor drops. The choice to panic was made based on
    /// patterns in fuchsia-async that may come to change:
    ///
    /// - Executor vends strong references to itself and those references are *stored* by most
    /// receiver implementations (as opposed to reached out on TLS every time).
    /// - Fuchsia-async objects return zx::Status on wait calls, there isn't an appropriate and
    /// easy to understand error to return when polling on an extinct executor.
    /// - All receivers are implemented in this crate and well-known.
    ///
    /// [1]: https://docs.rs/tokio/1.5.0/tokio/runtime/struct.Runtime.html#method.enter
    /// [2]: https://github.com/tokio-rs/tokio/blob/b42f21ec3e212ace25331d0c13889a45769e6006/tokio/src/signal/unix/driver.rs#L35
    /// [3]: by returning an upgraded Arc, tokio trusts callers to not "use it for too long", an
    /// opaque non-clone-copy-or-send guard would be stronger than this. See:
    /// https://github.com/tokio-rs/tokio/blob/b42f21ec3e212ace25331d0c13889a45769e6006/tokio/src/io/driver/mod.rs#L297
    pub fn on_parent_drop(&self) {
        // Drop all tasks
        self.active_tasks.lock().clear();

        // Drop all of the uncompleted tasks
        while let Some(_) = self.ready_tasks.pop() {}

        // Synthetic main task marked completed
        self.collector.task_completed(MAIN_TASK_ID, self.source);

        // Do not allow any receivers to outlive the executor. That's very likely a bug waiting to
        // happen. See discussion above.
        //
        // If you're here because you hit this panic check your code for:
        //
        // - A struct that contains a fuchsia_async::Executor NOT in the last position (last
        // position gets dropped last: https://doc.rust-lang.org/reference/destructors.html).
        //
        // - A function scope that contains a fuchsia_async::Executor NOT in the first position
        // (first position in function scope gets dropped last:
        // https://doc.rust-lang.org/reference/destructors.html?highlight=scope#drop-scopes).
        //
        // - A function that holds a `fuchsia_async::Executor` in scope and whose last statement
        // contains a temporary (temporaries are dropped after the function scope:
        // https://doc.rust-lang.org/reference/destructors.html#temporary-scopes). This usually
        // looks like a `match` statement at the end of the function without a semicolon.
        //
        // - Storing channel and FIDL objects in static variables.
        //
        // - fuchsia_async::unblock calls that move channels or FIDL objects to another thread.
        assert!(
            self.receivers.lock().mapping.is_empty(),
            "receivers must not outlive their executor"
        );

        // Remove the thread-local executor set in `new`.
        EHandle::rm_local();
    }
}

/// A handle to an executor.
#[derive(Clone)]
pub struct EHandle {
    pub(super) inner: Arc<Inner>,
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

    pub(super) fn rm_local() {
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

    pub(crate) fn deregister_receiver(&self, key: u64) {
        let key = key as usize;
        let mut lock = self.inner.receivers.lock();
        if lock.contains(key) {
            lock.remove(key);
        } else {
            // The executor is shutting down and already removed the entry.
            assert!(self.inner.done.load(Ordering::SeqCst), "Missing receiver to deregister");
        }
    }

    pub(crate) fn register_timer(time: Time, handle: TimerHandle) {
        with_local_timer_heap(|timer_heap| {
            timer_heap.add_timer(time, handle);
        });
    }
}

pub(super) struct Task {
    id: usize,
    future: AtomicFuture,
    executor: Arc<Inner>,
    notifier: Notifier,
    source: Option<&'static Location<'static>>,
}

impl Task {
    #[cfg_attr(trace_level_logging, track_caller)]
    fn new(id: usize, future: FutureObj<'static, ()>, executor: Arc<Inner>) -> Arc<Self> {
        #[cfg(trace_level_logging)]
        let source = Some(Location::caller());
        #[cfg(not(trace_level_logging))]
        let source = None;

        Arc::new(Self {
            id,
            future: AtomicFuture::new(future),
            executor,
            notifier: Notifier::default(),
            source,
        })
    }

    fn waker(self: &Arc<Self>) -> Arc<TaskWaker> {
        Arc::new(TaskWaker { task: Arc::downgrade(self) })
    }

    fn try_poll(self: &Arc<Self>) -> bool {
        let task_waker = self.waker();
        let w = waker_ref(&task_waker);
        self.notifier.reset();
        self.future.try_poll(&mut Context::from_waker(&w)) == AttemptPollResult::IFinished
    }
}

struct TaskWaker {
    task: Weak<Task>,
}

impl ArcWake for TaskWaker {
    fn wake_by_ref(arc_self: &Arc<Self>) {
        if let Some(task) = Weak::upgrade(&arc_self.task) {
            if task.notifier.prepare_notify() {
                task.executor.ready_tasks.push(task.clone());
                task.executor.notify_task_ready();
            }
        }
    }
}

/// Notifier is a helper which de-duplicates task wakeups. When embedded in a task, it keeps
/// track of whether the task has been notified or not. This optimization is possible due
/// to the futures contract which specifies that poll can occur any number of times, and as
/// such the poll count must not be relied upon.
#[derive(Default)]
pub(crate) struct Notifier {
    notified: AtomicBool,
}

impl Notifier {
    /// Prepare for notification and enqueuing the task. If true, the caller should proceed with
    /// scheduling the task. If false, another worker will ensure that this happens.
    pub fn prepare_notify(&self) -> bool {
        self.notified.compare_exchange(false, true, Ordering::AcqRel, Ordering::Acquire).is_ok()
    }

    /// Reset the notification. Should be called prior to polling the task again.
    pub fn reset(&self) {
        self.notified.store(false, Ordering::Release);
    }
}
