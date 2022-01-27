// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(missing_docs)]

//! This crate provides utilities for the Fuchsia display-controller API.

/// Custom error definitions for `fuchsia.hardware.display` and sysmem API functions.
mod error;

/// The `types` module defines convenions wrappers for FIDL data types in the
/// `fuchsia.hardware.display` library.
mod types;

/// Stateless representation of a display configuration.
mod config;

/// Helper functions setting up shared image buffers that can be assigned to display layers.
mod image;

/// The `Controller` type is a client-side abstraction for the `fuchsia.hardware.display.Controller`
/// protocol.
mod controller;

/// Rust bindings for the Fuchsia-canonical `zx_pixel_format_t` declared in
/// //zircon/system/public/zircon/pixelformat.h.
mod pixel_format;

pub use config::*;
pub use controller::{Controller, VsyncEvent};
pub use error::*;
pub use image::*;
pub use pixel_format::PixelFormat;
pub use types::*;
