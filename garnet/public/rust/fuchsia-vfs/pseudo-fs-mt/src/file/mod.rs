// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Module holding different kinds of pseudo files and their building blocks.

pub mod asynchronous;

/// Asynchronous is the default and, currently, the only implementation provided for pseudo files.
pub use asynchronous::{read_only, read_write, write_only};

pub mod test_utils;

mod common;
mod connection;
