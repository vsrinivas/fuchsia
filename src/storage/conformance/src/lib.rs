// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(missing_docs)]

//! Crate to provide fidl logging and test setup helpers for conformance tests
//! for io.fidl.

/// Test harness helper struct.
pub mod test_harness;

/// Utility functions for getting combinations of flags.
pub mod flags;
