// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Type-safe bindings for Magenta event pairs.

use {HandleBase, Handle, HandleRef, Peered, Status};
use {sys, into_result};

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

impl Peered for EventPair {
}

impl EventPair {
    /// Create an event pair, a pair of objects which can signal each other. Wraps the
    /// [mx_eventpair_create](https://fuchsia.googlesource.com/magenta/+/master/docs/syscalls/eventpair_create.md)
    /// syscall.
    pub fn create(options: EventPairOpts) -> Result<(EventPair, EventPair), Status> {
        let mut out0 = 0;
        let mut out1 = 0;
        let status = unsafe { sys::mx_eventpair_create(options as u32, &mut out0, &mut out1) };
        into_result(status, ||
            (Self::from_handle(Handle(out0)),
                Self::from_handle(Handle(out1))))
    }
}

/// Options for creating an event pair.
#[repr(u32)]
pub enum EventPairOpts {
    /// Default options.
    Default = 0,
}

impl Default for EventPairOpts {
    fn default() -> Self {
        EventPairOpts::Default
    }
}
