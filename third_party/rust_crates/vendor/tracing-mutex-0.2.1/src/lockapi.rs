//! Wrapper implementations for [`lock_api`].
//!
//! This module does not provide any particular mutex implementation by itself, but rather can be
//! used to add dependency tracking to mutexes that already exist. It implements all of the traits
//! in `lock_api` based on the one it wraps. Crates such as `spin` and `parking_lot` provide base
//! primitives that can be wrapped.
//!
//! Wrapped mutexes are at least one `usize` larger than the types they wrapped, and must be aligned
//! to `usize` boundaries. As such, libraries with many mutexes may want to consider the additional
//! required memory.
use lock_api::GuardNoSend;
use lock_api::RawMutex;
use lock_api::RawMutexFair;
use lock_api::RawMutexTimed;
use lock_api::RawRwLock;
use lock_api::RawRwLockDowngrade;
use lock_api::RawRwLockFair;
use lock_api::RawRwLockRecursive;
use lock_api::RawRwLockRecursiveTimed;
use lock_api::RawRwLockTimed;
use lock_api::RawRwLockUpgrade;
use lock_api::RawRwLockUpgradeDowngrade;
use lock_api::RawRwLockUpgradeFair;
use lock_api::RawRwLockUpgradeTimed;

use crate::LazyMutexId;

/// Tracing wrapper for all [`lock_api`] traits.
///
/// This wrapper implements any of the locking traits available, given that the wrapped type
/// implements them. As such, this wrapper can be used both for normal mutexes and rwlocks.
#[derive(Debug, Default)]
pub struct TracingWrapper<T> {
    inner: T,
    // Need to use a lazy mutex ID to intialize statically.
    id: LazyMutexId,
}

impl<T> TracingWrapper<T> {
    /// Mark this lock as held in the dependency graph.
    fn mark_held(&self) {
        self.id.mark_held();
    }

    /// Mark this lock as released in the dependency graph.
    ///
    /// # Safety
    ///
    /// This function should only be called when the lock has been previously acquired by this
    /// thread.
    unsafe fn mark_released(&self) {
        self.id.mark_released();
    }

    /// First mark ourselves as held, then call the locking function.
    fn lock(&self, f: impl FnOnce()) {
        self.mark_held();
        f();
    }

    /// First call the unlocking function, then mark ourselves as realeased.
    unsafe fn unlock(&self, f: impl FnOnce()) {
        f();
        self.mark_released();
    }

    /// Conditionally lock the mutex.
    ///
    /// First acquires the lock, then runs the provided function. If that function returns true,
    /// then the lock is kept, otherwise the mutex is immediately marked as relased.
    ///
    /// # Returns
    ///
    /// The value returned from the callback.
    fn conditionally_lock(&self, f: impl FnOnce() -> bool) -> bool {
        // Mark as locked while we try to do the thing
        self.mark_held();

        if f() {
            true
        } else {
            // Safety: we just locked it above.
            unsafe { self.mark_released() }
            false
        }
    }
}

unsafe impl<T> RawMutex for TracingWrapper<T>
where
    T: RawMutex,
{
    const INIT: Self = Self {
        inner: T::INIT,
        id: LazyMutexId::new(),
    };

    /// Always equal to [`GuardNoSend`], as an implementation detail in the tracking system requires
    /// this behaviour. May change in the future to reflect the actual guard type from the wrapped
    /// primitive.
    type GuardMarker = GuardNoSend;

    fn lock(&self) {
        self.lock(|| self.inner.lock());
    }

    fn try_lock(&self) -> bool {
        self.conditionally_lock(|| self.inner.try_lock())
    }

    unsafe fn unlock(&self) {
        self.unlock(|| self.inner.unlock());
    }

    fn is_locked(&self) -> bool {
        // Can't use the default implementation as the inner type might've overwritten it.
        self.inner.is_locked()
    }
}

unsafe impl<T> RawMutexFair for TracingWrapper<T>
where
    T: RawMutexFair,
{
    unsafe fn unlock_fair(&self) {
        self.unlock(|| self.inner.unlock_fair())
    }

    unsafe fn bump(&self) {
        // Bumping effectively doesn't change which locks are held, so we don't need to manage the
        // lock state.
        self.inner.bump();
    }
}

unsafe impl<T> RawMutexTimed for TracingWrapper<T>
where
    T: RawMutexTimed,
{
    type Duration = T::Duration;

    type Instant = T::Instant;

    fn try_lock_for(&self, timeout: Self::Duration) -> bool {
        self.conditionally_lock(|| self.inner.try_lock_for(timeout))
    }

    fn try_lock_until(&self, timeout: Self::Instant) -> bool {
        self.conditionally_lock(|| self.inner.try_lock_until(timeout))
    }
}

unsafe impl<T> RawRwLock for TracingWrapper<T>
where
    T: RawRwLock,
{
    const INIT: Self = Self {
        inner: T::INIT,
        id: LazyMutexId::new(),
    };

    /// Always equal to [`GuardNoSend`], as an implementation detail in the tracking system requires
    /// this behaviour. May change in the future to reflect the actual guard type from the wrapped
    /// primitive.
    type GuardMarker = GuardNoSend;

    fn lock_shared(&self) {
        self.lock(|| self.inner.lock_shared());
    }

    fn try_lock_shared(&self) -> bool {
        self.conditionally_lock(|| self.inner.try_lock_shared())
    }

    unsafe fn unlock_shared(&self) {
        self.unlock(|| self.inner.unlock_shared());
    }

    fn lock_exclusive(&self) {
        self.lock(|| self.inner.lock_exclusive());
    }

    fn try_lock_exclusive(&self) -> bool {
        self.conditionally_lock(|| self.inner.try_lock_exclusive())
    }

    unsafe fn unlock_exclusive(&self) {
        self.unlock(|| self.inner.unlock_exclusive());
    }

    fn is_locked(&self) -> bool {
        self.inner.is_locked()
    }
}

