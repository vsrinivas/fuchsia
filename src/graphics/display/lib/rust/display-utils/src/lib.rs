// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(missing_docs)]

//! This crate provides utilities for the Fuchsia display-controller API.

/// The `types` module defines convenions wrappers for FIDL data types in the
/// `fuchsia.hardware.display` library.
pub mod types;

/// The `Controller` type is a client-side abstraction for the `fuchsia.hardware.display.Controller`
/// protocol.
mod controller;
pub use controller::{Controller, VsyncEvent};
