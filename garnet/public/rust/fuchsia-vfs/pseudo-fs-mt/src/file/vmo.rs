// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! File nodes backed by a VMO.  These are useful for cases when individual read/write operation
//! actions need to be visible across all the connections to the same file.

pub mod asynchronous;

/// Asynchronous is the default and, currently, the only implementation provided for VMO backed
/// pseudo files.
pub use asynchronous::{read_only, read_write};

pub(self) mod connection;
