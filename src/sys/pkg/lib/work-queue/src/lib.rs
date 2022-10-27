// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(missing_docs)]
#![warn(clippy::all)]
#![allow(clippy::type_complexity)]

//! Concurrent work queue helpers

use {
    futures::{
        channel::mpsc,
        future::Shared,
        prelude::*,
        ready,
        stream::{FusedStream, FuturesUnordered},
    },
    parking_lot::Mutex,
    pin_project::pin_project,
    std::{
        collections::HashMap,
        hash::Hash,
        pin::Pin,
        sync::{Arc, Weak},
        task::{Context, Poll},
    },
    thiserror::Error,
};

mod state;
use state::{make_canceled_receiver, TaskFuture, TaskVariants};

/// Error type indicating a task failed because the queue was dropped before completing the task.
#[derive(Debug, PartialEq, Eq, Clone, Error)]
#[error("The queue was dropped before processing this task")]
pub struct Closed;

/// Trait for merging context for work tasks with the same key.
///
/// Implementations must satisfy (for all `a` and `b`):
/// * `a == b` implies `a.try_merge(b) == Ok(()) && a` was not modified; and
/// * `a.try_merge(b) == Err(b)` implies `a` was not modified and `b` was returned unmodified.
pub trait TryMerge: Eq + Sized {
    /// Attempts to try_merge `other` into `self`, returning `other` if such an operation is not
    /// possible.
    ///
    /// Implementations should return `Ok(())` if other was fully merged into `self`, or return
    /// `Err(other)`, leaving `self` unchanged if the instances could not be merged.
    fn try_merge(&mut self, other: Self) -> Result<(), Self>;
}

impl TryMerge for () {
    fn try_merge(&mut self, _: ()) -> Result<(), Self> {
        Ok(())
    }
}

/// Creates an unbounded queue of work tasks that will execute up to `concurrency` `worker`s at once.
///
/// # Examples
///
/// ```
/// # use queue::*;
/// # use futures::prelude::*;
///
/// #[derive(Debug, Clone)]
/// enum DownloadError {}
///
/// async fn download_file(url: String, _context: ()) -> Result<Vec<u8>, DownloadError> {
///     // ...
/// #     Ok(url.bytes().collect())
/// }
///
/// let mut executor = futures::executor::LocalPool::new();
/// executor.run_until(async move {
///     let (mut processor, sender) = work_queue(2, download_file);
///     let mut join_handles = vec![];
///     for crate_name in vec!["rand", "lazy_static", "serde", "regex"] {
///         let fut = sender.push(format!("https://crates.io/api/v1/crates/{}", crate_name), ());
///         join_handles.push((crate_name, fut));
///     }
///
///     // The queue stream won't terminate until all sender clones are dropped.
///     drop(sender);
///
///     while let Some(key) = processor.next().await {
///         println!("Finished processing {}", key);
///     }
///
///     for (crate_name, fut) in join_handles {
///         let res = fut
///             .await
///             .expect("queue to execute the task")
///             .expect("downloads can't fail, right?");
///         println!("Contents of {}: {:?}", crate_name, res);
///     }
/// });
/// ```
pub fn work_queue<W, K, C>(
    concurrency: usize,
    work_fn: W,
) -> (WorkQueue<W, K, C>, WorkSender<K, C, <W::Future as Future>::Output>)
where
    W: Work<K, C>,
    K: Clone + Eq + Hash,
    C: TryMerge,
{
    let tasks = Arc::new(Mutex::new(HashMap::new()));
    let (sender, receiver) = mpsc::unbounded();
    let sender = WorkSender { sender, tasks: Arc::downgrade(&tasks) };
    (
        WorkQueue {
            work_fn,
            concurrency,
            pending: receiver,
            tasks,
            running: FuturesUnordered::new(),
        },
        sender,
    )
}

/// Trait that creates a work future from a key and context.
pub trait Work<K, C> {
    /// The future that is executed by the WorkQueue.
    type Future: Future;

    /// Create a new `Future` to be executed by the WorkQueue.
    fn start(&self, key: K, context: C) -> Self::Future;
}

impl<F, K, C, WF> Work<K, C> for F
where
    F: Fn(K, C) -> WF,
    WF: Future,
{
    type Future = WF;

    fn start(&self, key: K, context: C) -> Self::Future {
        (self)(key, context)
    }
}

