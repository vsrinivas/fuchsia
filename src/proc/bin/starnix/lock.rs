// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(not(any(test, debug_assertions)))]
pub type Mutex<T> = parking_lot::Mutex<T>;
#[cfg(not(any(test, debug_assertions)))]
pub type MutexGuard<'a, T> = parking_lot::MutexGuard<'a, T>;
#[allow(unused)]
#[cfg(not(any(test, debug_assertions)))]
pub type MappedMutexGuard<'a, T> = parking_lot::MappedMutexGuard<'a, T>;
#[cfg(not(any(test, debug_assertions)))]
pub type RwLock<T> = parking_lot::RwLock<T>;
#[cfg(not(any(test, debug_assertions)))]
pub type RwLockReadGuard<'a, T> = parking_lot::RwLockReadGuard<'a, T>;
#[cfg(not(any(test, debug_assertions)))]
pub type RwLockWriteGuard<'a, T> = parking_lot::RwLockWriteGuard<'a, T>;

#[cfg(any(test, debug_assertions))]
pub type Mutex<T> = tracing_mutex::parkinglot::TracingMutex<T>;
#[cfg(any(test, debug_assertions))]
pub type MutexGuard<'a, T> = tracing_mutex::parkinglot::TracingMutexGuard<'a, T>;
#[allow(unused)]
#[cfg(any(test, debug_assertions))]
pub type MappedMutexGuard<'a, T> = tracing_mutex::parkinglot::TracingMappedMutexGuard<'a, T>;
#[cfg(any(test, debug_assertions))]
pub type RwLock<T> = tracing_mutex::parkinglot::TracingRwLock<T>;
#[cfg(any(test, debug_assertions))]
pub type RwLockReadGuard<'a, T> = tracing_mutex::parkinglot::TracingRwLockReadGuard<'a, T>;
#[cfg(any(test, debug_assertions))]
pub type RwLockWriteGuard<'a, T> = tracing_mutex::parkinglot::TracingRwLockWriteGuard<'a, T>;
