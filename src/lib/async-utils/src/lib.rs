// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(missing_docs)]

//! Provides utilities for working with asynchronous code.

pub use traits::PollExt;

pub mod event;
pub mod fold;
pub mod futures;
pub mod hanging_get;
pub mod stream_epitaph;
pub mod traits;
