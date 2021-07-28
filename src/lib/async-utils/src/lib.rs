// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(missing_docs)]

//! Provides utilities for working with asynchronous code.

pub use traits::PollExt;

pub mod async_once;
pub mod channel;
pub mod event;
pub mod fold;
pub mod futures;
pub mod hanging_get;
/// Helper to poll a mutex.
pub mod mutex_ticket;
/// Additional Useful Stream Combinators and Utilities
pub mod stream;
pub mod traits;
