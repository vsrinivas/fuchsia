// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::constants::{HAS_WAITERS, IS_LOCKED, SENTINEL};
use crate::list;
use futures::pin_mut;
use list::List;
use parking_lot::Mutex as ParkingLotMutex;
use pin_project::{pin_project, pinned_drop};
use std::cell::UnsafeCell;
use std::future::Future;
use std::pin::Pin;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::task::{Context, Poll, Waker};

/// Determines if a Cutex can be locked by a waiter.
pub trait AcquisitionPredicate<T>: Unpin + Send + Sync {
    /// Verify whether the object under the cutex is in a state where the lock requestor
    /// can proceed. Return true if so.
    /// If false is returned, later waiters are consulted to see if they can proceed, this
    /// waiter retains its position in the queue, and will be re-visited on the next lock release.
    fn can_lock(&self, value: &T) -> bool;

    /// Provide debug output for this predicate. By default we just display a pointer to
    /// us, but other predicates may be able to provide better debugging information.
    fn debug(&self, fmt: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(fmt, "{:p}", self)
    }
}

/// Acquisition predicate that can always lock.
pub(crate) struct AlwaysTrue;
impl<T> AcquisitionPredicate<T> for AlwaysTrue {
    fn can_lock(&self, _: &T) -> bool {
        true
    }

    fn debug(&self, fmt: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        fmt.write_str("true")
    }
}

struct AcquisitionPredicateDebug<'a, T>(&'a dyn AcquisitionPredicate<T>);
impl<'a, T> std::fmt::Debug for AcquisitionPredicateDebug<'a, T> {
    fn fmt(&self, fmt: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        self.0.debug(fmt)
    }
}

/// Instance of AlwaysTrue that's always available.
pub(crate) const ALWAYS_TRUE: AlwaysTrue = AlwaysTrue;

impl<T, F: Unpin + Send + Sync + Fn(&T) -> bool> AcquisitionPredicate<T> for F {
    fn can_lock(&self, value: &T) -> bool {
        (*self)(value)
    }
}

struct Waiter<T> {
    waker: Option<Waker>,
    predicate: *const dyn AcquisitionPredicate<T>,
}

impl<T> std::fmt::Debug for Waiter<T> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("Waiter")
            .field("waker", &self.waker)
            .field("predicate", &AcquisitionPredicateDebug(unsafe { &*self.predicate }))
            .finish()
    }
}

/// Safe since `AcquisitionPredicate` is `Sync`.
unsafe impl<T> Send for Waiter<T> {}

struct CutexState {
    state: AtomicUsize,
}

impl std::fmt::Debug for CutexState {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let state = self.state.load(Ordering::Relaxed);
        f.debug_struct("CutexState")
            .field("is_locked", &(state & IS_LOCKED != 0))
            .field("has_waiters", &(state & HAS_WAITERS != 0))
            .field("granted_to", &(state & SENTINEL))
            .finish()
    }
}

impl CutexState {
    fn new() -> Self {
        Self { state: AtomicUsize::new(SENTINEL) }
    }

    // Try to lock the cutex, return true if successful.
    #[must_use]
    fn try_lock(&self, wait_key: usize) -> bool {
        let old_state = self.state.fetch_or(IS_LOCKED, Ordering::Acquire);
        if old_state & IS_LOCKED == 0 {
            return true;
        }
        if wait_key != SENTINEL && (old_state & SENTINEL) == wait_key {
            // Set the grantee to SENTINEL again, and return locked.
            self.state.fetch_or(SENTINEL, Ordering::Relaxed);
            return true;
        }
        return false;
    }

    // Unlock a previously locked cutex IFF it has no waiters.
    // Returns true if the cutex was unlocked, false if not.
    #[must_use]
    fn unlock_if_no_waiters(&self) -> bool {
        let old_state =
            self.state.compare_and_swap(SENTINEL | IS_LOCKED, SENTINEL, Ordering::AcqRel);
        old_state == SENTINEL | IS_LOCKED
    }

    // Unlock a previously locked cutex.
    fn unlock(&self) {
        self.state.fetch_and(!IS_LOCKED, Ordering::AcqRel);
    }

    // Grants a given waiter the active lock
    fn grant_waiter_lock(&self, wait_key: usize) {
        self.state.store(wait_key | IS_LOCKED | HAS_WAITERS, Ordering::Release)
    }

    // Removing a wait key: if this wait_key was granted the lock but not polled, we need to
    // unlock again.
    #[must_use]
    fn remove_wait_key(&self, wait_key: usize) -> bool {
        if wait_key != SENTINEL {
            let cur_state = self.state.load(Ordering::Acquire);
            if cur_state & SENTINEL == wait_key {
                self.state.fetch_or(SENTINEL, Ordering::Relaxed);
                return true;
            }
        }
        return false;
    }

