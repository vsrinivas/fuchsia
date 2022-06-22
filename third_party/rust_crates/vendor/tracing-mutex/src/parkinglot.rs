//! Wrapper types and type aliases for tracing [`parking_lot`] mutexes.
//!
//! This module provides type aliases that use the [`lockapi`][crate::lockapi] module to provide
//! tracing variants of the `parking_lot` primitives. Each of the `TracingX` type aliases wraps an
//! `X` in the `parkint_lot` api with dependency tracking, and a `DebugX` will refer to a `TracingX`
//! when  `debug_assertions` are enabled and to `X` when they're not. This can be used to aid
//! debugging in development while enjoying maximum performance in production.
//!
//! # Usage
//!
//! ```
//! # use std::sync::Arc;
//! # use std::thread;
//! # use lock_api::Mutex;
//! # use tracing_mutex::parkinglot::TracingMutex;
//! let mutex = Arc::new(TracingMutex::new(0));
//!
//! let handles: Vec<_> = (0..10).map(|_| {
//!    let mutex = Arc::clone(&mutex);
//!    thread::spawn(move || *mutex.lock() += 1)
//! }).collect();
//!
//! handles.into_iter().for_each(|handle| handle.join().unwrap());
//!
//! // All threads completed so the value should be 10.
//! assert_eq!(10, *mutex.lock());
//! ```
//!
//! # Limitations
//!
//! The main lock for the global state is still provided by `std::sync` and the tracing primitives
//! are larger than the `parking_lot` primitives they wrap, so there can be a performance
//! degradation between using this and using `parking_lot` directly. If this is of concern to you,
//! try using the `DebugX`-structs, which provide cycle detection only when `debug_assertions` are
//! enabled and have no overhead when they're not.
//!
//! In addition, the mutex guards returned by the tracing wrappers are `!Send`, regardless of
//! whether `parking_lot` is configured to have `Send` mutex guards. This is a limitation of the
//! current bookkeeping system.
use parking_lot::Once;
use parking_lot::OnceState;

use crate::lockapi::TracingWrapper;
use crate::LazyMutexId;

macro_rules! debug_variant {
    ($debug_name:ident, $tracing_name:ident, $normal_name:ty) => {
        type $tracing_name = TracingWrapper<$normal_name>;

        #[cfg(debug_assertions)]
        type $debug_name = TracingWrapper<$normal_name>;
        #[cfg(not(debug_assertions))]
        type $debug_name = $normal_name;
    };
}

debug_variant!(
    DebugRawFairMutex,
    TracingRawFairMutex,
    parking_lot::RawFairMutex
);
debug_variant!(DebugRawMutex, TracingRawMutex, parking_lot::RawMutex);
debug_variant!(DebugRawRwLock, TracingRawRwLock, parking_lot::RawRwLock);

/// Dependency tracking fair mutex. See: [`parking_lot::FairMutex`].
pub type TracingFairMutex<T> = lock_api::Mutex<TracingRawFairMutex, T>;
/// Mutex guard for [`TracingFairMutex`].
pub type TracingFairMutexGuard<'a, T> = lock_api::MutexGuard<'a, TracingRawFairMutex, T>;
/// RAII guard for `TracingFairMutexGuard::map`.
pub type TracingMappedFairMutexGuard<'a, T> =
    lock_api::MappedMutexGuard<'a, TracingRawFairMutex, T>;
/// Debug-only dependency tracking fair mutex.
///
/// If debug assertions are enabled this resolves to [`TracingFairMutex`] and to
/// [`parking_lot::FairMutex`] otherwise.
pub type DebugFairMutex<T> = lock_api::Mutex<DebugRawFairMutex, T>;
/// Mutex guard for [`DebugFairMutex`].
pub type DebugFairMutexGuard<'a, T> = lock_api::MutexGuard<'a, DebugRawFairMutex, T>;
/// RAII guard for `DebugFairMutexGuard::map`.
pub type DebugMappedFairMutexGuard<'a, T> = lock_api::MappedMutexGuard<'a, DebugRawFairMutex, T>;

