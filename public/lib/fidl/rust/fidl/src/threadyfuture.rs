// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A somewhat heavyweight future implementation based on mutexes. It's designed
//! so that a simple "finished" value is efficient, but a deferred value is more
//! expensive (two allocations and some locking).
//!
//! In the longer term, this abstraction is not intended to remain. Rather, it is
//! to be replaced with futures-rs, and all client code strongly encouraged to be
//! fully asynchronous. The intent is that using a Future<T> abstraction will be
//! easier to migrate than, say, something based on callbacks.

use std::sync::{Arc, Mutex};
use std::thread;
use std::thread::Thread;
use std::mem;

trait Callback<T>: Send {
    fn call(self: Box<Self>, result: T);
}

impl<T, F: Send + FnOnce(T)> Callback<T> for F {
    fn call(self: Box<F>, result: T) {
        (*self)(result)
    }
}

enum PromiseState<T, E> {
    Start,
    Ok(T),
    Err(E),
    Waiting(Box<Callback<Result<T, E>>>),
    Parked(Thread),
    Done,
}

pub struct Promise<T, E>(Arc<Mutex<PromiseState<T, E>>>);

// TODO(raph): Future<T> should be a trait, with ThreadyFuture as an impl?
// TODO(raph): arguably should be a newtype to keep the contents private
pub enum Future<T, E> {
    Ok(T),
    Err(E),
    NotReady(Promise<T, E>),
}

impl<T: Send, E: Send> Promise<T, E> {
    /// Provide a value to a promise. This wakes up the associated future, either
    /// calling the callback (if waiting on `with`), or unparking the thread (if
    /// waiting on `get`).
    pub fn set_ok(self, value: T) {
        let mut state = self.0.lock().unwrap();
        match mem::replace(&mut *state, PromiseState::Done) {
            PromiseState::Start => *state = PromiseState::Ok(value),
            PromiseState::Waiting(f) => f.call(Ok(value)),
            PromiseState::Parked(thread) => {
                *state = PromiseState::Ok(value);
                thread.unpark();
            }
            _ => panic!("double write, can't happen")
        }
    }

    pub fn set_err(self, err: E) {
        let mut state = self.0.lock().unwrap();
        match mem::replace(&mut *state, PromiseState::Done) {
            PromiseState::Start => *state = PromiseState::Err(err),
            PromiseState::Waiting(f) => f.call(Err(err)),
            PromiseState::Parked(thread) => {
                *state = PromiseState::Err(err);
                thread.unpark();
            }
            _ => panic!("double write, can't happen")
        }
    }

    pub fn set_result(self, result: Result<T, E>) {
        match result {
            Ok(val) => self.set_ok(val),
            Err(err) => self.set_err(err),
        }
    }
}

impl<T: Send + 'static, E: Send + 'static> Future<T, E> {
    /// Create a new future that holds an immediately available value. This call
    /// is intended to be very efficient; no allocations or locking are needed.
    pub fn finished(value: T) -> Self {
        Future::Ok(value)
    }

    pub fn failed(err: E) -> Self {
        Future::Err(err)
    }

    pub fn done(result: Result<T, E>) -> Self {
        match result {
            Ok(value) => Future::finished(value),
            Err(err) => Future::failed(err),
        }
    }

    /// Create a future that's backed by an associated promise, and return both.
    /// When `set_value` is called on the promise, the value becomes available to
    /// the future.
    pub fn make_promise() -> (Self, Promise<T, E>) {
        let promise = Promise(Arc::new(Mutex::new(PromiseState::Start)));
        let future = Future::NotReady(Promise(promise.0.clone()));
        (future, promise)
    }

    /// Spawn a new future. This currently creates a new thread, which is slow of course.
    pub fn spawn<F: 'static + Send + FnOnce() -> Result<T, E>>(f: F) -> Self {
        let (future, promise) = Self::make_promise();
        let _ = thread::spawn(move || {
            promise.set_result(f());
        });
        future
    }

    /// Call the provided callback, either immediately if the value is available,
    /// or later, when `set_value` is called on the associated promise.
    pub fn with<F: Send + FnOnce(Result<T, E>) + 'static>(self, f: F) {
        match self {
            Future::Ok(value) => f(Ok(value)),
            Future::Err(err) => f(Err(err)),
            Future::NotReady(promise) => {
                let mut state = promise.0.lock().unwrap();
                match mem::replace(&mut *state, PromiseState::Done) {
                    PromiseState::Start => *state = PromiseState::Waiting(Box::new(f)),
                    PromiseState::Ok(value) => f(Ok(value)),
                    PromiseState::Err(err) => f(Err(err)),
                    _ => panic!("double get, can't happen")
                }
            }
        }
    }

    /// Try to get the value of the future, if available. If not, pass the future
    /// through (this method is on `self` so that the value can be moved out).
    pub fn try_get(self) -> Result<Result<T, E>, Self> {
        match self {
            Future::Ok(value) => Ok(Ok(value)),
            Future::Err(value) => Ok(Err(value)),
            Future::NotReady(promise) => {
                {
                    let mut state = promise.0.lock().unwrap();
                    match mem::replace(&mut *state, PromiseState::Done) {
                        PromiseState::Start => (),
                        PromiseState::Ok(value) => return Ok(Ok(value)),
                        PromiseState::Err(err) => return Ok(Err(err)),
                        _ => panic!("double get, can't happen")
                    }
                    *state = PromiseState::Start;
                }
                Err(Future::NotReady(promise))
            }
        }
    }

    /// Get the value of the future. Blocks until the value is available.
    pub fn get(self) -> Result<T, E> {
        match self {
            Future::Ok(value) => Ok(value),
            Future::Err(err) => Err(err),
            Future::NotReady(promise) => {
                loop {
                    let mut state = promise.0.lock().unwrap();
                    match mem::replace(&mut *state, PromiseState::Done) {
                        PromiseState::Start | PromiseState::Parked(_) => (),
                        PromiseState::Ok(value) => return Ok(value),
                        PromiseState::Err(err) => return Err(err),
                        _ => panic!("double get, can't happen")
                    }
                    *state = PromiseState::Parked(thread::current());
                    thread::park();
                }
            }
        }
    }

    pub fn and_then<U, F>(self, f: F) -> Future<U, E>
        where F: Send + 'static + FnOnce(T) -> Result<U, E>,
              U: Send + 'static
    {
        match self.try_get() {
            Ok(Ok(value)) => Future::done(f(value)),
            Ok(Err(err)) => Future::failed(err),
            Err(future) => {
                let (result, promise) = Future::make_promise();
                future.with(|result|
                    match result {
                        Ok(value) => promise.set_result(f(value)),
                        Err(err) => promise.set_err(err),
                    }
                );
                result
            }
        }
    }

    pub fn map<U, F>(self, f: F) -> Future<U, E>
        where F: Send + 'static + FnOnce(T) -> U,
              U: Send + 'static
    {
        self.and_then(|value| Ok(f(value)))
    }
}