/// A work queue that processes a configurable number of tasks concurrently, deduplicating work
/// with the same key.
///
/// Items are yielded from the stream in the order that they are processed, which may differ from
/// the order that items are enqueued, depending on which concurrent tasks complete first.
#[pin_project]
pub struct WorkQueue<W, K, C>
where
    W: Work<K, C>,
{
    /// The work callback function.
    work_fn: W,
    /// Maximum number of tasks to run concurrently.
    concurrency: usize,
    /// Metadata about pending and running work. Modified by the queue itself when running tasks
    /// and by [WorkSender] to add new tasks to the queue.
    tasks: Arc<Mutex<HashMap<K, TaskVariants<C, <W::Future as Future>::Output>>>>,

    /// Receiving end of the queue.
    #[pin]
    pending: mpsc::UnboundedReceiver<K>,
    /// Tasks currently being run. Will contain [0, concurrency] futures at any given time.
    #[pin]
    running: FuturesUnordered<RunningTask<K, W::Future>>,
}

impl<W, K, C> WorkQueue<W, K, C>
where
    W: Work<K, C>,
    K: Clone + Eq + Hash,
{
    /// Converts this stream of K into a single future that resolves when the stream is
    /// terminated.
    pub fn into_future(self) -> impl Future<Output = ()> {
        self.map(|_res| ()).collect::<()>()
    }

    /// Starts new work if under the concurrency limit and work is enqueued.
    /// Returns:
    /// * Poll::Ready(None) if the input work queue is empty and closed.
    /// * Poll::Ready(Some(())) if new work was started.
    /// * Poll::Pending if at the concurrency limit or no work is enqueued.
    fn find_work(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<()>> {
        let mut this = self.project();

        // Nothing to do if the stream of requests is EOF.
        if this.pending.is_terminated() {
            return Poll::Ready(None);
        }

        let mut found = false;
        while this.running.len() < *this.concurrency {
            match ready!(this.pending.as_mut().poll_next(cx)) {
                None => break,
                Some(key) => {
                    found = true;

                    // Transition the work info to the running state, claiming the context.
                    let context = this
                        .tasks
                        .lock()
                        .get_mut(&key)
                        .expect("map entry to exist if in pending queue")
                        .start();

                    // WorkSender::push_entry guarantees that key will only be pushed into pending
                    // if it created the entry in the map, so it is guaranteed here that multiple
                    // instances of the same key will not be executed concurrently.
                    let work = this.work_fn.start(key.clone(), context);
                    let fut = futures::future::join(futures::future::ready(key), work);

                    this.running.push(fut);
                }
            }
        }
        if found {
            Poll::Ready(Some(()))
        } else {
            Poll::Pending
        }
    }

    fn do_work(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<K>> {
        let mut this = self.project();

        match this.running.as_mut().poll_next(cx) {
            Poll::Pending => Poll::Pending,
            Poll::Ready(None) => {
                // this.running is now terminated, but unlike the guarantees given by other
                // FusedStream implementations, FuturesUnordered can continue to be polled (unless
                // new work comes in, it will continue to return Poll::Ready(None)), and new
                // futures can be pushed into it. Pushing new work on a terminated
                // FuturesUnordered will cause is_terminated to return false, and polling the
                // stream will start the task.
                if this.pending.is_terminated() {
                    Poll::Ready(None)
                } else {
                    Poll::Pending
                }
            }
            Poll::Ready(Some((key, res))) => {
                let mut tasks = this.tasks.lock();
                let infos: &mut TaskVariants<_, _> =
                    &mut tasks.get_mut(&key).expect("key to exist in map if not resolved");

                if let Some(next_context) = infos.done(res) {
                    // start the next operation immediately
                    let work = this.work_fn.start(key.clone(), next_context);
                    let key_clone = key.clone();
                    let fut = futures::future::join(futures::future::ready(key_clone), work);

                    drop(tasks);
                    this.running.push(fut);
                } else {
                    // last pending operation with this key
                    tasks.remove(&key);
                }

                // Yield the key that was processed to the stream, indicating if processing that
                // value was successful or not.
                Poll::Ready(Some(key))
            }
        }
    }
}

impl<W, K, C> Stream for WorkQueue<W, K, C>
where
    W: Work<K, C>,
    K: Clone + Eq + Hash,
{
    type Item = K;
    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        match (self.as_mut().find_work(cx), self.as_mut().do_work(cx)) {
            (Poll::Ready(None), Poll::Ready(None)) => {
                // There input queues are empty and closed, and all running work has been
                // completed. This work queue is now (or was already) terminated.
                Poll::Ready(None)
            }
            (Poll::Ready(Some(())), Poll::Ready(Some(res))) => {
                // The input queue made progress this iteration and a work item completed.
                // find_work again to either start more work or register for a wakeup when work
                // becomes available.
                let _ = self.as_mut().find_work(cx);
                Poll::Ready(Some(res))
            }
            (_not_ready_none, Poll::Ready(None)) => {
                // Our active task queue is empty, but more work can still come in. Report this
                // poll as pending.
                Poll::Pending
            }
            (_, poll) => poll,
        }
    }
}

type RunningTask<K, WF> = futures::future::Join<futures::future::Ready<K>, WF>;

/// A clonable handle to the work queue.  When all clones of [WorkSender] are dropped, the queue
/// will process all remaining requests and terminate its output stream.
pub struct WorkSender<K, C, O> {
    sender: mpsc::UnboundedSender<K>,
    // Weak reference to ensure that if the queue is dropped, the now unused sender end of the
    // completion callback will be dropped too, canceling the request.
    tasks: Weak<Mutex<HashMap<K, TaskVariants<C, O>>>>,
}

impl<K, C, O> Clone for WorkSender<K, C, O> {
    fn clone(&self) -> Self {
        Self { sender: self.sender.clone(), tasks: self.tasks.clone() }
    }
}

impl<K, C, O> std::fmt::Debug for WorkSender<K, C, O> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("WorkSender").finish()
    }
}

