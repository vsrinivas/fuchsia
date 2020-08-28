// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Values of this type represent "execution scopes" used by the library to give fine grained
//! control of the lifetimes of the tasks associated with particular connections.  When a new
//! connection is attached to a pseudo directory tree, an execution scope is provided.  This scope
//! is then used to start any tasks related to this connection.  All connections opened as a result
//! of operations on this first connection will also use the same scope, as well as any tasks
//! related to those connections.
//!
//! This way, it is possible to control the lifetime of a group of connections.  All connections
//! and their tasks can be shutdown by calling `shutdown` method on the scope that is hosting them.
//! Scope will also shutdown all the tasks when it goes out of scope.
//!
//! Implementation wise, execution scope is just a proxy, that forwards all the tasks to an actual
//! executor, provided as an instance of a [`Spawn`] trait.

use crate::{
    directory::mutable::entry_constructor::EntryConstructor,
    registry::{InodeRegistry, TokenRegistry},
};

use {
    futures::{
        channel::oneshot,
        task::{self, Context, Poll, Spawn},
        Future, FutureExt,
    },
    parking_lot::Mutex,
    pin_project::pin_project,
    slab::Slab,
    std::{ops::Drop, pin::Pin, sync::Arc},
};

pub type SpawnError = task::SpawnError;

/// An execution scope that is hosting tasks for a group of connections.  See the module level
/// documentation for details.
///
/// Actual execution will be delegated to an "upstream" executor - something that implements
/// [`Spawn`].  In a sense, this is somewhat of an analog of a multithreaded capable
/// [`FuturesUnordered`], but this some additional functionality specific to the vfs
/// library.
///
/// Use [`ExecutionScope::new()`] or [`ExecutionScope::build()`] to construct new
/// `ExecutionScope`es.
pub struct ExecutionScope {
    executor: Arc<Mutex<Executor>>,

    token_registry: Option<Arc<dyn TokenRegistry + Send + Sync>>,

    inode_registry: Option<Arc<dyn InodeRegistry + Send + Sync>>,

    entry_constructor: Option<Arc<dyn EntryConstructor + Send + Sync>>,
}

struct Executor {
    /// This is a list of shutdown channels for all the tasks that might be currently running.
    /// When we initiate a task shutdown by sending a message over the channel, but as we need to
    /// consume the sender in the process, we use `Option`s turning the consumed ones into `None`s.
    running: Slab<Option<oneshot::Sender<()>>>,

    /// Waiters waiting for all connections to be closed.
    waiters: std::vec::Vec<oneshot::Sender<()>>,
}

impl ExecutionScope {
    /// Constructs an execution scope that has no `token_registry`, `inode_registry`, nor
    /// `entry_constructor`.  Use [`build()`] if you want to specify other parameters.
    pub fn new() -> Self {
        Self::build().new()
    }

    // TODO: Remove spawn once deps in other repos are resolved.
    pub fn from_executor(_upstream: Box<dyn Spawn + Send>) -> Self {
        Self::new()
    }

    /// Constructs a new execution scope builder, wrapping the specified executor and optionally
    /// accepting additional parameters.  Run [`ExecutionScopeParams::new()`] to get an actual
    /// [`ExecutionScope`] object.
    pub fn build() -> ExecutionScopeParams {
        ExecutionScopeParams { token_registry: None, inode_registry: None, entry_constructor: None }
    }

    /// Sends a `task` to be executed in this execution scope.  This is very similar to
    /// [`Spawn::spawn_obj`] with a minor difference that `self` reference is not exclusive.
    ///
    /// Note that when the scope is shut down, this task will be interrupted the next time it
    /// returns `Pending`.  If you need to perform any shutdown operations, use
    /// [`spawn_with_shutdown`] instead.
    ///
    /// For the "vfs" library it is more convenient that this method allows non-exclusive
    /// access.  And as the implementation is employing internal mutability there are no downsides.
    /// This way `ExecutionScope` can actually also implement [`Spawn`] - it just was not necessary
    /// for now.
    pub fn spawn<Task>(&self, task: Task)
    where
        Task: Future<Output = ()> + Send + 'static,
    {
        Executor::run_abort_any_time(self.executor.clone(), task)
    }

