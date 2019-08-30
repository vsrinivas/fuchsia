// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! An interface between fuchsia_async and the async-testing testloop.

#![deny(missing_docs)]

mod ffi;
pub use crate::ffi::async_test_subloop_t;
mod subloop;
use crate::subloop::SubloopExecutor;

use futures::Future;

/// Creates a new subloop with an executor running |fut|.
///
/// The returned pointer points to a valid `async_test_subloop_t` and
/// must be used according to this structure's contract.
/// (see `lib/async-testing/test_subloop.h`).
pub fn new_subloop<F>(fut: F) -> *mut async_test_subloop_t
where
    F: Future,
{
    ffi::new_subloop(SubloopExecutor::new(fut))
}
