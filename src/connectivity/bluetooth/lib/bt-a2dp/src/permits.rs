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
    // eliminate the inner and store a ref to ourselves.
    fn reservation(inner: Arc<Mutex<Self>>, revoke_fn: Option<BoxRevokeFn>) -> Reservation {
        let (sender, receiver) = oneshot::channel::<Permit>();
        let mut lock = inner.lock();
        match Permit::try_issue_locked(&mut *lock, inner.clone(), None) {
            Some(permit) => sender.send(permit).expect("just created sender should accept"),
            None => lock.waiting.push_back(sender),
        }
        Reservation { receiver, inner: inner.clone(), revoke_fn }
    }

    /// Make a previously unrevokable permit revokable by supplying a function to revoke it.
    fn make_revokable(&mut self, key: usize, revoke_fn: BoxRevokeFn) {
        let prev = self.out.get_mut(key).expect("reservation should be out").replace(revoke_fn);
        assert!(prev.is_none(), "shouldn't be replacing a previous revocation function");
        self.revocations.push_back(key);
    }

    /// Get the next revokable permit function.
    fn pop_revoke(&mut self) -> Option<BoxRevokeFn> {
        self.revocations
            .pop_front()
            .map(|idx| self.out[idx].take().expect("revokable permits must have a fn"))
    }

    /// Empty the queue of revokable permit functions, returning them all for revocation.
    fn revoke_all(&mut self) -> Vec<BoxRevokeFn> {
        let mut indices = std::mem::take(&mut self.revocations);
        indices
            .drain(..)
            .map(|idx| self.out[idx].take().expect("revokable permits must have a fn"))
            .collect()
    }
}

/// A Reservation is a future that will eventually receive a permit once one becomes available.
pub struct Reservation {
    receiver: oneshot::Receiver<Permit>,
    inner: Arc<Mutex<PermitsInner>>,
    revoke_fn: Option<BoxRevokeFn>,
}

impl Future for Reservation {
    type Output = Permit;
    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        let res = ready!(self.receiver.poll_unpin(cx));
        let permit = res.expect("sender shouldn't ever be dropped");
        if let Some(f) = self.revoke_fn.take() {
            self.inner.lock().make_revokable(permit.key, f);
        }
        Poll::Ready(permit)
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
    /// be revoked to return a permit.  Revoked permits are returned here before reservations are
    /// filled.
    pub fn take(&self) -> Option<Permit> {
        if let Some(permit) = self.get() {
            return Some(permit);
        }
        let revoke_fn = self.inner.lock().pop_revoke();
        revoke_fn.map(|f| f())
    }

    /// Attempts to reserve all permits, including revoking any permits to do so.
    /// Permits that are seized are prioritized over any reservations.
    pub fn seize(&self) -> Vec<Permit> {
        let mut bunch = Vec::new();
        let mut revoke_fns = {
            let mut lock = self.inner.lock();
            loop {
                match Permit::try_issue_locked(&mut *lock, self.inner.clone(), None) {
                    Some(permit) => bunch.push(permit),
                    None => break,
                }
            }
            lock.revoke_all()
        };
        for f in revoke_fns.drain(..) {
            bunch.push(f())
        }
        bunch
    }

    /// Reserve a spot in line to receive a permit once one becomes available.
    /// Returns a future that resolves to a permit
    /// Reservations are first-come-first-serve, but permits that are revoked ignore reservations.
    pub fn reserve(&self) -> Reservation {
        PermitsInner::reservation(self.inner.clone(), None)
    }