    /// Sends a `task` to be executed in this execution scope.  This is very similar to
    /// [`Spawn::spawn_obj`] with a minor difference that `self` reference is not exclusive.
    ///
    /// Task to be executed will be constructed using the specified callback.  It is provided with
    /// a one-shot channel that will be signaled during the shutdown process.  The task must be
    /// monitoring the channel and should perform any necessary shutdown steps and terminate when
    /// a message is received over the channel.  If you do not need a custom shutdown process you
    /// can use [`spawn`] method instead.
    ///
    /// For the "vfs" library it is more convenient that this method allows non-exclusive
    /// access.  And as the implementation is employing internal mutability there are no downsides.
    /// This way `ExecutionScope` can actually also implement [`Spawn`] - it just was not necessary
    /// for now.
    pub fn spawn_with_shutdown<Constructor, Task>(&self, constructor: Constructor)
    where
        Constructor: FnOnce(oneshot::Receiver<()>) -> Task + 'static,
        Task: Future<Output = ()> + Send + 'static,
    {
        let (sender, receiver) = oneshot::channel();
        Executor::run_abort_with_shutdown(self.executor.clone(), constructor(receiver), sender)
    }

    pub fn token_registry(&self) -> Option<Arc<dyn TokenRegistry + Send + Sync>> {
        self.token_registry.as_ref().map(Arc::clone)
    }

    pub fn inode_registry(&self) -> Option<Arc<dyn InodeRegistry + Send + Sync>> {
        self.inode_registry.as_ref().map(Arc::clone)
    }

    pub fn entry_constructor(&self) -> Option<Arc<dyn EntryConstructor + Send + Sync>> {
        self.entry_constructor.as_ref().map(Arc::clone)
    }

    pub fn shutdown(&self) {
        let mut this = self.executor.lock();
        this.shutdown();
    }

    /// Wait for all tasks to complete.
    pub async fn wait(&self) {
        let receiver = {
            let mut this = self.executor.lock();
            if this.running.is_empty() {
                None
            } else {
                let (sender, receiver) = oneshot::channel::<()>();
                this.waiters.push(sender);
                Some(receiver)
            }
        };
        if let Some(receiver) = receiver {
            receiver.await.unwrap();
        }
    }
}

impl Clone for ExecutionScope {
    fn clone(&self) -> Self {
        ExecutionScope {
            executor: self.executor.clone(),
            token_registry: self.token_registry.as_ref().map(Arc::clone),
            inode_registry: self.inode_registry.as_ref().map(Arc::clone),
            entry_constructor: self.entry_constructor.as_ref().map(Arc::clone),
        }
    }
}

pub struct ExecutionScopeParams {
    token_registry: Option<Arc<dyn TokenRegistry + Send + Sync>>,
    inode_registry: Option<Arc<dyn InodeRegistry + Send + Sync>>,
    entry_constructor: Option<Arc<dyn EntryConstructor + Send + Sync>>,
}

impl ExecutionScopeParams {
    pub fn token_registry(mut self, value: Arc<dyn TokenRegistry + Send + Sync>) -> Self {
        assert!(self.token_registry.is_none(), "`token_registry` is already set");
        self.token_registry = Some(value);
        self
    }

    pub fn inode_registry(mut self, value: Arc<dyn InodeRegistry + Send + Sync>) -> Self {
        assert!(self.inode_registry.is_none(), "`inode_registry` is already set");
        self.inode_registry = Some(value);
        self
    }

    pub fn entry_constructor(mut self, value: Arc<dyn EntryConstructor + Send + Sync>) -> Self {
        assert!(self.entry_constructor.is_none(), "`entry_constructor` is already set");
        self.entry_constructor = Some(value);
        self
    }

    pub fn new(self) -> ExecutionScope {
        ExecutionScope {
            executor: Arc::new(Mutex::new(Executor { running: Slab::new(), waiters: Vec::new() })),
            token_registry: self.token_registry,
            inode_registry: self.inode_registry,
            entry_constructor: self.entry_constructor,
        }
    }
}

// A future that completes when either of two futures completes.
#[pin_project]
struct FirstToFinish<A, B> {
    #[pin]
    first: A,
    #[pin]
    second: B,
}

impl<A: Future, B: Future> Future for FirstToFinish<A, B> {
    type Output = ();

    fn poll(self: Pin<&mut Self>, cx: &mut Context) -> Poll<Self::Output> {
        let this = self.project();
        if let Poll::Ready(_) = this.first.poll(cx) {
            Poll::Ready(())
        } else if let Poll::Ready(_) = this.second.poll(cx) {
            Poll::Ready(())
        } else {
            Poll::Pending
        }
    }
}

trait OrFuture
where
    Self: Sized,
{
    fn or<B: Future>(self, second: B) -> FirstToFinish<Self, B> {
        FirstToFinish { first: self, second }
    }
}

