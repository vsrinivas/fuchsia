// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![recursion_limit = "256"]

pub use crate::types::{Error, Result};

/// Generic types
#[macro_use]
mod types;

/// Software Audio Driver
pub mod driver;

/// Audio Driver Stream Commands / Responses
mod stream;

/// Ring Buffer Commands / Responses
mod ring_buffer;
