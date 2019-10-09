// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(missing_docs)]

//! Concurrent work queue helpers

use {
    futures::{
        channel::{mpsc, oneshot},
        future::{BoxFuture, Shared},
        prelude::*,
        ready,
        stream::{FusedStream, FuturesUnordered},
        task::{Context, Poll},
    },
    parking_lot::Mutex,
    pin_utils::unsafe_pinned,
    std::{
        collections::{HashMap, VecDeque},
        hash::Hash,
        pin::Pin,
        sync::{Arc, Weak},
    },
};

mod error;
pub use error::TaskError;

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
    /// `Ok(other)`, leaving `self` unchanged if the instances could not be merged.
    fn try_merge(&mut self, other: Self) -> Result<(), Self>;
}

impl TryMerge for () {
    fn try_merge(&mut self, _: ()) -> Result<(), Self> {
        Ok(())
    }
}

/// Creates an unbounded queue of work tasks that will execute up to `concurrency` `work_fn`s at once.
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
///     while let Some(res) = processor.next().await {
///         match res {
///             Ok(key) => println!("Successfully processed {}", key),
///             Err(key) => println!("Error while processing {}", key),
///         }
///     }
///
///     for (crate_name, fut) in join_handles {
///         let res = fut.await.expect("downloads can't fail, right?");
///         println!("Contents of {}: {:?}", crate_name, res);
///     }
/// });
/// ```
pub fn work_queue<W, WF, K, C, O, E>(
    concurrency: usize,
    work_fn: W,
) -> (WorkQueue<W, K, C, O, E>, WorkSender<K, C, O, E>)
where
    W: Fn(K, C) -> WF,
    WF: Future<Output = Result<O, E>>,
    K: Clone + Eq + Hash + Send + 'static,
    C: TryMerge + Send + 'static,
    O: Send + 'static,
    E: Send + 'static,
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

/// A work queue that processes a configurable number of tasks concurrently, deduplicating work
/// with the same key.
///
/// Items are yielded from the stream in the order that they are processed, which may differ from
/// the order that items are enqueued, depending on which concurrent tasks complete first.
pub struct WorkQueue<W, K, C, O, E> {
    /// The work callback function.
    work_fn: W,
    /// Maximum number of tasks to run concurrently.
    concurrency: usize,
    /// Metadata about pending and running work. Modified by the queue itself when running tasks
    /// and by [WorkSender] to add new tasks to the queue.
    tasks: Arc<Mutex<HashMap<K, VecDeque<WorkInfo<C, O, E>>>>>,

    // Pinned fields. Must be kept up to date with Unpin impl and unsafe_pinned!() calls.
    /// Receiving end of the queue.
    pending: mpsc::UnboundedReceiver<K>,
    /// Tasks currently being run. Will contain [0, concurrency] futures at any given time.
    running: FuturesUnordered<RunningTask<K, O, E>>,
}

impl<W, K, C, O, E> Unpin for WorkQueue<W, K, C, O, E>
where
    FuturesUnordered<RunningTask<K, O, E>>: Unpin,
    mpsc::UnboundedReceiver<K>: Unpin,
{
}

