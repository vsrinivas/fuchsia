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
    parking_lot::Mutex,
    slab::Slab,
    std::collections::VecDeque,
    std::pin::Pin,
    std::sync::{
        atomic::{AtomicBool, Ordering},
        Arc,
    },
};

type BoxRevokeFn = Box<dyn FnOnce() -> Permit + Send>;

struct PermitsInner {
    // The maximum number of permits allowed.
    limit: usize,
    // The current permits out. Permits are indexed by their key.
    out: Slab<Option<BoxRevokeFn>>,
    // A queue of oneshot senders who are waiting for permits.
    waiting: VecDeque<futures::channel::oneshot::Sender<Permit>>,
    // An ordered queue of indexes into `out` which are revokable.
    revocations: VecDeque<usize>,
}

impl std::fmt::Debug for PermitsInner {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let letter_outs: Vec<(usize, &str)> =
            self.out.iter().map(|(k, r)| (k, if r.is_some() { "R" } else { "I" })).collect();
        f.debug_struct("PermitsInner")
            .field("limit", &self.limit)
            .field("waiting", &self.waiting)
            .field("out", &letter_outs)
            .finish()
    }
}

impl PermitsInner {
    fn new(limit: usize) -> Self {
        Self {
            limit,
            out: Slab::with_capacity(limit),
            waiting: VecDeque::new(),
            revocations: VecDeque::new(),
        }
    }

    // Try to reserve a key in the permits. If `revoke_fn` is Some then this permit is
    // revokable. Returns the key associated with the permit.
    fn try_get(&mut self, revoke_fn: Option<BoxRevokeFn>) -> Result<usize, Error> {
        if self.out.len() == self.out.capacity() {
            return Err(format_err!("No permits left"));
        }
        let idx = self.out.insert(revoke_fn);
        if self.out[idx].is_some() {
            self.revocations.push_back(idx);
        }
        Ok(idx)
    }

    // Release a permit that is out. Permits call this function when they are dropped
    // to hand off their permit to the next waiting reservation.
    // `inner` is a pointer to the shared mutex of this.
    // `key` is the key of the permit being released.
    fn release(&mut self, inner: Arc<Mutex<Self>>, key: usize) {
        assert!(self.out.contains(key), "released a permit that is not out");
        while let Some(sender) = self.waiting.pop_front() {
            if let Ok(()) = Permit::handoff(sender, inner.clone(), key) {
                return;
            }
        }
        // No permits were handed off, so this one gets turned in.
        drop(self.out.remove(key));
        self.revocations.retain(|k| *k != key)
    }

    // Create a Reservation future that will complete with a Permit when one becomes available.
    // `inner` should be a pointer to this.
    // TODO(https://github.com/rust-lang/rust/issues/75861): When new_cyclic is stable, we can
    // eliminate the inner and use self.
    fn reservation(inner: Arc<Mutex<Self>>) -> Reservation {
        let (sender, receiver) = oneshot::channel::<Permit>();
        match Permit::try_issue(inner.clone(), None) {
            Some(permit) => sender.send(permit).expect("just created sender should accept"),
            None => {
                let mut lock = inner.lock();
                lock.waiting.push_back(sender);
            }
        }
        Reservation(receiver)
    }

    /// Revoke one permit, and return it, if possible.
    fn revoke(&mut self) -> Option<Permit> {
        self.revocations.pop_front().map(|idx| {
            let revoke_fn = self.out[idx].take().expect("revokable permits must have a fn");
            revoke_fn()
        })
    }

    // Revoke all permits that can be revoked, and return them.
    fn revoke_all(&mut self) -> Vec<Permit> {
        let mut indices = std::mem::take(&mut self.revocations);
        indices
            .drain(..)
            .map(|idx| {
                let revoke_fn = self.out[idx].take().expect("revokable permits must have a fn");
                revoke_fn()
            })
            .collect()
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
    limit: usize,
}

impl Permits {
    /// Make a new set of permits with `limit` maximum concurrent permits available.
    pub fn new(limit: usize) -> Self {
        Self { inner: Arc::new(Mutex::new(PermitsInner::new(limit))), limit }
    }

    /// Returns the maximum number of permits allowed.
    pub fn limit(&self) -> usize {
        self.limit
    }

    /// Attempts to get a permit. Returns None if there are no permits available.
    pub fn get(&self) -> Option<Permit> {
        Permit::try_issue(self.inner.clone(), None)
    }

    /// Attempts to get a permit that is revokable.  Revokable permits can be revoked at any time
    /// after this function returns and must return a Permit when asked. `revoked_fn` will be
    /// called to retrieve the permit when it is revoked.
    /// A reservation is provided to the function which can be waited on to retrieve a permit
    /// as soon as it is available.
    pub fn get_revokable(
        &self,
        revoked_fn: impl FnOnce() -> Permit + 'static + Send,
    ) -> Option<Permit> {
        Permit::try_issue(self.inner.clone(), Some(Box::new(revoked_fn)))
    }

