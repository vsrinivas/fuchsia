// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Useful synchronization primitives.

// TODO(https://fxbug.dev/110884): Support single-threaded variants of types
// exported from this module.

#[cfg(feature = "instrumented")]
pub use netstack3_sync_instrumented::{Mutex, RwLock};

// Don't perform recursive lock checks when benchmarking so that the benchmark
// results are not affected by the extra bookkeeping.
#[cfg(not(feature = "instrumented"))]
pub use netstack3_sync_not_instrumented::{Mutex, RwLock};

pub use alloc::sync::{Arc as ReferenceCounted, Weak as WeakReferenceCounted};
