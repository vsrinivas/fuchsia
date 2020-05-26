// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use futures::{
    lock::{Mutex, MutexGuard, MutexLockFuture},
    prelude::*,
};
use rental::*;
use std::collections::HashMap;
use std::pin::Pin;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Weak};
use std::task::{Context, Poll, Waker};

/// Takes a future that returns an error, and transforms it to a future that logs said error
pub async fn log_errors(f: impl Send + Future<Output = Result<(), Error>>, message: &'static str) {
    if let Err(e) = f.await {
        log::warn!("{}: {:?}", message, e);
    }
}

#[derive(Debug)]
pub struct PollMutex<'a, T> {
    mutex: &'a Mutex<T>,
    lock: Option<MutexLockFuture<'a, T>>,
}

impl<'a, T> PollMutex<'a, T> {
    pub fn new(mutex: &'a Mutex<T>) -> PollMutex<'a, T> {
        PollMutex { mutex, lock: None }
    }

    pub fn poll(&mut self, ctx: &mut Context<'_>) -> Poll<MutexGuard<'a, T>> {
        let mut lock_fut = match self.lock.take() {
            None => self.mutex.lock(),
            Some(lock_fut) => lock_fut,
        };
        match lock_fut.poll_unpin(ctx) {
            Poll::Pending => {
                self.lock = Some(lock_fut);
                Poll::Pending
            }
            Poll::Ready(g) => Poll::Ready(g),
        }
    }
}

#[cfg(test)]
mod poll_mutex_test {

    use super::PollMutex;
    use crate::runtime::{run, wait_until};
    use anyhow::{format_err, Error};
    use futures::{
        channel::oneshot,
        future::{poll_fn, try_join},
        lock::Mutex,
        task::noop_waker_ref,
    };
    use matches::assert_matches;
    use std::{
        task::{Context, Poll},
        time::{Duration, Instant},
    };

    #[test]
    fn basics() {
        run(async move {
            let mutex = Mutex::new(42);
            let mut ctx = Context::from_waker(noop_waker_ref());
            let mut poll_mutex = PollMutex::new(&mutex);
            assert_matches!(poll_mutex.poll(&mut ctx), Poll::Ready(_));
            assert_matches!(poll_mutex.poll(&mut ctx), Poll::Ready(_));
            assert_matches!(poll_mutex.poll(&mut ctx), Poll::Ready(_));
            let mutex_guard = mutex.lock().await;
            assert_matches!(poll_mutex.poll(&mut ctx), Poll::Pending);
            assert_matches!(poll_mutex.poll(&mut ctx), Poll::Pending);
            drop(mutex_guard);
            assert_matches!(poll_mutex.poll(&mut ctx), Poll::Ready(_));
        })
    }

    #[test]
    fn wakes_up() -> Result<(), Error> {
        run(async move {
            let mutex = Mutex::new(42u8);
            let (tx_saw_first_pending, rx_saw_first_pending) = oneshot::channel();
            let mut poll_mutex = PollMutex::new(&mutex);
            let mutex_guard = mutex.lock().await;
            try_join(
                async move {
                    assert_matches!(
                        poll_mutex.poll(&mut Context::from_waker(noop_waker_ref())),
                        Poll::Pending
                    );
                    tx_saw_first_pending.send(()).map_err(|_| format_err!("cancelled"))?;
                    assert_eq!(*poll_fn(|ctx| poll_mutex.poll(ctx)).await, 42);
                    Ok(())
                },
                async move {
                    rx_saw_first_pending.await?;
                    wait_until(Instant::now() + Duration::from_millis(300)).await;
                    drop(mutex_guard);
                    Ok(())
                },
            )
            .await
            .map(|_| ())
        })
    }
}

pub(crate) trait LockInner: 'static {
    type Inner;
    fn lock_inner<'a>(&'a self) -> MutexLockFuture<'a, Self::Inner>;
}

rental! {
    mod rentals {
        use super::*;

        #[rental(covariant)]
        pub(super) struct PollWeakMutexLocking<T: LockInner> {
            inner: Arc<T>,
            lock: MutexLockFuture<'inner, T::Inner>,
        }
    }
}

use rentals::PollWeakMutexLocking;

pub(crate) struct PollWeakMutex<T: LockInner> {
    weak: Weak<T>,
    lock: Option<PollWeakMutexLocking<T>>,
}

impl<T: LockInner> PollWeakMutex<T> {
    pub fn new(weak: Weak<T>) -> Self {
        Self { weak, lock: None }
    }

    pub fn poll_fn<R>(
        &mut self,
        ctx: &mut Context<'_>,
        f: impl FnOnce(&mut Context<'_>, &mut MutexGuard<'_, T::Inner>) -> Poll<R>,
    ) -> Poll<Option<R>> {
        let mut locking = match self.lock.take() {
            None => match Weak::upgrade(&self.weak) {
                None => return Poll::Ready(None),
                Some(inner) => PollWeakMutexLocking::new(inner, |inner| inner.lock_inner()),
            },
            Some(locking) => locking,
        };
        let (keep, ret) = locking.rent_mut(|lock| match lock.poll_unpin(ctx) {
            Poll::Pending => (true, Poll::Pending),
            Poll::Ready(mut g) => (false, f(ctx, &mut g).map(Some)),
        });
        if keep {
            self.lock = Some(locking);
        }
        ret
    }
}

struct ObservableState<T> {
    current: T,
    version: u64,
    waiters: HashMap<u64, Waker>,
}

struct ObservableInner<T> {
    state: Mutex<ObservableState<T>>,
    next_observer_key: AtomicU64,
}