    // Mark that there are now waiters
    fn mark_waiters(&self) {
        self.state.fetch_or(HAS_WAITERS, Ordering::Relaxed);
    }

    // Mark that there are now no waiters
    fn clear_waiters(&self) {
        self.state.fetch_and(!HAS_WAITERS, Ordering::Relaxed);
    }
}

/// Cutex - a Conditionally-acquired mUTEX
///
/// A cutex:
/// - is fair under high contention
/// - can be conditionally acquired using lock_when
/// - otherwise presents a very similar API to future::lock::Mutex
#[derive(Debug)]
pub struct Cutex<T> {
    value: UnsafeCell<T>,
    state: CutexState,
    waiters: ParkingLotMutex<List<Waiter<T>>>,
}

unsafe impl<T> Send for Cutex<T> {}
unsafe impl<T> Sync for Cutex<T> {}

impl<T> Cutex<T> {
    /// Construct a new Cutex with an initial `value`
    pub fn new(value: T) -> Cutex<T> {
        Cutex {
            value: UnsafeCell::new(value),
            state: CutexState::new(),
            waiters: ParkingLotMutex::new(List::new()),
        }
    }

    /// Try to lock the Cutex without evaluating any predicate.
    fn try_lock(&self, wait_key: usize) -> Option<CutexGuard<'_, T>> {
        if self.state.try_lock(wait_key) {
            Some(CutexGuard { cutex: self })
        } else {
            None
        }
    }

    /// Remove some waiter from the waiter list.
    fn remove(&self, wait_key: &mut usize) {
        if *wait_key == SENTINEL {
            return;
        }
        let mut waiters = self.waiters.lock();
        let (_, is_last) = waiters.remove(*wait_key);
        if is_last {
            self.state.clear_waiters();
        }
        *wait_key = SENTINEL;
    }

    /// Unconditionally lock the Cutex
    pub fn lock(&self) -> CutexLockFuture<'_, '_, T> {
        self.lock_when_pinned(Pin::new(&ALWAYS_TRUE))
    }

    /// Lock the Cutex when `predicate`'s can_lock returns true.
    ///
    /// When the `predicate` returns false the lock request keeps it's position in queue
    /// but other (later) waiters are consulted to see if they can proceed -- so a predicate that
    /// always returns false does not prevent *other* waiters from proceeding.
    pub async fn lock_when<'a, 'b>(
        &'a self,
        predicate: impl 'b + AcquisitionPredicate<T>,
    ) -> CutexGuard<'a, T> {
        pin_mut!(predicate);
        self.lock_when_pinned(predicate).await
    }

    /// Lock the Cutex when predicate's can_lock returns true.
    ///
    /// See `lock_when` for a full description of predicate semantics.
    pub fn lock_when_pinned<'a, 'b>(
        &'a self,
        predicate: Pin<&'b (dyn AcquisitionPredicate<T> + 'b)>,
    ) -> CutexLockFuture<'a, 'b, T> {
        CutexLockFuture { cutex: self, predicate, wait_key: SENTINEL }
    }

    fn unlock(&self) {
        if !self.state.unlock_if_no_waiters() {
            let mut waiters = self.waiters.lock();
            let value = unsafe { &*self.value.get() };
            let completed_iteration = waiters.for_each_until_mut(|id, waiter| {
                if unsafe { &*waiter.predicate }.can_lock(value) {
                    if let Some(w) = waiter.waker.take() {
                        self.state.grant_waiter_lock(id);
                        w.wake();
                        // Short circuit and leave the iteration.
                        return false;
                    }
                }
                return true;
            });
            if completed_iteration {
                self.state.unlock();
            }
        }
    }
}

/// A future that waits for a Cutex to be locked with the given predicate.
/// Once this future completes and returns a CutexGuard, it's behavior if polled
/// is explicitly unstable and may change in the future.
#[pin_project(PinnedDrop)]
pub struct CutexLockFuture<'a, 'b, T> {
    cutex: &'a Cutex<T>,
    predicate: Pin<&'b (dyn AcquisitionPredicate<T> + 'b)>,
    wait_key: usize,
}

#[pinned_drop]
impl<'a, 'b, T> PinnedDrop for CutexLockFuture<'a, 'b, T> {
    fn drop(mut self: Pin<&mut Self>) {
        let unlock_again = self.cutex.state.remove_wait_key(self.wait_key);
        self.cutex.remove(&mut self.wait_key);
        if unlock_again {
            self.cutex.unlock();
        }
    }
}

impl<'a, 'b, T> Future for CutexLockFuture<'a, 'b, T> {
    type Output = CutexGuard<'a, T>;