impl<K, C, O> WorkSender<K, C, O>
where
    K: Clone + Eq + Hash,
    C: TryMerge,
    O: Clone,
{
    /// Enqueue the given key to be processed by a worker, or attach to an existing request to
    /// process this key.
    pub fn push(&self, key: K, context: C) -> impl Future<Output = Result<O, Closed>> {
        let tasks = match self.tasks.upgrade() {
            Some(tasks) => tasks,
            None => {
                // Work queue no longer exists. Immediately cancel this request.
                return make_canceled_receiver();
            }
        };
        let mut tasks = tasks.lock();

        Self::push_entry(&mut *tasks, &self.sender, key, context)
    }

    /// Enqueue all the given keys to be processed by a worker, merging them with existing known
    /// tasks if possible, returning an iterator of the futures that will resolve to the results of
    /// processing the keys.
    ///
    /// This method is similar to, but more efficient than, mapping an iterator to
    /// `WorkSender::push`.
    pub fn push_all(
        &self,
        entries: impl Iterator<Item = (K, C)>,
    ) -> impl Iterator<Item = impl Future<Output = Result<O, Closed>>> {
        let mut tasks = self.tasks.upgrade();
        let mut tasks = tasks.as_mut().map(|tasks| tasks.lock());

        entries
            .map(move |(key, context)| {
                if let Some(ref mut tasks) = tasks {
                    Self::push_entry(&mut *tasks, &self.sender, key, context)
                } else {
                    // Work queue no longer exists. Immediately cancel this request.
                    make_canceled_receiver()
                }
            })
            .collect::<Vec<_>>()
            .into_iter()
    }

    fn push_entry(
        tasks: &mut HashMap<K, TaskVariants<C, O>>,
        self_sender: &mpsc::UnboundedSender<K>,
        key: K,
        context: C,
    ) -> Shared<TaskFuture<O>> {
        use std::collections::hash_map::Entry;

        match tasks.entry(key.clone()) {
            Entry::Vacant(entry) => {
                // No other variant of this task is running or pending. Reserve our
                // spot in line and configure the task's metadata.
                if let Ok(()) = self_sender.unbounded_send(key) {
                    let (infos, fut) = TaskVariants::new(context);
                    entry.insert(infos);
                    fut
                } else {
                    // Work queue no longer exists. Immediately cancel this request.
                    make_canceled_receiver()
                }
            }
            Entry::Occupied(entry) => entry.into_mut().push(context),
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        futures::{
            channel::oneshot,
            executor::{block_on, LocalSpawner},
            task::{LocalSpawnExt, SpawnExt},
        },
        std::{borrow::Borrow, fmt},
    };

    #[test]
    fn basic_usage() {
        async fn do_work(_key: String, _context: ()) -> Result<(), ()> {
            Ok(())
        }

        let (processor, enqueue) = work_queue(3, do_work);

        let tasks = FuturesUnordered::new();

        tasks.push(enqueue.push("a".into(), ()));
        tasks.push(enqueue.push("a".into(), ()));
        tasks.push(enqueue.push("b".into(), ()));
        tasks.push(enqueue.push("a".into(), ()));
        tasks.push(enqueue.push("c".into(), ()));

        drop(enqueue);

        block_on(async move {
            let (keys, res) = futures::future::join(
                processor.collect::<Vec<String>>(),
                tasks.collect::<Vec<Result<Result<(), ()>, _>>>(),
            )
            .await;
            assert_eq!(keys, vec!["a".to_string(), "b".into(), "c".into()]);
            assert_eq!(res, vec![Ok(Ok(())), Ok(Ok(())), Ok(Ok(())), Ok(Ok(())), Ok(Ok(()))]);
        });
    }

    #[test]
    fn into_future() {
        async fn nop(key: i32, _context: ()) -> i32 {
            key
        }
        let (processor, enqueue) = work_queue(1, nop);

        let res_fut =
            future::join3(processor.into_future(), enqueue.push(1, ()), enqueue.push(2, ()));
        drop(enqueue);

        let res = block_on(res_fut);
        assert_eq!(res, ((), Ok(1), Ok(2)));
    }

    #[derive(Debug, PartialEq, Eq)]
    pub(crate) struct MergeEqual(pub(crate) i32);

    impl TryMerge for MergeEqual {
        fn try_merge(&mut self, other: Self) -> Result<(), Self> {
            if self.0 == other.0 {
                Ok(())
            } else {
                Err(other)
            }
        }
    }

    #[test]
    fn dropping_queue_fails_requests() {
        async fn do_work(_key: &str, _context: MergeEqual) -> Result<(), ()> {
            Ok(())
        }

        let (processor, enqueue) = work_queue(1, do_work);

        let fut_early_a = enqueue.push("a", MergeEqual(0));
        let fut_early_b = enqueue.push("a", MergeEqual(1));
        let fut_early_c = enqueue.push("a", MergeEqual(0));
        drop(processor);
        let fut_late = enqueue.push("b", MergeEqual(0));

        block_on(async move {
            assert_eq!(fut_early_a.await, Err(Closed));
            assert_eq!(fut_early_b.await, Err(Closed));
            assert_eq!(fut_early_c.await, Err(Closed));
            assert_eq!(fut_late.await, Err(Closed));

            let requests = vec![("1", MergeEqual(0)), ("2", MergeEqual(1)), ("1", MergeEqual(0))];
            for fut in enqueue.push_all(requests.into_iter()) {
                assert_eq!(fut.await, Err(Closed));
            }
        });
    }

    #[derive(Debug)]
    struct TestRunningTask<C, O> {
        unblocker: oneshot::Sender<O>,
        context: C,
    }

    #[derive(Debug)]
    struct TestRunningTasks<K, C, O>
    where
        K: Eq + Hash,
    {
        tasks: Arc<Mutex<HashMap<K, TestRunningTask<C, O>>>>,
    }

    impl<K, C, O> TestRunningTasks<K, C, O>
    where
        K: fmt::Debug + Eq + Hash + Sized + Clone,
        C: fmt::Debug,
        O: fmt::Debug,
    {
        fn new() -> Self {
            Self { tasks: Arc::new(Mutex::new(HashMap::new())) }
        }

        fn resolve<Q>(&self, key: &Q, res: O) -> C
        where
            Q: Eq + Hash + ?Sized,
            K: Borrow<Q>,
        {
            let task =
                self.tasks.lock().remove(key.borrow()).expect("key to exist in running work");
            task.unblocker.send(res).unwrap();
            task.context
        }

        fn peek(&self) -> Option<K> {
            self.tasks.lock().keys().next().map(|key| key.clone())
        }

        fn keys(&self) -> Vec<K> {
            self.tasks.lock().keys().cloned().collect()
        }

        fn assert_empty(&self) {
            assert_eq!(
                self.tasks.lock().keys().collect::<Vec<&K>>(),
                Vec::<&K>::new(),
                "expect queue to be empty"
            );
        }
    }

    #[derive(Debug)]
    struct TestQueueResults<K> {
        done: Arc<Mutex<Vec<K>>>,
        terminated: Arc<Mutex<bool>>,
    }

    impl<K> TestQueueResults<K> {
        fn take(&self) -> Vec<K> {
            std::mem::replace(&mut *self.done.lock(), vec![])
        }

        fn is_terminated(&self) -> bool {
            *self.terminated.lock()
        }
    }

    #[test]
    fn check_works_with_sendable_types() {
        struct TestWork;

        impl Work<(), ()> for TestWork {
            type Future = futures::future::Ready<()>;

            fn start(&self, _key: (), _context: ()) -> Self::Future {
                futures::future::ready(())
            }
        }

        let (processor, enqueue) = work_queue(3, TestWork);

        let tasks = FuturesUnordered::new();
        tasks.push(enqueue.push((), ()));

        drop(enqueue);

        let mut executor = futures::executor::LocalPool::new();
        let handle = executor
            .spawner()
            .spawn_with_handle(async move {
                let (keys, res) =
                    futures::future::join(processor.collect::<Vec<_>>(), tasks.collect::<Vec<_>>())
                        .await;
                assert_eq!(keys, vec![()]);
                assert_eq!(res, vec![Ok(())]);
            })
            .expect("spawn to work");
        let () = executor.run_until(handle);
    }

    #[test]
    fn check_works_with_unsendable_types() {
        use std::rc::Rc;

        // Unfortunately `impl !Send for $Type` is unstable, so use Rc<()> to make sure WorkQueue
        // still works.
        struct TestWork(Rc<()>);
        #[derive(Clone, Debug, PartialEq, Eq, Hash)]
        struct TestKey(Rc<()>);
        #[derive(PartialEq, Eq)]
        struct TestContext(Rc<()>);
        #[derive(Clone, Debug, PartialEq)]
        struct TestOutput(Rc<()>);

        impl Work<TestKey, TestContext> for TestWork {
            type Future = futures::future::Ready<TestOutput>;

            fn start(&self, _key: TestKey, _context: TestContext) -> Self::Future {
                futures::future::ready(TestOutput(Rc::new(())))
            }
        }

        impl TryMerge for TestContext {
            fn try_merge(&mut self, _: Self) -> Result<(), Self> {
                Ok(())
            }
        }

        let (processor, enqueue) = work_queue(3, TestWork(Rc::new(())));

        let tasks = FuturesUnordered::new();
        tasks.push(enqueue.push(TestKey(Rc::new(())), TestContext(Rc::new(()))));

        drop(enqueue);

        let mut executor = futures::executor::LocalPool::new();
        let handle = executor
            .spawner()
            .spawn_local_with_handle(async move {
                let (keys, res) =
                    futures::future::join(processor.collect::<Vec<_>>(), tasks.collect::<Vec<_>>())
                        .await;
                assert_eq!(keys, vec![TestKey(Rc::new(()))]);
                assert_eq!(res, vec![Ok(TestOutput(Rc::new(())))]);
            })
            .expect("spawn to work");
        let () = executor.run_until(handle);
    }

    fn spawn_test_work_queue<K, C, O>(
        spawner: LocalSpawner,
        concurrency: usize,
    ) -> (WorkSender<K, C, O>, TestRunningTasks<K, C, O>, TestQueueResults<K>)
    where
        K: Send + Clone + fmt::Debug + Eq + Hash + 'static,
        C: TryMerge + Send + fmt::Debug + 'static,
        O: Send + Clone + fmt::Debug + 'static,
    {
        let running = TestRunningTasks::<K, C, O>::new();
        let running_tasks = running.tasks.clone();
        let do_work = move |key: K, context: C| {
            // wait for the test driver to resolve this work item and return the result it
            // provides.
            let (sender, receiver) = oneshot::channel();
            assert!(running_tasks
                .lock()
                .insert(key, TestRunningTask::<C, O> { unblocker: sender, context })
                .is_none());
            async move { receiver.await.unwrap() }
        };

        let (mut processor, enqueue) = work_queue(concurrency, do_work);
        let done = Arc::new(Mutex::new(Vec::new()));
        let terminated = Arc::new(Mutex::new(false));
        let results =
            TestQueueResults { done: Arc::clone(&done), terminated: Arc::clone(&terminated) };

        spawner
            .spawn_local(async move {
                while let Some(res) = processor.next().await {
                    done.lock().push(res);
                }
                *terminated.lock() = true;
            })
            .expect("spawn to succeed");

        (enqueue, running, results)
    }

    #[test]
    fn processes_known_work_before_stalling() {
        let mut executor = futures::executor::LocalPool::new();

        let (enqueue, running, done) =
            spawn_test_work_queue::<&str, (), usize>(executor.spawner(), 2);

        let task_hello = enqueue.push("hello", ());
        let task_world = enqueue.push("world!", ());
        let task_test = enqueue.push("test", ());
        executor.run_until_stalled();
        assert_eq!(done.take(), Vec::<&str>::new());

        running.resolve("hello", 5);
        running.resolve("world!", 6);
        running.assert_empty();
        executor.run_until_stalled();
        assert_eq!(done.take(), vec!["hello", "world!"]);

        assert_eq!(executor.run_until(task_hello), Ok(5));
        assert_eq!(executor.run_until(task_world), Ok(6));

        running.resolve("test", 4);
        assert_eq!(executor.run_until(task_test), Ok(4));
        assert_eq!(done.take(), vec!["test"]);
    }

    #[test]
    fn restarts_after_draining_input_queue() {
        let mut executor = futures::executor::LocalPool::new();

        let (enqueue, running, done) = spawn_test_work_queue::<&str, (), ()>(executor.spawner(), 2);

        // Process a few tasks to completion through the queue.
        let task_a = enqueue.push("a", ());
        let task_b = enqueue.push("b", ());
        executor.run_until_stalled();
        running.resolve("a", ());
        running.resolve("b", ());
        assert_eq!(executor.run_until(task_a), Ok(()));
        assert_eq!(executor.run_until(task_b), Ok(()));
        assert_eq!(done.take(), vec!["a", "b"]);

        // Ensure the queue processes more tasks after its inner FuturesUnordered queue has
        // previously terminated.
        let task_c = enqueue.push("c", ());
        executor.run_until_stalled();
        running.resolve("c", ());
        assert_eq!(executor.run_until(task_c), Ok(()));
        assert_eq!(done.take(), vec!["c"]);

        // Also ensure the queue itself terminates once all send handles are dropped and all tasks
        // are complete.
        let task_a = enqueue.push("a", ());
        let task_d = enqueue.push("d", ());
        drop(enqueue);
        executor.run_until_stalled();
        assert!(!done.is_terminated());
        assert_eq!(done.take(), Vec::<&str>::new());
        running.resolve("a", ());
        running.resolve("d", ());
        assert_eq!(executor.run_until(task_a), Ok(()));
        assert_eq!(executor.run_until(task_d), Ok(()));
        assert_eq!(done.take(), vec!["a", "d"]);
        assert!(done.is_terminated());
    }

    #[test]
    fn push_all() {
        let mut executor = futures::executor::LocalPool::new();

        let (enqueue, running, done) =
            spawn_test_work_queue::<&str, (), usize>(executor.spawner(), 2);

        let mut futs =
            enqueue.push_all(vec![("a", ()), ("b", ()), ("c", ()), ("b", ())].into_iter());
        running.assert_empty();

        executor.run_until_stalled();
        running.resolve("a", 1);
        running.resolve("b", 2);
        running.assert_empty();
        assert_eq!(executor.run_until(futs.next().unwrap()), Ok(1));
        assert_eq!(executor.run_until(futs.next().unwrap()), Ok(2));

        running.resolve("c", 3);
        running.assert_empty();
        assert_eq!(executor.run_until(futs.next().unwrap()), Ok(3));
        assert_eq!(executor.run_until(futs.next().unwrap()), Ok(2));
        assert!(futs.next().is_none());

        assert_eq!(done.take(), vec!["a", "b", "c"]);
    }

    #[test]
    fn handles_many_tasks() {
        let mut executor = futures::executor::LocalPool::new();

        let (enqueue, running, done) =
            spawn_test_work_queue::<String, (), ()>(executor.spawner(), 5);

        let mut tasks = FuturesUnordered::new();

        for i in 0..10000 {
            let key = format!("task_{}", i);
            tasks.push(enqueue.push(key, ()));
        }

        // also queue up some duplicate tasks.
        let task_dups = enqueue
            .push_all((0..10000).filter(|i| i % 2 == 0).map(|i| {
                let key = format!("task_{}", i);
                (key, ())
            }))
            .collect::<FuturesUnordered<_>>();

        executor.run_until_stalled();

        while let Some(key) = running.peek() {
            running.resolve(&key, ());
            assert_eq!(executor.run_until(tasks.next()), Some(Ok(())));
            assert_eq!(done.take(), vec![key]);
        }

        assert_eq!(executor.run_until(task_dups.collect::<Vec<_>>()), vec![Ok(()); 5000]);
    }

    #[test]
    fn dedups_compound_keys() {
        let mut executor = futures::executor::LocalPool::new();

        #[derive(Debug, Clone, PartialEq, Eq, Hash)]
        struct Params<'a> {
            key: &'a str,
            options: &'a [&'a str],
        }

        let (enqueue, running, done) =
            spawn_test_work_queue::<Params<'_>, (), &str>(executor.spawner(), 5);

        let key_a = Params { key: "first", options: &[] };
        let key_b = Params { key: "first", options: &["unique"] };
        let task_a1 = enqueue.push(key_a.clone(), ());
        let task_b = enqueue.push(key_b.clone(), ());
        let task_a2 = enqueue.push(key_a.clone(), ());

        executor.run_until_stalled();

        running.resolve(&key_b, "first_unique");
        executor.run_until_stalled();
        assert_eq!(done.take(), vec![key_b]);
        assert_eq!(executor.run_until(task_b), Ok("first_unique"));

        running.resolve(&key_a, "first_no_options");
        executor.run_until_stalled();
        assert_eq!(done.take(), vec![key_a]);
        assert_eq!(executor.run_until(task_a2), Ok("first_no_options"));
        assert_eq!(executor.run_until(task_a1), Ok("first_no_options"));
    }

    #[test]
    fn merges_context_of_pending_tasks() {
        let mut executor = futures::executor::LocalPool::new();

        #[derive(Default, Debug, PartialEq, Eq)]
        struct MyContext(String);

        impl TryMerge for MyContext {
            fn try_merge(&mut self, other: Self) -> Result<(), Self> {
                self.0.push_str(&other.0);
                Ok(())
            }
        }

        let (enqueue, running, done) =
            spawn_test_work_queue::<&str, MyContext, ()>(executor.spawner(), 1);

        let task_a = enqueue.push("dup", MyContext("a".into()));
        let task_unique = enqueue.push("unique", MyContext("not-deduped".into()));
        let task_b = enqueue.push("dup", MyContext("b".into()));
        executor.run_until_stalled();
        let task_c1 = enqueue.push("dup", MyContext("c".into()));
        executor.run_until_stalled();

        // "c" not merged in since "dup" was already running with different context.
        assert_eq!(running.resolve("dup", ()), MyContext("ab".into()));
        assert_eq!(executor.run_until(task_a), Ok(()));
        assert_eq!(executor.run_until(task_b), Ok(()));
        assert_eq!(done.take(), vec!["dup"]);

        // even though "unique" was added to the queue before "dup"/"c", "dup" is given priority
        // since it was already running.
        assert_eq!(running.keys(), vec!["dup"]);
        assert_eq!(running.resolve("dup", ()), MyContext("c".into()));
        assert_eq!(executor.run_until(task_c1), Ok(()));
        assert_eq!(done.take(), vec!["dup"]);

        assert_eq!(running.resolve("unique", ()), MyContext("not-deduped".into()));
        assert_eq!(executor.run_until(task_unique), Ok(()));
        assert_eq!(done.take(), vec!["unique"]);
        running.assert_empty();

        // ensure re-running a previously completed item executes it again.
        let task_c2 = enqueue.push("dup", MyContext("c".into()));
        executor.run_until_stalled();
        assert_eq!(running.keys(), vec!["dup"]);
        assert_eq!(running.resolve("dup", ()), MyContext("c".into()));
        assert_eq!(executor.run_until(task_c2), Ok(()));
        assert_eq!(done.take(), vec!["dup"]);
        running.assert_empty();
    }
}