    /// Attempts to get a permit. If a permit isn't available, but one can be revoked, one will
    /// be revoked to satisfy returning a permit.
    pub fn take(&self) -> Option<Permit> {
        if let Some(permit) = self.get() {
            return Some(permit);
        }
        self.inner.lock().revoke()
    }

    /// Attempts to reserve all permits that are available, revoking permits if necessary to do so.
    /// Permits that are seized are prioritized over any reservations.
    pub fn seize(&self) -> Vec<Permit> {
        let mut lock = self.inner.lock();
        let mut bunch = lock.revoke_all();
        loop {
            match Permit::try_issue_locked(&mut *lock, self.inner.clone(), None) {
                Some(permit) => bunch.push(permit),
                None => break,
            }
        }
        bunch
    }

    /// returns a future that resolves to a new permit once one becomes available.
    /// reservations are first-come-first-serve.
    pub fn reserve(&self) -> Reservation {
        PermitsInner::reservation(self.inner.clone())
    }
}

#[derive(Debug)]
pub struct Permit {
    inner: Option<Arc<Mutex<PermitsInner>>>,
    committed: Arc<AtomicBool>,
    key: usize,
}

impl Permit {
    /// Issues a permit using the given `inner`. Returns none if there are no permits available or
    /// in the case of lock contention.
    fn try_issue(inner: Arc<Mutex<PermitsInner>>, revoke_fn: Option<BoxRevokeFn>) -> Option<Self> {
        let mut lock = inner.lock();
        Self::try_issue_locked(&mut *lock, inner.clone(), revoke_fn)
    }

    // Tries to issue a permit given a PermitsInner.
    // Used to issue permits in succession without unlocking the PermitsInner.
    fn try_issue_locked(
        inner: &mut PermitsInner,
        ptr: Arc<Mutex<PermitsInner>>,
        revoke_fn: Option<BoxRevokeFn>,
    ) -> Option<Self> {
        inner.try_get(revoke_fn).ok().map(|key| Self {
            inner: Some(ptr),
            committed: Arc::new(AtomicBool::new(true)),
            key,
        })
    }

    // Tries to hand off a permit through a sender. This creates a new Permit and makes sure it
    // is in the channel before it returns. The permit is then "real", and guarantees it will
    // release itself when dropped.
    fn handoff(
        sender: oneshot::Sender<Permit>,
        inner: Arc<Mutex<PermitsInner>>,
        key: usize,
    ) -> Result<(), Error> {
        let committed = Arc::new(AtomicBool::new(false));
        let commit_clone = committed.clone();
        let potential = Self { inner: Some(inner), committed, key };
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
            inner.lock().release(clone, self.key);
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

        assert_eq!(2, permits.limit());

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

    #[test]
    fn revoke_permits() {
        const TOTAL_PERMITS: usize = 2;
        let permits = Permits::new(TOTAL_PERMITS);

        let permit_holder = Arc::new(Mutex::new(None));

        let revoke_from_holder_fn = {
            let holder = permit_holder.clone();
            move || holder.lock().take().expect("should be holding Permit")
        };

        let revokable_permit =
            permits.get_revokable(revoke_from_holder_fn.clone()).expect("permit available");
        permit_holder.lock().replace(revokable_permit);

        let seized_permits = permits.seize();

        // We have two permits.
        assert_eq!(TOTAL_PERMITS, seized_permits.len());
        // The permit has been revoked
        assert!(permit_holder.lock().is_none());

        // Drop the permits
        drop(seized_permits);

        // Should be able to take a permit when one is just available.
        let _nonrevokable_permit = permits.take().expect("permit available");
        let revokable_permit =
            permits.get_revokable(revoke_from_holder_fn.clone()).expect("two permits");
        permit_holder.lock().replace(revokable_permit);

        // Seizing all the (remaining) permits doesn't get the non-revokable one.
        let seized_permits = permits.seize();
        assert_eq!(1, seized_permits.len());
        // The permit has been revoked
        assert!(permit_holder.lock().is_none());

        drop(seized_permits);

        let revokable_permit = permits.get_revokable(revoke_from_holder_fn).expect("permit");
        permit_holder.lock().replace(revokable_permit);

        // Can take the permit from the revokable one.
        let _taken = permits.take().expect("can take the permit");
        assert!(permit_holder.lock().is_none());

        // Can't take a permit if none are available.
        assert!(permits.take().is_none());
    }

    #[test]
    fn revokable_dropped_before_revokation() {
        const TOTAL_PERMITS: usize = 2;
        let permits = Permits::new(TOTAL_PERMITS);

        let permit_holder = Arc::new(Mutex::new(None));

        let revoke_from_holder_fn = {
            let holder = permit_holder.clone();
            move || holder.lock().take().expect("should be holding Permit when revoked")
        };

        let revokable_permit =
            permits.get_revokable(revoke_from_holder_fn).expect("permit available");
        // Drop it before we have a chance to revoke it.
        drop(revokable_permit);

        let seized_permits = permits.seize();
        // We have both permits.
        assert_eq!(TOTAL_PERMITS, seized_permits.len());
    }
}