impl<W, WF, K, C, O, E> WorkQueue<W, K, C, O, E>
where
    W: Fn(K, C) -> WF,
    WF: Future<Output = Result<O, E>>,
    K: Clone + Eq + Hash + Send + 'static,
    WF: Send + 'static,
    O: Send + 'static,
    E: Send + 'static,
{
    // safe because:
    // * WorkQueue does not implement Drop or use repr(packed)
    // * Unpin is only implemented if all pinned fields implement Unpin
    unsafe_pinned!(pending: mpsc::UnboundedReceiver<K>);
    unsafe_pinned!(running: FuturesUnordered<RunningTask<K, O, E>>);

    /// Converts this stream of Result<K, K> into a single future that resolves when the stream is
    /// terminated.
    pub fn into_future(self) -> impl Future<Output = ()> {
        self.map(|_res| ()).collect::<()>()
    }

    /// Starts new work if under the concurrency limit and work is enqueued.
    /// Returns:
    /// * Poll::Ready(None) if the input work queue is empty and closed.
    /// * Poll::Ready(Some(())) if new work was started.
    /// * Poll::Pending if at the concurrency limit or no work is enqueued.
    fn find_work(mut self: Pin<&mut Self>, cx: &mut Context) -> Poll<Option<()>> {
        // Nothing to do if the stream of requests is EOF.
        if self.as_mut().pending().is_terminated() {
            return Poll::Ready(None);
        }

        let mut found = false;
        while self.as_mut().running().len() < self.concurrency {
            match ready!(self.as_mut().pending().poll_next(cx)) {
                None => break,
                Some(key) => {
                    found = true;

                    // Transition the work info to the running state, claiming the context.
                    let context = self
                        .tasks
                        .lock()
                        .get_mut(&key)
                        .expect("map entry to exist if in pending queue")[0]
                        .context
                        .take()
                        .expect("context to not yet be claimed");

                    // WorkSender::push_entry guarantees that key will only be pushed into pending
                    // if it created the entry in the map, so it is guaranteed here that multiple
                    // instances of the same key will not be executed concurrently.
                    let work = (self.work_fn)(key.clone(), context);
                    let fut = async move { (key, work.await) }.boxed();

                    self.as_mut().running().push(fut);
                }
            }
        }
        if found {
            Poll::Ready(Some(()))
        } else {
            Poll::Pending
        }
    }

    fn do_work(mut self: Pin<&mut Self>, cx: &mut Context) -> Poll<Option<Result<K, K>>> {
        match self.as_mut().running().poll_next(cx) {
            Poll::Pending => Poll::Pending,
            Poll::Ready(None) => {
                // self.running is now terminated, but unlike the guarantees given by other
                // FusedStream implementations, FuturesUnordered can continue to be polled (unless
                // new work comes in, it will continue to return Poll::Ready(None)), and new
                // futures can be pushed into it. Pushing new work on a terminated
                // FuturesUnordered will cause is_terminated to return false, and polling the
                // stream will start the task.
                if self.as_mut().pending().is_terminated() {
                    Poll::Ready(None)
                } else {
                    Poll::Pending
                }
            }
            Poll::Ready(Some((key, res))) => {
                let cb = {
                    let mut tasks = self.tasks.lock();
                    let infos: &mut VecDeque<WorkInfo<_, _, _>> =
                        tasks.get_mut(&key).expect("key to exist in map if not resolved");

                    let cb = infos.pop_front().expect("work item entry to still exist").cb;

                    if infos.is_empty() {
                        // last pending operation with this key
                        tasks.remove(&key);
                    } else {
                        // start the next operation immediately
                        let context =
                            infos[0].context.take().expect("context to not yet be claimed");

                        let work = (self.work_fn)(key.clone(), context);
                        let key_clone = key.clone();
                        let fut = async move { (key_clone, work.await) }.boxed();

                        drop(tasks);
                        self.as_mut().running().push(fut);
                    }
                    cb
                };

                let key = match res {
                    Ok(_) => Ok(key),
                    Err(_) => Err(key),
                };

                // As the shared future was just removed from the map, if all clones of that future
                // have also been dropped, this send can fail. Silently ignore that error.
                let _ = cb.send(res);

                // Yield the key that was processed to the stream, indicating if proccessing that
                // value was successful or not.
                Poll::Ready(Some(key))
            }
        }
    }
}

