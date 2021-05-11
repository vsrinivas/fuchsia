// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This module defines errors used in the crate.

use thiserror::Error;

/// Defines errors used in the crate.
/// Enum defines types of errors and their human readable messages.
#[repr(C)]
#[derive(Debug, Copy, Clone, Error, PartialEq)]
pub enum Error {
    /// The interval has invalid range.
    #[error("invalid range")]
    InvalidRange,
}
