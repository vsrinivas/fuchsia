// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! # Inspect VMO format
//!
//! This library contains utilities for writing the [`Inspect VMO format`][inspect-vmo].
//!
//! [inspect-vmo]: https://fuchsia.dev/fuchsia-src/reference/diagnostics/inspect/vmo-format

mod bitfields;
mod block;
mod block_type;
mod container;
mod error;

pub mod constants;
pub mod utils;

pub use bitfields::*;
pub use block::*;
pub use block_type::*;
pub use container::*;
pub use error::*;