impl<W, WF, K, C, O, E> Stream for WorkQueue<W, K, C, O, E>
where
    W: Fn(K, C) -> WF,
    WF: Future<Output = Result<O, E>> + Send + 'static,
    K: Clone + Eq + Hash + Send + 'static,
    O: Send + 'static,
    E: Send + 'static,
{
    type Item = Result<K, K>;
    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context) -> Poll<Option<Self::Item>> {
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

// With feature(type_alias_impl_trait), boxing the future would not be necessary, which would also
// remove the requirements that O and E are Send + 'static.
//type TaskFuture<O, E> = impl Future<Output=Result<O, TaskError<E>>>;
type TaskFuture<O, E> = BoxFuture<'static, Result<O, TaskError<E>>>;
type RunningTask<K, O, E> = BoxFuture<'static, (K, Result<O, E>)>;

/// Shared state for a single work item. Contains the sending end of the shared future for sending
/// the result of the task, the clonable shared future that will resolve to that result, and, for
/// tasks, that are not yet running, the context.
pub struct WorkInfo<C, O, E> {
    // Starting to run a task will take the context, so `context` will be None iff the task is
    // running.
    context: Option<C>,
    cb: oneshot::Sender<Result<O, E>>,
    fut: Shared<TaskFuture<O, E>>,
}

/// A clonable handle to the work queue.  When all clones of [WorkSender] are dropped, the queue
/// will process all remaining requests and terminate its output stream.
#[derive(Clone)]
pub struct WorkSender<K, C, O, E> {
    sender: mpsc::UnboundedSender<K>,
    // Weak reference to ensure that if the queue is dropped, the now unused sender end of the
    // completion callback will be dropped too, canceling the request.
    tasks: Weak<Mutex<HashMap<K, VecDeque<WorkInfo<C, O, E>>>>>,
}

fn make_broadcast_pair<O, E>() -> (oneshot::Sender<Result<O, E>>, Shared<TaskFuture<O, E>>)
where
    O: Clone + Send + 'static,
    E: Clone + Send + 'static,
{
    let (sender, receiver) = oneshot::channel();
    let fut = async move {
        match receiver.await {
            Ok(Ok(o)) => Ok(o),
            Err(oneshot::Canceled) => Err(TaskError::Canceled),
            Ok(Err(e)) => Err(TaskError::Inner(e)),
        }
    }
        .boxed()
        .shared();

    (sender, fut)
}

impl<K, C, O, E> WorkSender<K, C, O, E>
where
    K: Clone + Send + Eq + Hash,
    C: TryMerge,
    O: Clone + Send + 'static,
    E: Clone + Send + 'static,
{
    /// Enqueue the given key to be processed by a worker, or attach to an existing request to
    /// process this key.
    pub fn push(&self, key: K, context: C) -> impl Future<Output = Result<O, TaskError<E>>> {
        let tasks = match self.tasks.upgrade() {
            Some(tasks) => tasks,
            None => {
                // Work queue no longer exists. Immediately cancel this request.
                let (_, fut) = make_broadcast_pair();
                return fut;
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
    /// `QueueSender::push`.
    pub fn push_all(
        &self,
        entries: impl Iterator<Item = (K, C)>,
    ) -> impl Iterator<Item = impl Future<Output = Result<O, TaskError<E>>>> {
        let mut tasks = self.tasks.upgrade();
        let mut tasks = tasks.as_mut().map(|tasks| tasks.lock());

        entries
            .map(move |(key, context)| {
                if let Some(ref mut tasks) = tasks {
                    Self::push_entry(&mut *tasks, &self.sender, key, context)
                } else {
                    // Work queue no longer exists. Immediately cancel this request.
                    let (_, fut) = make_broadcast_pair();
                    return fut;
                }
            })
            .collect::<Vec<_>>()
            .into_iter()
    }

    fn push_entry(
        tasks: &mut HashMap<K, VecDeque<WorkInfo<C, O, E>>>,
        self_sender: &mpsc::UnboundedSender<K>,
        key: K,
        mut context: C,
    ) -> Shared<BoxFuture<'static, Result<O, TaskError<E>>>> {
        use std::collections::hash_map::Entry;

        match tasks.entry(key.clone()) {
            Entry::Vacant(entry) => {
                let (sender, fut) = make_broadcast_pair();
                if let Ok(()) = self_sender.unbounded_send(key) {
                    // Register the pending task in the map so duplicate request can simply attach
                    // to the existing task.
                    entry.insert(
                        vec![WorkInfo { context: Some(context), cb: sender, fut: fut.clone() }]
                            .into(),
                    );
                } else {
                    // Work queue no longer exists. Dropping work resolves this task as canceled.
                }
                fut
            }
            Entry::Occupied(mut entry) => {
                // First try to try_merge this task with another queued or running task.
                for info in entry.get_mut().iter_mut() {
                    if let Some(c) = &mut info.context {
                        if let Err(unmerged) = c.try_merge(context) {
                            context = unmerged;
                        } else {
                            return info.fut.clone();
                        }
                    }
                }

                // Otherwise, enqueue a new task.
                let (sender, fut) = make_broadcast_pair();

                // Instead of enqueueing this task to be run, just append this to the existing list
                // of tasks for this key. When an earlier task completes, it will pick up the next
                // in the list.
                entry.get_mut().push_back(WorkInfo {
                    context: Some(context),
                    cb: sender,
                    fut: fut.clone(),
                });
                fut
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        futures::{
            executor::{block_on, LocalSpawner},
            task::LocalSpawnExt,
        },
        std::{borrow::Borrow, fmt},
    };

    #[test]
    fn basic_usage() {
        async fn do_work(_key: String, _context: ()) -> Result<(), ()> {
            Ok(())
        }

        let (processor, enqueue) = work_queue(3, do_work);

        let mut tasks = FuturesUnordered::new();

        tasks.push(enqueue.push("a".into(), ()));
        tasks.push(enqueue.push("a".into(), ()));
        tasks.push(enqueue.push("b".into(), ()));
        tasks.push(enqueue.push("a".into(), ()));
        tasks.push(enqueue.push("c".into(), ()));

        drop(enqueue);

        block_on(async move {
            let (keys, res) = futures::future::join(
                processor.collect::<Vec<Result<String, String>>>(),
                tasks.collect::<Vec<Result<(), _>>>(),
            )
            .await;
            assert_eq!(keys, vec![Ok("a".to_string()), Ok("b".into()), Ok("c".into())]);
            assert_eq!(res, vec![Ok(()), Ok(()), Ok(()), Ok(()), Ok(()),]);
        });
    }

    #[test]
    fn into_future() {
        async fn nop(key: i32, _context: ()) -> Result<i32, ()> {
            Ok(key)
        }
        let (processor, enqueue) = work_queue(1, nop);

        let res_fut =
            future::join3(processor.into_future(), enqueue.push(1, ()), enqueue.push(2, ()));
        drop(enqueue);

        let res = block_on(res_fut);
        assert_eq!(res, ((), Ok(1), Ok(2)));
    }

    #[derive(Debug, PartialEq, Eq)]
    struct MergeEqual(i32);

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
            assert_eq!(fut_early_a.await, Err(TaskError::Canceled));
            assert_eq!(fut_early_b.await, Err(TaskError::Canceled));
            assert_eq!(fut_early_c.await, Err(TaskError::Canceled));
            assert_eq!(fut_late.await, Err(TaskError::Canceled));

            let requests = vec![("1", MergeEqual(0)), ("2", MergeEqual(1)), ("1", MergeEqual(0))];
            for fut in enqueue.push_all(requests.into_iter()) {
                assert_eq!(fut.await, Err(TaskError::Canceled));
            }
        });
    }

    #[derive(Debug)]
    struct TestRunningTask<C, O, E> {
        unblocker: oneshot::Sender<Result<O, E>>,
        context: C,
    }

    #[derive(Debug)]
    struct TestRunningTasks<K, C, O, E>
    where
        K: Eq + Hash,
    {
        tasks: Arc<Mutex<HashMap<K, TestRunningTask<C, O, E>>>>,
    }

    impl<K, C, O, E> TestRunningTasks<K, C, O, E>
    where
        K: fmt::Debug + Eq + Hash + Sized + Clone,
        C: fmt::Debug,
        O: fmt::Debug,
        E: fmt::Debug,
    {
        fn new() -> Self {
            Self { tasks: Arc::new(Mutex::new(HashMap::new())) }
        }

        fn resolve<Q>(&self, key: &Q, res: Result<O, E>) -> C
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
        done: Arc<Mutex<Vec<Result<K, K>>>>,
        terminated: Arc<Mutex<bool>>,
    }

    impl<K> TestQueueResults<K> {
        fn take(&self) -> Vec<Result<K, K>> {
            std::mem::replace(&mut *self.done.lock(), vec![])
        }

        fn is_terminated(&self) -> bool {
            *self.terminated.lock()
        }
    }

    fn spawn_test_work_queue<K, C, O, E>(
        mut spawner: LocalSpawner,
        concurrency: usize,
    ) -> (WorkSender<K, C, O, E>, TestRunningTasks<K, C, O, E>, TestQueueResults<K>)
    where
        K: Send + Clone + fmt::Debug + Eq + Hash + 'static,
        C: TryMerge + Send + fmt::Debug + 'static,
        O: Send + Clone + fmt::Debug + 'static,
        E: Send + Clone + fmt::Debug + 'static,
    {
        let running = TestRunningTasks::<K, C, O, E>::new();
        let running_tasks = running.tasks.clone();
        let do_work = move |key: K, context: C| {
            // wait for the test driver to resolve this work item and return the result it
            // provides.
            let (sender, receiver) = oneshot::channel();
            assert!(running_tasks
                .lock()
                .insert(key, TestRunningTask::<C, O, E> { unblocker: sender, context })
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
            spawn_test_work_queue::<&str, (), usize, ()>(executor.spawner(), 2);

        let task_hello = enqueue.push("hello", ());
        let task_world = enqueue.push("world!", ());
        let task_test = enqueue.push("test", ());
        executor.run_until_stalled();
        assert_eq!(done.take(), Vec::<Result<&str, &str>>::new());

        running.resolve("hello", Ok(5));
        running.resolve("world!", Ok(6));
        running.assert_empty();
        executor.run_until_stalled();
        assert_eq!(done.take(), vec![Ok("hello"), Ok("world!")]);

        assert_eq!(executor.run_until(task_hello), Ok(5));
        assert_eq!(executor.run_until(task_world), Ok(6));

        running.resolve("test", Ok(4));
        assert_eq!(executor.run_until(task_test), Ok(4));
        assert_eq!(done.take(), vec![Ok("test")]);
    }

    #[test]
    fn restarts_after_draining_input_queue() {
        let mut executor = futures::executor::LocalPool::new();

        let (enqueue, running, done) =
            spawn_test_work_queue::<&str, (), (), ()>(executor.spawner(), 2);

        // Process a few tasks to completion through the queue.
        let task_a = enqueue.push("a", ());
        let task_b = enqueue.push("b", ());
        executor.run_until_stalled();
        running.resolve("a", Ok(()));
        running.resolve("b", Ok(()));
        assert_eq!(executor.run_until(task_a), Ok(()));
        assert_eq!(executor.run_until(task_b), Ok(()));
        assert_eq!(done.take(), vec![Ok("a"), Ok("b")]);

        // Ensure the queue processes more tasks after its inner FuturesUnordered queue has
        // previously terminated.
        let task_c = enqueue.push("c", ());
        executor.run_until_stalled();
        running.resolve("c", Ok(()));
        assert_eq!(executor.run_until(task_c), Ok(()));
        assert_eq!(done.take(), vec![Ok("c")]);

        // Also ensure the queue itself terminates once all send handles are dropped and all tasks
        // are complete.
        let task_a = enqueue.push("a", ());
        let task_d = enqueue.push("d", ());
        drop(enqueue);
        executor.run_until_stalled();
        assert!(!done.is_terminated());
        assert_eq!(done.take(), vec![]);
        running.resolve("a", Ok(()));
        running.resolve("d", Ok(()));
        assert_eq!(executor.run_until(task_a), Ok(()));
        assert_eq!(executor.run_until(task_d), Ok(()));
        assert_eq!(done.take(), vec![Ok("a"), Ok("d")]);
        assert!(done.is_terminated());
    }

    #[test]
    fn exposes_failures_to_callbacks_and_stream() {
        let mut executor = futures::executor::LocalPool::new();

        let (enqueue, running, done) =
            spawn_test_work_queue::<&str, (), (), ()>(executor.spawner(), 1);

        let task_pass = enqueue.push("pass", ());
        let task_fail = enqueue.push("fail", ());
        executor.run_until_stalled();
        assert_eq!(done.take(), Vec::<Result<&str, &str>>::new());

        running.resolve("pass", Ok(()));
        running.assert_empty();
        assert_eq!(executor.run_until(task_pass), Ok(()));
        assert_eq!(done.take(), vec![Ok("pass")]);

        running.resolve("fail", Err(()));
        running.assert_empty();
        assert_eq!(executor.run_until(task_fail), Err(TaskError::Inner(())));
        assert_eq!(done.take(), vec![Err("fail")]);
    }

    #[test]
    fn push_all() {
        let mut executor = futures::executor::LocalPool::new();

        let (enqueue, running, done) =
            spawn_test_work_queue::<&str, (), usize, ()>(executor.spawner(), 2);

        let mut futs =
            enqueue.push_all(vec![("a", ()), ("b", ()), ("c", ()), ("b", ())].into_iter());
        running.assert_empty();

        executor.run_until_stalled();
        running.resolve("a", Ok(1));
        running.resolve("b", Ok(2));
        running.assert_empty();
        assert_eq!(executor.run_until(futs.next().unwrap()), Ok(1));
        assert_eq!(executor.run_until(futs.next().unwrap()), Ok(2));

        running.resolve("c", Ok(3));
        running.assert_empty();
        assert_eq!(executor.run_until(futs.next().unwrap()), Ok(3));
        assert_eq!(executor.run_until(futs.next().unwrap()), Ok(2));
        assert!(futs.next().is_none());

        assert_eq!(done.take(), vec![Ok("a"), Ok("b"), Ok("c")]);
    }

    #[test]
    fn handles_many_tasks() {
        let mut executor = futures::executor::LocalPool::new();

        let (enqueue, running, done) =
            spawn_test_work_queue::<String, (), (), ()>(executor.spawner(), 5);

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
            running.resolve(&key, Ok(()));
            assert_eq!(executor.run_until(tasks.next()), Some(Ok(())));
            assert_eq!(done.take(), vec![Ok(key)]);
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
            spawn_test_work_queue::<Params, (), &str, std::num::ParseIntError>(
                executor.spawner(),
                5,
            );

        let key_a = Params { key: "first", options: &[] };
        let key_b = Params { key: "first", options: &["unique"] };
        let task_a1 = enqueue.push(key_a.clone(), ());
        let task_b = enqueue.push(key_b.clone(), ());
        let task_a2 = enqueue.push(key_a.clone(), ());

        executor.run_until_stalled();

        running.resolve(&key_b, Ok("first_unique"));
        executor.run_until_stalled();
        assert_eq!(done.take(), vec![Ok(key_b)]);
        assert_eq!(executor.run_until(task_b), Ok("first_unique"));

        running.resolve(&key_a, Ok("first_no_options"));
        executor.run_until_stalled();
        assert_eq!(done.take(), vec![Ok(key_a)]);
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
            spawn_test_work_queue::<&str, MyContext, (), ()>(executor.spawner(), 1);

        let task_a = enqueue.push("dup", MyContext("a".into()));
        let task_unique = enqueue.push("unique", MyContext("not-deduped".into()));
        let task_b = enqueue.push("dup", MyContext("b".into()));
        executor.run_until_stalled();
        let task_c1 = enqueue.push("dup", MyContext("c".into()));
        executor.run_until_stalled();

        // "c" not merged in since "dup" was already running with different context.
        assert_eq!(running.resolve("dup", Ok(())), MyContext("ab".into()));
        assert_eq!(executor.run_until(task_a), Ok(()));
        assert_eq!(executor.run_until(task_b), Ok(()));
        assert_eq!(done.take(), vec![Ok("dup")]);

        // even though "unique" was added to the queue before "dup"/"c", "dup" is given priority
        // since it was already running.
        assert_eq!(running.keys(), vec!["dup"]);
        assert_eq!(running.resolve("dup", Ok(())), MyContext("c".into()));
        assert_eq!(executor.run_until(task_c1), Ok(()));
        assert_eq!(done.take(), vec![Ok("dup")]);

        assert_eq!(running.resolve("unique", Ok(())), MyContext("not-deduped".into()));
        assert_eq!(executor.run_until(task_unique), Ok(()));
        assert_eq!(done.take(), vec![Ok("unique")]);
        running.assert_empty();

        // ensure re-running a previously completed item executes it again.
        let task_c2 = enqueue.push("dup", MyContext("c".into()));
        executor.run_until_stalled();
        assert_eq!(running.keys(), vec!["dup"]);
        assert_eq!(running.resolve("dup", Ok(())), MyContext("c".into()));
        assert_eq!(executor.run_until(task_c2), Ok(()));
        assert_eq!(done.take(), vec![Ok("dup")]);
        running.assert_empty();
    }
}
