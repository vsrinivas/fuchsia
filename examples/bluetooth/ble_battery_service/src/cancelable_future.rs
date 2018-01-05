// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern crate futures;

use futures::{Future, Poll, Async};
use futures::task::AtomicTask;
use std::sync::Arc;
use std::sync::atomic::{AtomicBool, Ordering};

// This file provides support for cancelable futures (thanks cramertj@!). A PR
// has been sent to futures-rs. Remove this once that lands and makes it into
// Fuchsia (see https://github.com/alexcrichton/futures-rs/issues/693).

/// A future which can be cancelled using a `CancelHandle`.
#[derive(Debug, Clone)]
pub struct Cancelable<T> {
    future: T,
    inner: Arc<CancelInner>,
}

impl<T> Cancelable<T>
where
    T: Future<Item = ()>,
{
    pub fn new(future: T, handle: &CancelHandle) -> Self {
        Cancelable {
            future,
            inner: handle.inner.clone(),
        }
    }
}

/// A handle to a `Cancelable` future.
#[derive(Debug, Clone)]
pub struct CancelHandle {
    inner: Arc<CancelInner>,
}

impl CancelHandle {
    pub fn new() -> Self {
        CancelHandle {
            inner: Arc::new(CancelInner {
                task: AtomicTask::new(),
                cancel: AtomicBool::new(false),
            }),
        }
    }
}

// Inner type storing the task to awaken and a bool indicating that it
// should be cancelled.
#[derive(Debug)]
struct CancelInner {
    task: AtomicTask,
    cancel: AtomicBool,
}

impl<T> Future for Cancelable<T>
where
    T: Future<Item = ()>,
{
    type Item = ();
    type Error = T::Error;
    fn poll(&mut self) -> Poll<Self::Item, Self::Error> {
        if self.inner.cancel.load(Ordering::Acquire) {
            Ok(Async::Ready(()))
        } else {
            match self.future.poll() {
                Ok(Async::NotReady) => {
                    self.inner.task.register();
                    Ok(Async::NotReady)
                }
                res => res,
            }
        }
    }
}

impl CancelHandle {
    /// Cancel the `Cancelable` future associated with this handle.
    pub fn cancel(&self) {
        self.inner.cancel.store(true, Ordering::Release);
        self.inner.task.notify();
    }
}
