// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Types that are shared across more than one crate in the Fuchsia identity stack.
#![deny(warnings)]
#![deny(missing_docs)]
#![feature(async_await, await_macro)]

// Async task management.
mod task_group;

/// Reexport task groups.
pub use crate::task_group::{cancel_or, TaskGroup, TaskGroupCancel, TaskGroupError};