impl<A> OrFuture for A {}

impl Executor {
    fn run_abort_any_time<F: 'static + Future<Output = ()> + Send>(
        executor: Arc<Mutex<Executor>>,
        task: F,
    ) {
        let (sender, receiver) = oneshot::channel();
        Self::run_abort_with_shutdown(executor, task.or(receiver), sender)
    }

    fn run_abort_with_shutdown<F: 'static + Future + Send>(
        executor: Arc<Mutex<Executor>>,
        task: F,
        shutdown: oneshot::Sender<()>,
    ) {
        let mut this = executor.lock();

        let task_id = this.running.insert(Some(shutdown));
        let executor_clone = executor.clone();
        let task =
            task.then(move |_| async move { executor_clone.lock().task_did_finish(task_id) });
        fuchsia_async::Task::spawn(task).detach();
    }

    fn shutdown(&mut self) {
        for (_key, task) in self.running.iter_mut() {
            // As the task removal is processed by the task itself, we may see cases when we have
            // already sent the stop message, but the task did not remove its entry from the list
            // just yet.  There is a race condition with the task shutdown process.  Shutdown
            // happens in one thread, while task execution - in another.  So, we need to tolerate
            // "double" removal either here, or in the task shutdown code.  Making the task shutodwn
            // code responsible from removing itself from the `running` list seems a bit cleaner.
            if let Some(sender) = task.take() {
                // If the task is in the process of finishing by itself, there's a small window
                // where the receiver could have been dropped before the task has been removed from
                // the running list, so ignore errors here.
                let _ = sender.send(());
            }
        }
    }

    fn task_did_finish(&mut self, task_id: usize) {
        self.running.remove(task_id);
        if self.running.is_empty() {
            for waiter in self.waiters.drain(..) {
                let _ = waiter.send(());
            }
        }
    }
}

impl Drop for Executor {
    fn drop(&mut self) {
        self.shutdown();
    }
}

#[cfg(test)]
mod tests {
    use super::ExecutionScope;

    use crate::{
        directory::mutable::entry_constructor::EntryConstructor,
        registry::{inode_registry, token_registry, InodeRegistry, TokenRegistry},
    };

    use {
        fuchsia_async::{Executor, Time, Timer},
        fuchsia_zircon::prelude::*,
        futures::{
            channel::{mpsc, oneshot},
            select,
            task::Poll,
            Future, FutureExt, StreamExt,
        },
        pin_utils::pin_mut,
        std::sync::{
            atomic::{AtomicUsize, Ordering},
            Arc,
        },
    };

    fn run_test<GetTest, GetTestRes>(get_test: GetTest)
    where
        GetTest: FnOnce(ExecutionScope) -> GetTestRes,
        GetTestRes: Future<Output = ()>,
    {
        let mut exec = Executor::new().expect("Executor creation failed");

        let scope = ExecutionScope::new();

        let test = get_test(scope);

        pin_mut!(test);
        assert_eq!(exec.run_until_stalled(&mut test), Poll::Ready(()), "Test did not complete");
    }

    #[test]
    fn simple() {
        run_test(|scope| {
            async move {
                let (sender, receiver) = oneshot::channel();
                let (counters, task) = mocks::ImmediateTask::new(sender);

                scope.spawn(task);

                // Make sure our task had a chance to execute.
                receiver.await.unwrap();

                assert_eq!(counters.drop_call(), 1);
                assert_eq!(counters.poll_call(), 1);
            }
        });
    }

    #[test]
    fn simple_drop() {
        run_test(|scope| {
            async move {
                let (poll_sender, poll_receiver) = oneshot::channel();
                let (processing_done_sender, processing_done_receiver) = oneshot::channel();
                let (drop_sender, drop_receiver) = oneshot::channel();
                let (counters, task) =
                    mocks::ControlledTask::new(poll_sender, processing_done_receiver, drop_sender);

                scope.spawn(task);

                poll_receiver.await.unwrap();

                processing_done_sender.send(()).unwrap();

                scope.shutdown();

                drop_receiver.await.unwrap();

                // poll might be called one or two times depending on the order in which the
                // executor decides to poll the two tasks (this one and the one we spawned).
                let poll_count = counters.poll_call();
                assert!(poll_count >= 1, "poll was not called");

                assert_eq!(counters.drop_call(), 1);
            }
        });
    }

