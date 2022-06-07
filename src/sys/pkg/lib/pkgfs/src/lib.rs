// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(missing_docs)]

//! Typesafe wrappers around the /pkgfs filesystem.

/// Re-exports of wrapped types from io_util.
pub use io_util::node::OpenError;

pub mod control;
pub mod install;
pub mod needs;
pub mod packages;
pub mod system;
pub mod versions;