impl<T: 'static> LockInner for ObservableInner<T> {
    type Inner = ObservableState<T>;
    fn lock_inner<'a>(&'a self) -> MutexLockFuture<'a, ObservableState<T>> {
        self.state.lock()
    }
}

static NEXT_TRACE_ID: AtomicU64 = AtomicU64::new(0);

pub struct Observable<T>(Arc<ObservableInner<T>>, Option<u64>);

pub struct Observer<T: 'static> {
    lock: PollWeakMutex<ObservableInner<T>>,
    version: u64,
    key: u64,
    traced: Option<u64>,
}

/// Asynchronously observable item... observers will be notified on changes, and see a None when
/// the Observable blinks out of existance.
/// If an Observer misses an edit, that Observer never sees the edit (i.e. Observer's only see the
/// most recent change).
impl<T: std::fmt::Debug> Observable<T> {
    pub fn new(current: T) -> Self {
        Self::new_traced(current, false)
    }

    pub fn new_traced(current: T, traced: bool) -> Self {
        Self(
            Arc::new(ObservableInner {
                state: Mutex::new(ObservableState { version: 1, current, waiters: HashMap::new() }),
                next_observer_key: AtomicU64::new(0),
            }),
            if traced { Some(NEXT_TRACE_ID.fetch_add(1, Ordering::Relaxed)) } else { None },
        )
    }

    pub fn new_observer(&self) -> Observer<T> {
        Observer {
            lock: PollWeakMutex::new(Arc::downgrade(&self.0)),
            version: 0,
            key: self.0.next_observer_key.fetch_add(1, Ordering::Relaxed),
            traced: self.1,
        }
    }

    pub async fn edit(&self, f: impl FnOnce(&mut T)) {
        let mut inner = self.0.state.lock().await;
        if let Some(trace_id) = self.1 {
            log::info!("[{}] edit version={} prev={:?}", trace_id, inner.version, inner.current);
        }
        f(&mut inner.current);
        inner.version += 1;
        if let Some(trace_id) = self.1 {
            log::info!(
                "[{}] edited version={} current={:?}",
                trace_id,
                inner.version,
                inner.current
            );
        }
        std::mem::replace(&mut inner.waiters, Default::default())
            .into_iter()
            .for_each(|(_, w)| w.wake());
    }

    pub async fn push(&self, new: T) {
        self.edit(|current| *current = new).await;
    }

    pub async fn current(&self) -> T
    where
        T: Clone,
    {
        self.0.state.lock().await.current.clone()
    }
}

impl<T: Clone + std::fmt::Debug> futures::Stream for Observer<T> {
    type Item = T;
    fn poll_next(self: Pin<&mut Self>, ctx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        let this = &mut Pin::into_inner(self);
        let this_version = &mut this.version;
        let this_key = this.key;
        let trace = this.traced;
        this.lock.poll_fn(ctx, |ctx, guard| {
            if let Some(trace_id) = trace {
                log::info!(
                    "[{}] poll observer: this.version={} observable.version={} value={:?}",
                    trace_id,
                    *this_version,
                    guard.version,
                    guard.current
                );
            }
            if *this_version != guard.version {
                *this_version = guard.version;
                Poll::Ready(guard.current.clone())
            } else {
                guard.waiters.insert(this_key, ctx.waker().clone());
                Poll::Pending
            }
        })
    }
}

pub(crate) struct CompositeStream<Incoming, Producer> {
    incoming: Option<Incoming>,
    active: Vec<Producer>,
}

impl<Incoming: Stream<Item = Producer> + Send + Unpin, Producer: Stream + Send + Unpin> Stream
    for CompositeStream<Incoming, Producer>
{
    type Item = Producer::Item;

    fn poll_next(self: Pin<&mut Self>, ctx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        let this = Pin::into_inner(self);
        while let Some(incoming) = this.incoming.as_mut() {
            match incoming.poll_next_unpin(ctx) {
                Poll::Ready(Some(producer)) => this.active.push(producer),
                Poll::Ready(None) => {
                    this.incoming = None;
                    break;
                }
                Poll::Pending => break,
            }
        }
        let mut done = Vec::new();
        let mut out = None;
        for (i, active) in this.active.iter_mut().enumerate() {
            match active.poll_next_unpin(ctx) {
                Poll::Ready(Some(next)) => {
                    let last = this.active.len() - 1;
                    this.active.swap(i, last);
                    out = Some(next);
                    break;
                }
                Poll::Ready(None) => done.push(i),
                Poll::Pending => (),
            }
        }
        for i in done.into_iter().rev() {
            this.active.swap_remove(i);
        }
        if let Some(next) = out {
            Poll::Ready(Some(next))
        } else if this.incoming.is_none() {
            Poll::Ready(None)
        } else {
            Poll::Pending
        }
    }
}

pub(crate) trait CompositeStreamExt {
    type Composite;
    fn composite(self) -> Self::Composite
    where
        Self: Sized;
}

impl<S: Stream> CompositeStreamExt for S {
    type Composite = CompositeStream<S, S::Item>;
    fn composite(self) -> CompositeStream<S, S::Item>
    where
        Self: Sized,
    {
        CompositeStream { incoming: Some(self), active: Vec::new() }
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::router::test_util::run;

    #[test]
    fn observable_basics() {
        run(|| async move {
            let observable = Observable::new(1);
            let mut observer = observable.new_observer();
            assert_eq!(observer.next().await, Some(1));
            observable.push(2).await;
            assert_eq!(observer.next().await, Some(2));
            observable.push(3).await;
            observable.push(4).await;
            assert_eq!(observer.next().await, Some(4));
            drop(observable);
            assert_eq!(observer.next().await, None);
        });
    }
}
