// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! # Inspect VMO format
//!
//! This module contains utilities for writing the [`Inspect VMO format`][inspect-vmo].
//!
//! [inspect-vmo]: https://fuchsia.dev/fuchsia-src/reference/diagnostics/inspect/vmo-format

pub mod bitfields;
pub mod block;
pub mod block_type;
pub mod constants;
pub mod container;
