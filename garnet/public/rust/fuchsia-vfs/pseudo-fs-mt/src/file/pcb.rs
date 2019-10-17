// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! File nodes with per-connection buffers.  These are mostly useful for read-only files that
//! produce their content "dynamically" or for writable files with "atomic" content, where
//! conflicts are resolved by just overwriting the whole content of the file with updated state.

pub mod asynchronous;

/// Asynchronous is the default and, currently, the only implementation provided for pseudo files.
pub use asynchronous::{read_only, read_write, write_only};

mod connection;