/// Dependency tracking mutex. See: [`parking_lot::Mutex`].
pub type TracingMutex<T> = lock_api::Mutex<TracingRawMutex, T>;
/// Mutex guard for [`TracingMutex`].
pub type TracingMutexGuard<'a, T> = lock_api::MutexGuard<'a, TracingRawMutex, T>;
/// RAII guard for `TracingMutexGuard::map`.
pub type TracingMappedMutexGuard<'a, T> = lock_api::MappedMutexGuard<'a, TracingRawMutex, T>;
/// Debug-only dependency tracking mutex.
///
/// If debug assertions are enabled this resolves to [`TracingMutex`] and to [`parking_lot::Mutex`]
/// otherwise.
pub type DebugMutex<T> = lock_api::Mutex<DebugRawMutex, T>;
/// Mutex guard for [`DebugMutex`].
pub type DebugMutexGuard<'a, T> = lock_api::MutexGuard<'a, DebugRawMutex, T>;
/// RAII guard for `TracingMutexGuard::map`.
pub type DebugMappedMutexGuard<'a, T> = lock_api::MappedMutexGuard<'a, DebugRawMutex, T>;

/// Dependency tracking reentrant mutex. See: [`parking_lot::ReentrantMutex`].
///
/// **Note:** due to the way dependencies are tracked, this mutex can only be acquired directly
/// after itself. Acquiring any other mutex in between introduces a dependency cycle, and will
/// therefore be rejected.
pub type TracingReentrantMutex<T> =
    lock_api::ReentrantMutex<TracingWrapper<parking_lot::RawMutex>, parking_lot::RawThreadId, T>;
/// Mutex guard for [`TracingReentrantMutex`].
pub type TracingReentrantMutexGuard<'a, T> = lock_api::ReentrantMutexGuard<
    'a,
    TracingWrapper<parking_lot::RawMutex>,
    parking_lot::RawThreadId,
    T,
>;
/// RAII guard for `TracingReentrantMutexGuard::map`.
pub type TracingMappedReentrantMutexGuard<'a, T> =
    lock_api::MappedReentrantMutexGuard<'a, TracingRawMutex, parking_lot::RawThreadId, T>;

/// Debug-only dependency tracking reentrant mutex.
///
/// If debug assertions are enabled this resolves to [`TracingReentrantMutex`] and to
/// [`parking_lot::ReentrantMutex`] otherwise.
pub type DebugReentrantMutex<T> =
    lock_api::ReentrantMutex<DebugRawMutex, parking_lot::RawThreadId, T>;
/// Mutex guard for [`DebugReentrantMutex`].
pub type DebugReentrantMutexGuard<'a, T> =
    lock_api::ReentrantMutexGuard<'a, DebugRawMutex, parking_lot::RawThreadId, T>;
/// RAII guard for `DebugReentrantMutexGuard::map`.
pub type DebugMappedReentrantMutexGuard<'a, T> =
    lock_api::MappedReentrantMutexGuard<'a, DebugRawMutex, parking_lot::RawThreadId, T>;

/// Dependency tracking RwLock. See: [`parking_lot::RwLock`].
pub type TracingRwLock<T> = lock_api::RwLock<TracingRawRwLock, T>;
/// Read guard for [`TracingRwLock`].
pub type TracingRwLockReadGuard<'a, T> = lock_api::RwLockReadGuard<'a, TracingRawRwLock, T>;
/// Upgradable Read guard for [`TracingRwLock`].
pub type TracingRwLockUpgradableReadGuard<'a, T> =
    lock_api::RwLockUpgradableReadGuard<'a, TracingRawRwLock, T>;
/// Write guard for [`TracingRwLock`].
pub type TracingRwLockWriteGuard<'a, T> = lock_api::RwLockWriteGuard<'a, TracingRawRwLock, T>;
/// RAII guard for `TracingRwLockReadGuard::map`.
pub type TracingMappedRwLockReadGuard<'a, T> =
    lock_api::MappedRwLockReadGuard<'a, TracingRawRwLock, T>;
/// RAII guard for `TracingRwLockWriteGuard::map`.
pub type TracingMappedRwLockWriteGuard<'a, T> =
    lock_api::MappedRwLockWriteGuard<'a, TracingRawRwLock, T>;

/// Debug-only dependency tracking RwLock.
///
/// If debug assertions are enabled this resolved to [`TracingRwLock`] and to
/// [`parking_lot::RwLock`] otherwise.
pub type DebugRwLock<T> = lock_api::RwLock<DebugRawRwLock, T>;
/// Read guard for [`TracingRwLock`].
pub type DebugRwLockReadGuard<'a, T> = lock_api::RwLockReadGuard<'a, DebugRawRwLock, T>;
/// Upgradable Read guard for [`TracingRwLock`].
pub type DebugRwLockUpgradableReadGuard<'a, T> =
    lock_api::RwLockUpgradableReadGuard<'a, DebugRawRwLock, T>;
