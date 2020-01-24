// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use futures::prelude::*;
use std::cell::RefCell;
use std::collections::HashMap;
use std::pin::Pin;
use std::rc::{Rc, Weak};
use std::task::{Context, Poll, Waker};

/// Takes a future that returns an error, and transforms it to a future that logs said error
pub async fn log_errors(f: impl Future<Output = Result<(), Error>>, message: &'static str) {
    if let Err(e) = f.await {
        log::warn!("{}: {:?}", message, e);
    }
}

struct ObservableInner<T> {
    current: T,
    version: u64,
    waiters: HashMap<u64, Waker>,
    next_observer_key: u64,
}

pub struct Observable<T>(Rc<RefCell<ObservableInner<T>>>);
pub struct Observer<T> {
    inner: Weak<RefCell<ObservableInner<T>>>,
    version: u64,
    key: u64,
}

/// Single threaded asynchronously observable item... observers will be notified on changes, and see
/// a None when the Observable blinks out of existance.
/// If an Observer misses an edit, that Observer never sees the edit (i.e. Observer's only see the
/// most recent change).
impl<T> Observable<T> {
    pub fn new(current: T) -> Self {
        Self(Rc::new(RefCell::new(ObservableInner {
            version: 1,
            current,
            waiters: HashMap::new(),
            next_observer_key: 0,
        })))
    }

    pub fn new_observer(&self) -> Observer<T> {
        let mut inner = self.0.borrow_mut();
        let key = inner.next_observer_key;
        inner.next_observer_key += 1;
        Observer { inner: Rc::downgrade(&self.0), version: 0, key }
    }

    pub fn edit(&self, f: impl FnOnce(&mut T)) {
        let mut inner = self.0.borrow_mut();
        f(&mut inner.current);
        inner.version += 1;
        std::mem::replace(&mut inner.waiters, Default::default())
            .into_iter()
            .for_each(|(_, w)| w.wake());
    }

    pub fn push(&self, new: T) {
        self.edit(|current| *current = new);
    }
}

impl<T: Clone> futures::Stream for Observer<T> {
    type Item = T;
    fn poll_next(self: Pin<&mut Self>, ctx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        let mut this = &mut Pin::into_inner(self);
        if let Some(inner) = Weak::upgrade(&this.inner) {
            let mut inner = inner.borrow_mut();
            if this.version != inner.version {
                this.version = inner.version;
                Poll::Ready(Some(inner.current.clone()))
            } else {
                inner.waiters.insert(this.key, ctx.waker().clone());
                Poll::Pending
            }
        } else {
            Poll::Ready(None)
        }
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
            observable.push(2);
            assert_eq!(observer.next().await, Some(2));
            observable.push(3);
            observable.push(4);
            assert_eq!(observer.next().await, Some(4));
            drop(observable);
            assert_eq!(observer.next().await, None);
        });
    }
}
