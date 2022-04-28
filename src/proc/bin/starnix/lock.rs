// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(not(any(testing, debug_assertions)))]
pub type Mutex<T> = parking_lot::Mutex<T>;
#[cfg(not(any(testing, debug_assertions)))]
pub type MutexGuard<'a, T> = parking_lot::MutexGuard<'a, T>;
#[cfg(not(any(testing, debug_assertions)))]
pub type RwLock<T> = parking_lot::RwLock<T>;
#[cfg(not(any(testing, debug_assertions)))]
pub type RwLockReadGuard<'a, T> = parking_lot::RwLockReadGuard<'a, T>;
#[cfg(not(any(testing, debug_assertions)))]
pub type RwLockWriteGuard<'a, T> = parking_lot::RwLockWriteGuard<'a, T>;

// TODO(qsr): tracing-mutex doesn't currently have wrapper for parking_lot mutex. The support exist
// in the repository, but no version has been published yet. When tracing-mutex is upgraded, all
// these types should point to the parking_lot implementation.
#[cfg(any(testing, debug_assertions))]
pub type Mutex<T> = debug_assertions::PoisonFreeTracingMutex<T>;
#[cfg(any(testing, debug_assertions))]
pub type MutexGuard<'a, T> = debug_assertions::PoisonFreeTracingMutexGuard<'a, T>;
#[cfg(any(testing, debug_assertions))]
pub type RwLock<T> = debug_assertions::PoisonFreeTracingRwLock<T>;
#[cfg(any(testing, debug_assertions))]
pub type RwLockReadGuard<'a, T> = tracing_mutex::stdsync::TracingReadGuard<'a, T>;
#[cfg(any(testing, debug_assertions))]
pub type RwLockWriteGuard<'a, T> = tracing_mutex::stdsync::TracingWriteGuard<'a, T>;

#[cfg(any(testing, debug_assertions))]
mod debug_assertions {
    use std::fmt;
    use std::ops;
    use tracing_mutex::stdsync::*;

    #[derive(Debug, Default)]
    pub struct PoisonFreeTracingMutex<T> {
        inner: TracingMutex<T>,
    }

    impl<T> PoisonFreeTracingMutex<T> {
        pub fn new(t: T) -> Self {
            Self { inner: TracingMutex::new(t) }
        }

        pub fn lock(&self) -> PoisonFreeTracingMutexGuard<'_, T> {
            PoisonFreeTracingMutexGuard { inner: self.inner.lock().unwrap() }
        }
    }

    // TODO(qsr): The wrapper around TracingMutexGuard is necessary because Deref implementation
    // for TracingMutexGuard is incorrectly deferencing to a MutexGuard<'a, T> instead of T. When
    // it is fixed, the wrapper can be removed.
    #[derive(Debug)]
    pub struct PoisonFreeTracingMutexGuard<'a, T> {
        inner: TracingMutexGuard<'a, T>,
    }

    impl<'a, T> ops::Deref for PoisonFreeTracingMutexGuard<'a, T> {
        type Target = T;

        fn deref(&self) -> &Self::Target {
            &&self.inner
        }
    }

    impl<'a, T> ops::DerefMut for PoisonFreeTracingMutexGuard<'a, T> {
        fn deref_mut(&mut self) -> &mut Self::Target {
            self.inner.deref_mut()
        }
    }

    impl<'a, T: fmt::Display> fmt::Display for PoisonFreeTracingMutexGuard<'a, T> {
        fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
            self.inner.fmt(f)
        }
    }

    #[derive(Debug, Default)]
    pub struct PoisonFreeTracingRwLock<T> {
        inner: TracingRwLock<T>,
    }

    impl<T> PoisonFreeTracingRwLock<T> {
        pub fn new(t: T) -> Self {
            Self { inner: TracingRwLock::new(t) }
        }

        pub fn read(&self) -> TracingReadGuard<'_, T> {
            self.inner.read().unwrap()
        }

        pub fn write(&self) -> TracingWriteGuard<'_, T> {
            self.inner.write().unwrap()
        }
    }
}
