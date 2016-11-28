// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Type-safe bindings for Magenta event pairs.

use {HandleBase, Handle, HandleRef};

/// An object representing a Magenta
/// [event pair](https://fuchsia.googlesource.com/magenta/+/master/docs/concepts.md#Other-IPC_Events_Event-Pairs_and-User-Signals).
///
/// As essentially a subtype of `Handle`, it can be freely interconverted.
pub struct EventPair(Handle);

impl HandleBase for EventPair {
    fn get_ref(&self) -> HandleRef {
        self.0.get_ref()
    }

    fn from_handle(handle: Handle) -> Self {
        EventPair(handle)
    }
}
