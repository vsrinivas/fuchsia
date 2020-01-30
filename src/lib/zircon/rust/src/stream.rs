// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Type-safe bindings for Zircon stream objects.

use crate::{AsHandleRef, Handle, HandleBased, HandleRef};

/// An object representing a Zircon [stream](https://fuchsia.dev/fuchsia-src/concepts/objects/stream.md).
///
/// As essentially a subtype of `Handle`, it can be freely interconverted.
#[derive(Debug, Eq, PartialEq, Ord, PartialOrd, Hash)]
#[repr(transparent)]
pub struct Stream(Handle);
impl_handle_based!(Stream);

impl Stream {
    // TODO: Add operations.
}