    /// Reserve a spot in line to receive a permit once one becomes available.
    /// Returns a future that resovles to a revokable permit.
    /// Once the permit has been returned from the Reservation, it can be revoked at any time
    /// afterwards.
    pub fn reserve_revokable(
        &self,
        revoked_fn: impl FnOnce() -> Permit + 'static + Send,
    ) -> Reservation {
        PermitsInner::reservation(self.inner.clone(), Some(Box::new(revoked_fn)))
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

        let third_out =
            expect_reservation_status(&mut exec, &mut third, true).expect("third resolved");
        expect_reservation_status(&mut exec, &mut fourth, false);

        drop(fourth);

        drop(two);

        // There should be one available now. Let's get it through a reservation.
        let mut fifth = permits.reserve();

        // Permits out: third_out (a permit) and fifth (a reservation which has a permit waiting to be retrieved).

        // Even though we haven't polled the reservation yet, we can't get another one (fifth has it)
        expect_none(permits.get(), "no items should be available");

        // Let's get two last reservations, which won't be filled yet.
        let sixth = permits.reserve();
        let mut seventh = permits.reserve();
        let _eighth = permits.reserve();

        // We can drop the Permits structure at any time, the permits and reservations that are out.
        // will work fine (we just won't be able to make any more)
        drop(permits);

        let _fifth_out =
            expect_reservation_status(&mut exec, &mut fifth, true).expect("fifth retrieved");

        drop(third_out);

        drop(sixth);

        let _seventh_out =
            expect_reservation_status(&mut exec, &mut seventh, true).expect("seventh retrieved");
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

    // It's turtles all the way down.
    // This function revokes a permit by taking it from the permits_holder
    // Then makes a reservation using itself as the revocation function.
    // Putting the reservation into reservations_holder.
    fn revoke_then_reserve_again(
        permits: Permits,
        holder: Arc<Mutex<Vec<Permit>>>,
        reservations: Arc<Mutex<Vec<Reservation>>>,
    ) -> Permit {
        let permit = holder.lock().pop().expect("should have a permit");
        let recurse_fn = {
            let permits = permits.clone();
            let reservations = reservations.clone();
            move || revoke_then_reserve_again(permits, holder, reservations)
        };
        let reservation = permits.reserve_revokable(recurse_fn);
        reservations.lock().push(reservation);
        permit
    }

    #[test]
    fn revokable_reservations() {
        let mut exec = fasync::TestExecutor::new().expect("executor should start");
        const TOTAL_PERMITS: usize = 2;
        let permits = Permits::new(TOTAL_PERMITS);

        let permits_holder = Arc::new(Mutex::new(Vec::new()));
        let reservations_holder = Arc::new(Mutex::new(Vec::new()));

        let revoke_from_holder_fn = {
            let holder = permits_holder.clone();
            move || holder.lock().pop().expect("should have a Permit")
        };

        let revokable =
            permits.get_revokable(revoke_from_holder_fn.clone()).expect("got revokable");
        permits_holder.lock().push(revokable);

        let revoke_then_reserve_fn = {
            let permits = permits.clone();
            let holder = permits_holder.clone();
            let reservations = reservations_holder.clone();
            move || revoke_then_reserve_again(permits, holder, reservations)
        };

        let mut revokable_reservation = permits.reserve_revokable(revoke_then_reserve_fn.clone());
        let revokable_permit =
            expect_reservation_status(&mut exec, &mut revokable_reservation, true).expect("ready");
        permits_holder.lock().push(revokable_permit);

        let seized_permits = permits.seize();
        // We have both permits.
        assert_eq!(TOTAL_PERMITS, seized_permits.len());
        assert_eq!(0, permits_holder.lock().len());

        // But! also a reservation.
        let mut another_reservation = reservations_holder.lock().pop().expect("reservation");
        // This one won't get us another reservation, but is still revokable.
        let mut revokable_reservation_two =
            permits.reserve_revokable(revoke_from_holder_fn.clone());

        // Dropping both seized permits will deliver both reservations.
        drop(seized_permits);

        let revokable_permit =
            expect_reservation_status(&mut exec, &mut another_reservation, true).expect("ready");
        permits_holder.lock().push(revokable_permit);
        let revokable_permit =
            expect_reservation_status(&mut exec, &mut revokable_reservation_two, true)
                .expect("ready");
        permits_holder.lock().push(revokable_permit);

        // We can seize both of these again! Hah!
        let seized_permits = permits.seize();
        // We have both permits.
        assert_eq!(TOTAL_PERMITS, seized_permits.len());
        assert_eq!(0, permits_holder.lock().len());
        // But we have yet another reservation (from the recycling one)
        let mut yet_another = reservations_holder.lock().pop().expect("reservation");
        expect_reservation_status(&mut exec, &mut yet_another, false);

        // If we drop both seized permits..
        drop(seized_permits);

        // We can get one permit (this time unseizable)
        let one = permits.get().expect("one is available");

        // But not a second one, since the reservation has it.
        expect_none(permits.get(), "none should be available");

        let revokable_permit =
            expect_reservation_status(&mut exec, &mut yet_another, true).expect("ready");
        permits_holder.lock().push(revokable_permit);

        // Seizing now will only seize the revokable one.
        let seized_permits = permits.seize();
        assert_eq!(1, seized_permits.len());
        assert_eq!(0, permits_holder.lock().len());

        // We still get another reservation (from the recycling one)
        let mut yet_another = reservations_holder.lock().pop().expect("reservation");
        expect_reservation_status(&mut exec, &mut yet_another, false);

        // Dropping the unrevokable one will fulfill the revokable reservation.
        drop(one);

        let revokable_permit =
            expect_reservation_status(&mut exec, &mut yet_another, true).expect("ready");
        permits_holder.lock().push(revokable_permit);

        // And we can take that one away too.
        let _taken_permit = permits.take().expect("should be able to take one");
    }
}
