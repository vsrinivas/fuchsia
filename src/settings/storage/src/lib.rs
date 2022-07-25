// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(clippy::all)]

/// Allows controllers to store state in persistent device level storage.
pub mod device_storage;

/// Storage that interacts with persistent fidl.
pub mod fidl_storage;

/// Implements recording stash write failures to inspect.
pub mod stash_logger;

/// Exposes a factory struct and trait to generate storage.
pub mod storage_factory;

#[derive(PartialEq, Eq, Clone, Debug)]
/// Enum for describing whether writing affected persistent value.
pub enum UpdateState {
    Unchanged,
    Updated,
}
