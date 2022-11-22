// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Types that are shared across more than one crate in the Fuchsia identity stack.
#![deny(missing_docs)]
#![warn(clippy::all)]
// Using `let () = ...` is often idiomatic in Fuchsia, since it asserts that the
// return type is (), and the compiler will flag if the return type ever changes
// (and needs to be used).
#![allow(clippy::let_unit_value)]

// Async task management.
mod task_group;
// Staged file atomic write support.
mod staged_file;

/// Data associated with authentication enrollment.
#[derive(Debug, PartialEq, Eq)]
pub struct EnrollmentData(pub Vec<u8>);

/// Data associated with an enrollment of an authentication mechanism
/// capable of storage unlock.
#[derive(Debug, PartialEq, Eq)]
pub struct PrekeyMaterial(pub Vec<u8>);

pub use crate::task_group::{cancel_or, TaskGroup, TaskGroupCancel, TaskGroupError};

pub use crate::staged_file::{StagedFile, StagedFileError};