    #[test]
    fn test_wait_waits_for_tasks_to_finish() {
        let mut executor = Executor::new().expect("Executor creation failed");
        let scope = ExecutionScope::new();
        executor.run_singlethreaded(async {
            let (poll_sender, poll_receiver) = oneshot::channel();
            let (processing_done_sender, processing_done_receiver) = oneshot::channel();
            let (drop_sender, _drop_receiver) = oneshot::channel();
            let (_, task) =
                mocks::ControlledTask::new(poll_sender, processing_done_receiver, drop_sender);

            scope.spawn(task);

            poll_receiver.await.unwrap();

            // We test that wait is working correctly by concurrently waiting and telling the
            // task to complete, and making sure that the order is correct.
            let done = std::sync::Mutex::new(false);
            futures::join!(
                async {
                    scope.wait().await;
                    assert_eq!(*done.lock().unwrap(), true);
                },
                async {
                    // This is a Turing halting problem so the sleep is justified.
                    Timer::new(Time::after(100.millis())).await;
                    *done.lock().unwrap() = true;
                    processing_done_sender.send(()).unwrap();
                }
            );
        });
    }

    #[test]
    fn spawn_with_shutdown() {
        run_test(|scope| async move {
            let (processing_done_sender, processing_done_receiver) = oneshot::channel();
            let (shutdown_complete_sender, shutdown_complete_receiver) = oneshot::channel();

            scope.spawn_with_shutdown(|_shutdown| async move {
                processing_done_receiver.await.unwrap();
                shutdown_complete_sender.send(()).unwrap();
            });

            processing_done_sender.send(()).unwrap();

            shutdown_complete_receiver.await.unwrap();
        });
    }

    #[test]
    fn explicit_shutdown() {
        run_test(|scope| async move {
            let (tick_sender, tick_receiver) = mpsc::unbounded();
            let (tick_confirmation_sender, mut tick_confirmation_receiver) = mpsc::unbounded();
            let (shutdown_complete_sender, shutdown_complete_receiver) = oneshot::channel();

            let tick_count = Arc::new(AtomicUsize::new(0));

            scope.spawn_with_shutdown({
                let tick_count = tick_count.clone();

                |shutdown| async move {
                    let mut tick_receiver = tick_receiver.fuse();
                    let mut shutdown = shutdown.fuse();
                    loop {
                        select! {
                            tick = tick_receiver.next() => {
                                tick.unwrap();
                                tick_count.fetch_add(1, Ordering::Relaxed);
                                tick_confirmation_sender.unbounded_send(()).unwrap();
                            },
                            _ = shutdown => break,
                        }
                    }
                    shutdown_complete_sender.send(()).unwrap();
                }
            });

            assert_eq!(tick_count.load(Ordering::Relaxed), 0);

            tick_sender.unbounded_send(()).unwrap();
            tick_confirmation_receiver.next().await.unwrap();
            assert_eq!(tick_count.load(Ordering::Relaxed), 1);

            tick_sender.unbounded_send(()).unwrap();
            tick_confirmation_receiver.next().await.unwrap();
            assert_eq!(tick_count.load(Ordering::Relaxed), 2);

            scope.shutdown();

            shutdown_complete_receiver.await.unwrap();
            assert_eq!(tick_count.load(Ordering::Relaxed), 2);
        });
    }

    #[test]
    fn with_token_registry() {
        let registry = token_registry::Simple::new();

        let scope = ExecutionScope::build().token_registry(registry.clone()).new();

        let registry2 = scope.token_registry().unwrap();
        assert!(
            Arc::ptr_eq(&(registry as Arc<dyn TokenRegistry + Send + Sync>), &registry2),
            "`scope` returned `Arc` to a token registry is different from the one initially set."
        );
    }

    #[test]
    fn with_inode_registry() {
        let registry = inode_registry::Simple::new();

        let scope = ExecutionScope::build().inode_registry(registry.clone()).new();

        let registry2 = scope.inode_registry().unwrap();
        assert!(
            Arc::ptr_eq(&(registry as Arc<dyn InodeRegistry + Send + Sync>), &registry2),
            "`scope` returned `Arc` to an inode registry is different from the one initially set."
        );
    }

    #[test]
    fn with_mock_entry_constructor() {
        let entry_constructor = mocks::MockEntryConstructor::new();

        let scope = ExecutionScope::build().entry_constructor(entry_constructor.clone()).new();

        let entry_constructor2 = scope.entry_constructor().unwrap();
        assert!(
            Arc::ptr_eq(
                &(entry_constructor as Arc<dyn EntryConstructor + Send + Sync>),
                &entry_constructor2
            ),
            "`scope` returned `Arc` to an entry constructor is different from the one initially \
             set."
        );
    }

