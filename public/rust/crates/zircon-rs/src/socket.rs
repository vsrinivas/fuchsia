// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Type-safe bindings for Magenta sockets.

use {HandleBase, Handle, HandleRef};

/// An object representing a Magenta
/// [socket](https://fuchsia.googlesource.com/magenta/+/master/docs/concepts.md#Message-Passing_Sockets-and-Channels).
///
/// As essentially a subtype of `Handle`, it can be freely interconverted.
pub struct Socket(Handle);

impl HandleBase for Socket {
    fn get_ref(&self) -> HandleRef {
        self.0.get_ref()
    }

    fn from_handle(handle: Handle) -> Self {
        Socket(handle)
    }
}