/// Write guard for [`TracingRwLock`].
pub type DebugRwLockWriteGuard<'a, T> = lock_api::RwLockWriteGuard<'a, DebugRawRwLock, T>;
/// RAII guard for `DebugRwLockReadGuard::map`.
pub type DebugMappedRwLockReadGuard<'a, T> = lock_api::MappedRwLockReadGuard<'a, DebugRawRwLock, T>;
/// RAII guard for `DebugRwLockWriteGuard::map`.
pub type DebugMappedRwLockWriteGuard<'a, T> =
    lock_api::MappedRwLockWriteGuard<'a, DebugRawRwLock, T>;

/// A dependency-tracking wrapper for [`parking_lot::Once`].
#[derive(Debug, Default)]
pub struct TracingOnce {
    inner: Once,
    id: LazyMutexId,
}

impl TracingOnce {
    /// Create a new `TracingOnce` value.
    pub const fn new() -> Self {
        Self {
            inner: Once::new(),
            id: LazyMutexId::new(),
        }
    }

    /// Returns the current state of this `Once`.
    pub fn state(&self) -> OnceState {
        self.inner.state()
    }

    ///
    /// This call is considered as "locking this `TracingOnce`" and it participates in dependency
    /// tracking as such.
    ///
    /// # Panics
    ///
    /// This method will panic if `f` panics, poisoning this `Once`. In addition, this function
    /// panics when the lock acquisition order is determined to be inconsistent.
    pub fn call_once(&self, f: impl FnOnce()) {
        let _borrow = self.id.get_borrowed();
        self.inner.call_once(f);
    }

    /// Performs the given initialization routine once and only once.
    ///
    /// This method is identical to [`TracingOnce::call_once`] except it ignores poisoning.
    pub fn call_once_force(&self, f: impl FnOnce(OnceState)) {
        let _borrow = self.id.get_borrowed();
        self.inner.call_once_force(f);
    }
}

/// Debug-only `Once`.
///
/// If debug assertions are enabled this resolves to [`TracingOnce`] and to [`parking_lot::Once`]
/// otherwise.
#[cfg(debug_assertions)]
pub type DebugOnce = TracingOnce;
#[cfg(not(debug_assertions))]
pub type DebugOnce = Once;

#[cfg(test)]
mod tests {
    use std::sync::Arc;
    use std::thread;

    use super::*;

    #[test]
    fn test_mutex_usage() {
        let mutex = Arc::new(TracingMutex::new(()));
        let local_lock = mutex.lock();
        drop(local_lock);

        thread::spawn(move || {
            let _remote_lock = mutex.lock();
        })
        .join()
        .unwrap();
    }

    #[test]
    #[should_panic]
    fn test_mutex_conflict() {
        let mutexes = [
            TracingMutex::new(()),
            TracingMutex::new(()),
            TracingMutex::new(()),
        ];

        for i in 0..3 {
            let _first_lock = mutexes[i].lock();
            let _second_lock = mutexes[(i + 1) % 3].lock();
        }
    }

    #[test]
    fn test_rwlock_usage() {
        let lock = Arc::new(TracingRwLock::new(()));
        let lock2 = Arc::clone(&lock);

        let _read_lock = lock.read();

        // Should be able to acquire lock in the background
        thread::spawn(move || {
            let _read_lock = lock2.read();
        })
        .join()
        .unwrap();
    }

    #[test]
    fn test_rwlock_upgradable_read_usage() {
        let lock = TracingRwLock::new(());

        // Should be able to acquire an upgradable read lock.
        let upgradable_guard: TracingRwLockUpgradableReadGuard<'_, _> = lock.upgradable_read();

        // Should be able to upgrade the guard.
        let _write_guard: TracingRwLockWriteGuard<'_, _> =
            TracingRwLockUpgradableReadGuard::upgrade(upgradable_guard);
    }

    #[test]
    fn test_once_usage() {
        let once = Arc::new(TracingOnce::new());
        let once_clone = once.clone();

        assert!(!once_clone.state().done());

        let handle = thread::spawn(move || {
            assert!(!once_clone.state().done());

            once_clone.call_once(|| {});

            assert!(once_clone.state().done());
        });

        handle.join().unwrap();

        assert!(once.state().done());
    }
}