    fn poll(self: Pin<&mut Self>, ctx: &mut Context<'_>) -> Poll<Self::Output> {
        let this = self.project();
        let cutex = this.cutex;

        if let Some(guard) = cutex.try_lock(*this.wait_key) {
            if this.predicate.can_lock(unsafe { &*cutex.value.get() }) {
                cutex.remove(this.wait_key);
                return Poll::Ready(guard);
            }
        }

        {
            let mut waiters = cutex.waiters.lock();
            if *this.wait_key == SENTINEL {
                let (wait_key, is_first) = waiters.push(Waiter {
                    waker: Some(ctx.waker().clone()),
                    predicate: unsafe {
                        // Transmute to cast away the lifetime of `predicate`.
                        // We guarantee that no access to `predicate` happens after it's destroyed
                        // by removing ourselves from the waiters list during drop.
                        std::mem::transmute(&*this.predicate.as_ref())
                    },
                });
                *this.wait_key = wait_key;
                if is_first {
                    cutex.state.mark_waiters();
                }
            } else {
                waiters.get_mut(*this.wait_key).waker = Some(ctx.waker().clone());
            }
        }

        // Ensure that we haven't raced `CutexGuard::drop`'s unlock path by attempting to acquire
        // the lock again.
        if let Some(guard) = cutex.try_lock(*this.wait_key) {
            if this.predicate.can_lock(unsafe { &*cutex.value.get() }) {
                cutex.remove(this.wait_key);
                return Poll::Ready(guard);
            }
        }

        Poll::Pending
    }
}

/// A locked `Cutex`. When dropped, drops the lock and allows the next waiter in.
#[derive(Debug)]
pub struct CutexGuard<'a, T> {
    cutex: &'a Cutex<T>,
}

impl<'a, T> Drop for CutexGuard<'a, T> {
    fn drop(&mut self) {
        self.cutex.unlock();
    }
}

impl<'a, T> std::ops::Deref for CutexGuard<'a, T> {
    type Target = T;
    fn deref(&self) -> &T {
        unsafe { &*self.cutex.value.get() }
    }
}

impl<'a, T> std::ops::DerefMut for CutexGuard<'a, T> {
    fn deref_mut(&mut self) -> &mut T {
        unsafe { &mut *self.cutex.value.get() }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use futures::executor::block_on;
    use futures::future::join;
    use futures::prelude::*;
    use futures::task::noop_waker_ref;
    use matches::assert_matches;

    #[test]
    fn noop() {
        let _c = Cutex::new(());
    }

    #[test]
    fn can_lock_uncontested() {
        let _ = block_on(Cutex::new(()).lock());
    }

    #[test]
    fn can_lock_contested() {
        block_on(async move {
            let c = &Cutex::new(1u8);
            let g = c.lock().await;
            assert_eq!(*g, 1);
            join(
                async move {
                    *c.lock().await = 2;
                },
                async move {
                    assert_eq!(*g, 1);
                    drop(g);
                },
            )
            .await;
            assert_eq!(*c.lock().await, 2);
        });
    }

    #[test]
    fn can_lock_when() {
        block_on(async move {
            let c = &Cutex::new(1u8);
            join(
                async move {
                    *c.lock_when(|x: &u8| *x == 2).await = 3;
                },
                async move {
                    let mut g = c.lock().await;
                    assert_eq!(*g, 1);
                    *g = 2;
                },
            )
            .await;
            let g = c.lock().await;
            assert_eq!(*g, 3);
        })
    }

    #[test]
    fn can_drop_waiter() {
        block_on(async move {
            let c = &Cutex::new(1u8);
            drop(c.lock());
            c.lock().await;
        })
    }

    #[test]
    fn can_drop_contested_waiter() {
        let c = &Cutex::new(1u8);
        let mut ctx = Context::from_waker(noop_waker_ref());
        let g = c.lock().now_or_never().unwrap();
        let mut f1 = c.lock().boxed();
        let mut f2 = c.lock().boxed();
        assert_matches!(f1.as_mut().poll(&mut ctx), Poll::Pending);
        assert_matches!(f2.as_mut().poll(&mut ctx), Poll::Pending);
        drop(g); // Fairness says give the lock to f1
        drop(f1); // But then abandon it...
        assert_matches!(f2.as_mut().poll(&mut ctx), Poll::Ready(_));
    }

    #[test]
    fn can_drop_contested_waiter2() {
        let c = &Cutex::new(1u8);
        let mut ctx = Context::from_waker(noop_waker_ref());
        let g = c.lock().now_or_never().unwrap();
        let mut f1 = c.lock().boxed();
        let mut f2 = c.lock().boxed();
        assert_matches!(f1.as_mut().poll(&mut ctx), Poll::Pending);
        assert_matches!(f2.as_mut().poll(&mut ctx), Poll::Pending);
        drop(g); // Fairness says give the lock to f1
        drop(f2); // Abandoning the other waiter ought to have no effect
        assert_matches!(f1.as_mut().poll(&mut ctx), Poll::Ready(_));
    }
}
