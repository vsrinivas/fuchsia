// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Type-safe bindings for Magenta port.

use {HandleBase, Handle, HandleRef};

/// An object representing a Magenta port.
///
/// As essentially a subtype of `Handle`, it can be freely interconverted.
pub struct Port(Handle);

impl HandleBase for Port {
    fn get_ref(&self) -> HandleRef {
        self.0.get_ref()
    }

    fn from_handle(handle: Handle) -> Self {
        Port(handle)
    }
}
