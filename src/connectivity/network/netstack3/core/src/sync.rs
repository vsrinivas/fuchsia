// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Useful synchronization primitives.

#[cfg(not(benchmark))]
pub(crate) use netstack3_sync_instrumented::{Mutex, RwLock};

// Don't perform recursive lock checks when benchmarking so that the benchmark
// results are not affected by the extra bookkeeping.
#[cfg(benchmark)]
pub(crate) use netstack3_sync_not_instrumented::{Mutex, RwLock};