unsafe impl<T> RawRwLockDowngrade for TracingWrapper<T>
where
    T: RawRwLockDowngrade,
{
    unsafe fn downgrade(&self) {
        // Downgrading does not require tracking
        self.inner.downgrade()
    }
}

unsafe impl<T> RawRwLockUpgrade for TracingWrapper<T>
where
    T: RawRwLockUpgrade,
{
    fn lock_upgradable(&self) {
        self.lock(|| self.inner.lock_upgradable());
    }

    fn try_lock_upgradable(&self) -> bool {
        self.conditionally_lock(|| self.inner.try_lock_upgradable())
    }

    unsafe fn unlock_upgradable(&self) {
        self.unlock(|| self.inner.unlock_upgradable());
    }

    unsafe fn upgrade(&self) {
        self.inner.upgrade();
    }

    unsafe fn try_upgrade(&self) -> bool {
        self.inner.try_upgrade()
    }
}

unsafe impl<T> RawRwLockFair for TracingWrapper<T>
where
    T: RawRwLockFair,
{
    unsafe fn unlock_shared_fair(&self) {
        self.unlock(|| self.inner.unlock_shared_fair());
    }

    unsafe fn unlock_exclusive_fair(&self) {
        self.unlock(|| self.inner.unlock_exclusive_fair());
    }

    unsafe fn bump_shared(&self) {
        self.inner.bump_shared();
    }

    unsafe fn bump_exclusive(&self) {
        self.inner.bump_exclusive();
    }
}

unsafe impl<T> RawRwLockRecursive for TracingWrapper<T>
where
    T: RawRwLockRecursive,
{
    fn lock_shared_recursive(&self) {
        self.lock(|| self.inner.lock_shared_recursive());
    }

    fn try_lock_shared_recursive(&self) -> bool {
        self.conditionally_lock(|| self.inner.try_lock_shared_recursive())
    }
}

unsafe impl<T> RawRwLockRecursiveTimed for TracingWrapper<T>
where
    T: RawRwLockRecursiveTimed,
{
    fn try_lock_shared_recursive_for(&self, timeout: Self::Duration) -> bool {
        self.conditionally_lock(|| self.inner.try_lock_shared_recursive_for(timeout))
    }

    fn try_lock_shared_recursive_until(&self, timeout: Self::Instant) -> bool {
        self.conditionally_lock(|| self.inner.try_lock_shared_recursive_until(timeout))
    }
}

unsafe impl<T> RawRwLockTimed for TracingWrapper<T>
where
    T: RawRwLockTimed,
{
    type Duration = T::Duration;

    type Instant = T::Instant;

    fn try_lock_shared_for(&self, timeout: Self::Duration) -> bool {
        self.conditionally_lock(|| self.inner.try_lock_shared_for(timeout))
    }

    fn try_lock_shared_until(&self, timeout: Self::Instant) -> bool {
        self.conditionally_lock(|| self.inner.try_lock_shared_until(timeout))
    }

    fn try_lock_exclusive_for(&self, timeout: Self::Duration) -> bool {
        self.conditionally_lock(|| self.inner.try_lock_exclusive_for(timeout))
    }

    fn try_lock_exclusive_until(&self, timeout: Self::Instant) -> bool {
        self.conditionally_lock(|| self.inner.try_lock_exclusive_until(timeout))
    }
}

unsafe impl<T> RawRwLockUpgradeDowngrade for TracingWrapper<T>
where
    T: RawRwLockUpgradeDowngrade,
{
    unsafe fn downgrade_upgradable(&self) {
        self.inner.downgrade_upgradable()
    }

    unsafe fn downgrade_to_upgradable(&self) {
        self.inner.downgrade_to_upgradable()
    }
}

unsafe impl<T> RawRwLockUpgradeFair for TracingWrapper<T>
where
    T: RawRwLockUpgradeFair,
{
    unsafe fn unlock_upgradable_fair(&self) {
        self.unlock(|| self.inner.unlock_upgradable_fair())
    }

    unsafe fn bump_upgradable(&self) {
        self.inner.bump_upgradable()
    }
}

unsafe impl<T> RawRwLockUpgradeTimed for TracingWrapper<T>
where
    T: RawRwLockUpgradeTimed,
{
    fn try_lock_upgradable_for(&self, timeout: Self::Duration) -> bool {
        self.conditionally_lock(|| self.inner.try_lock_upgradable_for(timeout))
    }

    fn try_lock_upgradable_until(&self, timeout: Self::Instant) -> bool {
        self.conditionally_lock(|| self.inner.try_lock_upgradable_until(timeout))
    }

    unsafe fn try_upgrade_for(&self, timeout: Self::Duration) -> bool {
        self.inner.try_upgrade_for(timeout)
    }

    unsafe fn try_upgrade_until(&self, timeout: Self::Instant) -> bool {
        self.inner.try_upgrade_until(timeout)
    }
}