    mod mocks {
        use crate::{
            directory::{
                entry::DirectoryEntry,
                mutable::entry_constructor::{EntryConstructor, NewEntryType},
            },
            path::Path,
        };

        use {
            fuchsia_zircon::Status,
            futures::{
                channel::oneshot,
                task::{Context, Poll},
                Future, FutureExt,
            },
            std::{
                ops::Drop,
                pin::Pin,
                sync::{
                    atomic::{AtomicUsize, Ordering},
                    Arc,
                },
            },
        };

        pub(super) struct TaskCounters {
            poll_call_count: Arc<AtomicUsize>,
            drop_call_count: Arc<AtomicUsize>,
        }

        impl TaskCounters {
            fn new() -> (Arc<AtomicUsize>, Arc<AtomicUsize>, Self) {
                let poll_call_count = Arc::new(AtomicUsize::new(0));
                let drop_call_count = Arc::new(AtomicUsize::new(0));

                (
                    poll_call_count.clone(),
                    drop_call_count.clone(),
                    Self { poll_call_count, drop_call_count },
                )
            }

            pub(super) fn poll_call(&self) -> usize {
                self.poll_call_count.load(Ordering::Relaxed)
            }

            pub(super) fn drop_call(&self) -> usize {
                self.drop_call_count.load(Ordering::Relaxed)
            }
        }

        pub(super) struct ImmediateTask {
            poll_call_count: Arc<AtomicUsize>,
            drop_call_count: Arc<AtomicUsize>,
            done_sender: Option<oneshot::Sender<()>>,
        }

        impl ImmediateTask {
            pub(super) fn new(done_sender: oneshot::Sender<()>) -> (TaskCounters, Self) {
                let (poll_call_count, drop_call_count, counters) = TaskCounters::new();
                (
                    counters,
                    Self { poll_call_count, drop_call_count, done_sender: Some(done_sender) },
                )
            }
        }

        impl Future for ImmediateTask {
            type Output = ();

            fn poll(mut self: Pin<&mut Self>, _cx: &mut Context) -> Poll<Self::Output> {
                self.poll_call_count.fetch_add(1, Ordering::Relaxed);

                if let Some(sender) = self.done_sender.take() {
                    sender.send(()).unwrap();
                }

                Poll::Ready(())
            }
        }

        impl Drop for ImmediateTask {
            fn drop(&mut self) {
                self.drop_call_count.fetch_add(1, Ordering::Relaxed);
            }
        }

        impl Unpin for ImmediateTask {}

        pub(super) struct ControlledTask {
            poll_call_count: Arc<AtomicUsize>,
            drop_call_count: Arc<AtomicUsize>,

            drop_sender: Option<oneshot::Sender<()>>,
            future: Pin<Box<dyn Future<Output = ()> + Send>>,
        }

        impl ControlledTask {
            pub(super) fn new(
                poll_sender: oneshot::Sender<()>,
                processing_complete: oneshot::Receiver<()>,
                drop_sender: oneshot::Sender<()>,
            ) -> (TaskCounters, Self) {
                let (poll_call_count, drop_call_count, counters) = TaskCounters::new();
                (
                    counters,
                    Self {
                        poll_call_count,
                        drop_call_count,
                        drop_sender: Some(drop_sender),
                        future: Box::pin(async move {
                            poll_sender.send(()).unwrap();
                            processing_complete.await.unwrap();
                        }),
                    },
                )
            }
        }

        impl Future for ControlledTask {
            type Output = ();

            fn poll(mut self: Pin<&mut Self>, cx: &mut Context) -> Poll<Self::Output> {
                self.poll_call_count.fetch_add(1, Ordering::Relaxed);
                self.future.as_mut().poll(cx)
            }
        }

        impl Drop for ControlledTask {
            fn drop(&mut self) {
                self.drop_call_count.fetch_add(1, Ordering::Relaxed);
                self.drop_sender.take().unwrap().send(()).unwrap();
            }
        }

        pub(super) struct MockEntryConstructor {}

        impl MockEntryConstructor {
            pub(super) fn new() -> Arc<Self> {
                Arc::new(Self {})
            }
        }

        impl EntryConstructor for MockEntryConstructor {
            fn create_entry(
                self: Arc<Self>,
                _parent: Arc<dyn DirectoryEntry>,
                _what: NewEntryType,
                _name: &str,
                _path: &Path,
            ) -> Result<Arc<dyn DirectoryEntry>, Status> {
                panic!("Not implemented")
            }
        }
    }
}
