// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use futures::{Poll, FutureExt};
use futures::future::FutureObj;
use futures::task;
use std::cell::UnsafeCell;
use std::sync::atomic::AtomicUsize;
use std::sync::atomic::Ordering::{AcqRel, Relaxed};

/// A lock-free thread-safe future.
pub struct AtomicFuture {
    // ACTIVE, INACTIVE, NOTIFIED, or DONE
    state: AtomicUsize,
    // `future` is safe to access only after a successful
    // compare-and-swap from INACTIVE to ACTIVE, and before
    // a transition from ACTIVE or NOTIFIED to INACTIVE or DONE.
    // INVARIANT: this value must be `Some(...)` so long as
    // `state` != `DONE`.
    future: UnsafeCell<Option<FutureObj<'static, ()>>>,
}

/// `AtomicFuture` is safe to access from multiple threads at once.
/// Its only method is `try_poll`, which itself is thread-safe.
/// (See comments on method implementation for details)
unsafe impl Sync for AtomicFuture {}
trait AssertSend: Send {}
impl AssertSend for AtomicFuture {}

/// No thread is currently polling the future.
const INACTIVE: usize = 0;

/// A thread is currently polling the future.
const ACTIVE: usize = 1;

/// A thread is currently polling the future, and will poll again once it completes.
const NOTIFIED: usize = 2;

/// The future has been completed and should not be polled again.
const DONE: usize = 3;

// Valid transitions are:
//
// INACTIVE => ACTIVE: a thread took control of the future
// ACTIVE => INACTIVE: a thread released control of the future
// ACTIVE => NOTIFIED: a thread is currently in control of the future
//                     and has been notified to poll again
// ACTIVE => DONE: the future has been completed
// NOTIFIED => ACTIVE: a thread saw "NOTIFIED" and is going to poll again
// NOTIFIED => DONE: the future has been completed

/// The result of a call to `try_poll`.
/// This indicates the result of attempting to `poll` the future.
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub enum AttemptPollResult {
    /// The future was being polled by another thread, but it was notified
    /// to poll at least once more before yielding.
    Busy,
    /// The future was polled, but did not complete.
    Pending,
    /// The future was polled and finished by this thread.
    /// This result is normally used to trigger garbage-collection of the future.
    IFinished,
    /// The future was already completed by another thread.
    SomeoneElseFinished,
}

impl AtomicFuture {
    /// Create a new `AtomicFuture`.
    pub fn new(
        future: FutureObj<'static, ()>,
    ) -> Self {
        AtomicFuture {
            state: AtomicUsize::new(INACTIVE),
            future: UnsafeCell::new(Some(future)),
        }
    }

    /// Attempt to poll the underlying future.
    ///
    /// `try_poll` ensures that the future is polled at least once more
    /// unless it has already finished.
    pub fn try_poll(&self, cx: &mut task::Context) -> AttemptPollResult
    {
        // AcqRel is used instead of SeqCst in the following code.
        //
        // AcqRel behaves like Acquire on loads and Release on stores.
        // This means that a successful compare-and-swap will observe reads and
        // writes from other threads that happened before their successful
        // compare and swap operations.
        //
        // Prior to reading *or* mutating `self.future`, we must have
        // performed a successful transition from INACTIVE to ACTIVE.
        //
        // After any mutation to `self.future`, no other threads will read
        // `self.future` until the following two operations have occured:
        //
        // - The mutating thread has transitioned from ACTIVE to (INACTIVE or DONE)
        // - The new reader has transitioned from INACTIVE to ACTIVE.
        //
        // This guarantees that any writes written by the mutating thread will
        // be observable by the reading thread.
        loop {
            // Attempt to acquire sole responsibility for polling the future
            match self.state.compare_and_swap(INACTIVE, ACTIVE, AcqRel) {
                INACTIVE => {
                    // we are now the (only) active worker. proceed to poll!
                    loop {
                        let poll_res = {
                            // This `UnsafeCell` access is valid because `self.future.get()`
                            // is only called here, inside the critical section where
                            // we performed the transition from INACTIVE to ACTIVE.
                            let opt: &mut Option<FutureObj<'static, ()>> =
                                unsafe { &mut *self.future.get() };

                            // We know that the future is still there and hasn't completed
                            // because `state` != `DONE`
                            let future = opt.as_mut().expect("Missing future in AtomicFuture");
                            future.poll_unpin(cx)
                        };

                        match poll_res {
                            Poll::Ready(()) => {
                                // Take the future so that its innards can be dropped
                                let future_opt: &mut Option<FutureObj<'static, ()>> =
                                    unsafe { &mut *self.future.get() };
                                future_opt.take();

                                // No one else will read `future` unless they see
                                // `INACTIVE`, which will never happen again.
                                self.state.store(DONE, Relaxed);
                                return AttemptPollResult::IFinished;
                            }
                            Poll::Pending => {
                                // Continue on
                            }
                        }

                        match self.state.compare_and_swap(ACTIVE, INACTIVE, AcqRel) {
                            ACTIVE => {
                                return AttemptPollResult::Pending;
                            }
                            NOTIFIED => {
                                // We were notified to poll again while we were busy.
                                // We are still the sole owner of the memory in `future`,
                                // so we don't need any specific memory ordering guarantees.
                                self.state.store(ACTIVE, Relaxed);
                                continue;
                            }
                            INACTIVE | DONE => {
                                panic!("Invalid data contention in AtomicFuture");
                            }
                            _ => {
                                panic!("Unexpected AtomicFuture state");
                            }
                        }
                    }
                }
                ACTIVE => {
                    // Someone else was already working on this.
                    // notify them to make sure they poll at least one more time
                    //
                    // We're not acquiring access to memory or releasing any writes,
                    // so we can use `Relaxed` memory ordering.
                    match self.state.compare_and_swap(ACTIVE, NOTIFIED, Relaxed) {
                        INACTIVE => {
                            // Ooh, the worker finished before we could notify.
                            // Let's start over and try and become the new worker.
                            continue
                        }
                        ACTIVE | NOTIFIED => {
                            // Either we CAS'd to NOTIFIED or someone else did.
                            //
                            // Since this is a relaxed read, you might wonder how we know
                            // that our read of NOTIFIED isn't stale.
                            //
                            // Note that the event which triggered the call to `try_poll` must
                            // have occurred prior to the initial (failed) CAS from INACTIVE to
                            // ACTIVE. That event itself must have introduced a memory fence in
                            // order to ensure visibility on successive calls to `try_poll` on
                            // another thread. Common events include syscalls and mutex writes.
                            //
                            // Since this CAS cannot be reordered with the initial CAS of
                            // the same variable, we have following definite order:
                            //
                            // event (syscall, mutex write, etc.) happens before
                            // read of ACTIVE happens before
                            // read of NOTIFIED
                            //
                            // This means `state` was definitely NOTIFIED at some point
                            // after the initial EVENT.
                            return AttemptPollResult::Busy
                        }
                        DONE => {
                            // The worker completed this future already
                            return AttemptPollResult::SomeoneElseFinished
                        }
                        _ => {
                            panic!("Unexpected AtomicFuture state");
                        }
                    }
                }
                NOTIFIED => {
                    // The worker is already going to poll at least one more time.
                    return AttemptPollResult::Busy
                }
                DONE => {
                    // Someone else completed this future already
                    return AttemptPollResult::SomeoneElseFinished
                }
                _ => {
                    panic!("Unexpected AtomicFuture state");
                }
            }
        }
    }
}
