// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A construct to confirm that only a limited number of tokens are allowed at the same time.
//! Permits are granted either immediately, or can be retrieved via a future which will resolve
//! to a valid permit once one becomes available.

use {
    anyhow::{format_err, Error},
    futures::{
        channel::oneshot,
        ready,
        task::{Context, Poll},
        Future, FutureExt,
    },
    std::collections::VecDeque,
    std::pin::Pin,
    std::sync::{
        atomic::{AtomicBool, Ordering},
        Arc, Mutex,
    },
};

#[derive(Debug)]
struct PermitsInner {
    // The maximum number of permits allowed.
    limit: usize,
    // The current amount of permits out.
    out: usize,
    // A queue of oneshot senders who are waiting for permits.
    waiting: VecDeque<futures::channel::oneshot::Sender<Permit>>,
}

impl PermitsInner {
    fn new(limit: usize) -> Self {
        Self { limit, out: 0, waiting: VecDeque::new() }
    }

    fn try_get(&mut self) -> Result<(), Error> {
        if self.out < self.limit {
            self.out += 1;
            Ok(())
        } else {
            Err(format_err!("No permits left"))
        }
    }

    fn release(&mut self, inner: Arc<Mutex<Self>>) {
        assert!(self.out > 0, "released a permit when no permits are out");
        while let Some(sender) = self.waiting.pop_front() {
            if let Ok(()) = Permit::handoff(sender, inner.clone()) {
                return;
            }
        }
        // No permits were handed off, so this one gets turned in.
        self.out -= 1;
    }

    fn reservation(inner: Arc<Mutex<Self>>) -> Reservation {
        let (sender, receiver) = oneshot::channel::<Permit>();
        match Permit::try_issue(inner.clone()) {
            Some(permit) => sender.send(permit).expect("just created sender should accept"),
            None => {
                let mut lock = inner.lock().expect("lock poisoned");
                lock.waiting.push_back(sender);
            }
        }
        Reservation(receiver)
    }
}

pub struct Reservation(oneshot::Receiver<Permit>);

impl Future for Reservation {
    type Output = Permit;
    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        let res = ready!(self.0.poll_unpin(cx));
        Poll::Ready(res.expect("sender shouldn't ever be dropped"))
    }
}

#[derive(Debug, Clone)]
pub struct Permits {
    inner: Arc<Mutex<PermitsInner>>,
}

impl Permits {
    pub fn new(limit: usize) -> Self {
        Self { inner: Arc::new(Mutex::new(PermitsInner::new(limit))) }
    }

    /// Attempts to get a permit. Returns None if there are no permits available.
    pub fn get(&self) -> Option<Permit> {
        Permit::try_issue(self.inner.clone())
    }

    /// Returns a future that resolves to a new permit once one becomes available.
    /// reservations are first-come-first-serve.
    pub fn reserve(&self) -> Reservation {
        PermitsInner::reservation(self.inner.clone())
    }
}

#[derive(Debug)]
pub struct Permit {
    inner: Option<Arc<Mutex<PermitsInner>>>,
    committed: Arc<AtomicBool>,
}

impl Permit {
    /// Issues a permit using the given `inner`. Returns none if there are no permits available or
    /// in the case of lock contention.
    fn try_issue(inner: Arc<Mutex<PermitsInner>>) -> Option<Self> {
        match inner.try_lock().map(|mut l| l.try_get().ok()) {
            Ok(Some(())) => {}
            _ => return None,
        };
        Some(Self { inner: Some(inner.clone()), committed: Arc::new(AtomicBool::new(true)) })
    }

    fn handoff(
        sender: oneshot::Sender<Permit>,
        inner: Arc<Mutex<PermitsInner>>,
    ) -> Result<(), Error> {
        let committed = Arc::new(AtomicBool::new(false));
        let commit_clone = committed.clone();
        let potential = Self { inner: Some(inner), committed };
        match sender.send(potential) {
            Ok(()) => {
                commit_clone.store(true, Ordering::Relaxed);
                Ok(())
            }
            Err(_) => Err(format_err!("failed to handoff")),
        }
    }
}

impl Drop for Permit {
    fn drop(&mut self) {
        let inner = match self.inner.take() {
            None => return, // Dropped twice, nothing to do.
            Some(inner) => inner,
        };
        let committed = self.committed.load(Ordering::Relaxed);
        if committed {
            let clone = inner.clone();
            match inner.lock() {
                Ok(mut lock) => lock.release(clone),
                Err(_) => {
                    if !std::thread::panicking() {
                        panic!("permit lock poisoned");
                    }
                }
            };
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use fuchsia_async as fasync;

    #[track_caller]
    fn expect_none<T>(opt: Option<T>, msg: &str) {
        if let Some(_) = opt {
            panic!("{}", msg);
        }
    }

    #[test]
    fn no_permits_available() {
        // Not super useful...
        let permits = Permits::new(0);

        expect_none(permits.get(), "shouldn't be able to get a permit");

        // Can still get a reservation, that will never complete.
        let _reservation = permits.reserve();
    }

    fn expect_reservation_status(
        exec: &mut fasync::TestExecutor,
        reservation: &mut Reservation,
        done: bool,
    ) -> Option<Permit> {
        match exec.run_until_stalled(reservation) {
            Poll::Pending => {
                assert!(!done, "expected reservation to be done");
                None
            }
            Poll::Ready(permit) => {
                assert!(done, "expected reservation to be pending");
                Some(permit)
            }
        }
    }

    #[test]
    fn permit_dropping() {
        // Two permits allowed.
        let permits = Permits::new(2);

        let one = permits.get().expect("first permit");
        let two = permits.get().expect("second permit");
        expect_none(permits.get(), "shouln't get a third permit");

        drop(two);

        let three = permits.get().expect("third permit");
        drop(one);
        let four = permits.get().expect("fourth permit");

        drop(three);
        drop(four);

        let _five = permits.get().expect("fifth permit");
    }

    #[test]
    fn permit_reservations() {
        let mut exec = fasync::TestExecutor::new().expect("executor should start");

        // Two permits allowed.
        let permits = Permits::new(2);

        let one = permits.get().expect("permit one should be available");
        let two = permits.get().expect("second permit is also okay");
        expect_none(permits.get(), "can't get a third item");

        let mut first = permits.reserve();
        let second = permits.reserve();
        let mut third = permits.reserve();
        let mut fourth = permits.reserve();

        // We should be able to drop any of these reservations before they become a Permit
        drop(second);

        expect_reservation_status(&mut exec, &mut first, false);
        expect_reservation_status(&mut exec, &mut third, false);
        expect_reservation_status(&mut exec, &mut fourth, false);

        drop(one);

        let first_out =
            expect_reservation_status(&mut exec, &mut first, true).expect("first resolved");
        expect_reservation_status(&mut exec, &mut third, false);
        expect_reservation_status(&mut exec, &mut fourth, false);

        drop(first_out);

        let _third_out =
            expect_reservation_status(&mut exec, &mut third, true).expect("third resolved");
        expect_reservation_status(&mut exec, &mut fourth, false);

        drop(fourth);

        drop(two);

        // There should be one available now. Let's get it through the reservation.
        let _last_reservation = permits.reserve();

        // Reservations out: third_out and last_reservation

        // Even though we haven't polled the reservation yet, we can't get another one (the reservation took it)
        expect_none(permits.get(), "no items should be available");
    }
}
