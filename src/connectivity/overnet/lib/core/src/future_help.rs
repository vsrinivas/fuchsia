// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use async_lock::RwLock;
use event_listener::Event;
use futures::prelude::*;
use std::sync::{Arc, Weak};

/// Takes a future that returns an error, and transforms it to a future that logs said error
pub async fn log_errors(
    f: impl Send + Future<Output = Result<(), Error>>,
    message: impl std::fmt::Display,
) {
    if let Err(e) = f.await {
        tracing::warn!("{}: {:?}", message, e);
    }
}

struct Stamped<T> {
    current: T,
    stamp: u64,
}

struct ObservableState<T> {
    value: RwLock<Stamped<T>>,
    next_version: Event,
}

impl<T> Drop for ObservableState<T> {
    fn drop(&mut self) {
        self.next_version.notify_relaxed(usize::MAX);
    }
}

pub struct Observable<T> {
    state: Arc<ObservableState<T>>,
}

pub struct Observer<T: 'static> {
    state: Weak<ObservableState<T>>,
    observed: u64,
}

/// Asynchronously observable item... observers will be notified on changes, and see a None when
/// the Observable blinks out of existance.
/// If an Observer misses an edit, that Observer never sees the edit (i.e. Observer's only see the
/// most recent change).
impl<T: std::fmt::Debug> Observable<T> {
    pub fn new(current: T) -> Self {
        Self {
            state: Arc::new(ObservableState {
                value: RwLock::new(Stamped { current, stamp: 1 }),
                next_version: Event::new(),
            }),
        }
    }

    pub fn new_observer(&self) -> Observer<T> {
        Observer { state: Arc::downgrade(&self.state), observed: 0 }
    }

    async fn maybe_mutate(&self, f: impl FnOnce(&mut T) -> bool) -> bool {
        let mut lock = self.state.value.write().await;
        let changed = f(&mut lock.current);
        if changed {
            lock.stamp += 1;
            self.state.next_version.notify_relaxed(usize::MAX);
        }
        changed
    }

    pub async fn edit(&self, f: impl FnOnce(&mut T)) {
        self.maybe_mutate(move |v| {
            f(v);
            true
        })
        .await;
    }

    pub async fn push(&self, new: T) {
        self.edit(|current| *current = new).await
    }

    pub async fn maybe_push(&self, new: T) -> bool
    where
        T: std::cmp::PartialEq,
    {
        self.maybe_mutate(move |current| {
            let change = *current != new;
            if change {
                *current = new;
            }
            change
        })
        .await
    }
}

impl<T: Clone + std::fmt::Debug> Observer<T> {
    pub async fn next(&mut self) -> Option<T> {
        while let Some(state) = Weak::upgrade(&self.state) {
            let lock = state.value.read().await;
            if lock.stamp != self.observed {
                self.observed = lock.stamp;
                return Some(lock.current.clone());
            }
            let next_version = state.next_version.listen();
            drop(lock);
            drop(state);
            next_version.await;
        }
        None
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[fuchsia::test]
    async fn observable_basics() {
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
    }
}
