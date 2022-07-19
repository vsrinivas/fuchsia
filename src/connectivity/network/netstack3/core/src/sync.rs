// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Useful synchronization primitives.

pub(crate) type MutexGuard<'a, T> = std::sync::MutexGuard<'a, T>;

/// A mutual exclusion primitive useful for protecting shared data
///
/// A [`std::sync::Mutex`] assuming lock poisoning will never occur.
#[derive(Default)]
pub(crate) struct Mutex<T>(std::sync::Mutex<T>);

impl<T> Mutex<T> {
    pub(crate) fn new(t: T) -> Mutex<T> {
        Mutex(std::sync::Mutex::new(t))
    }

    pub(crate) fn lock(&self) -> MutexGuard<'_, T> {
        let Self(m) = self;
        m.lock().expect("unexpectedly poisoned")
    }
}

pub(crate) type RwLockReadGuard<'a, T> = std::sync::RwLockReadGuard<'a, T>;
pub(crate) type RwLockWriteGuard<'a, T> = std::sync::RwLockWriteGuard<'a, T>;

/// A reader-writer lock
///
/// A [`std::sync::RwLock`] assuming lock poisoning will never occur.
#[derive(Default)]
pub(crate) struct RwLock<T>(std::sync::RwLock<T>);

impl<T> RwLock<T> {
    pub(crate) fn read(&self) -> RwLockReadGuard<'_, T> {
        let Self(rw) = self;
        rw.read().expect("unexpectedly poisoned")
    }

    pub(crate) fn write(&self) -> RwLockWriteGuard<'_, T> {
        let Self(rw) = self;
        rw.write().expect("unexpectedly poisoned")
    }
}
