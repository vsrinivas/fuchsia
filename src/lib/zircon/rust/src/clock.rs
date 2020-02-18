// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Type-safe bindings for Zircon clock objects.

use crate::{AsHandleRef, Handle, HandleBased, HandleRef};

/// An object representing a Zircon
/// [clock](https://fuchsia.dev/fuchsia-src/concepts/objects/clock.md).
///
/// As essentially a subtype of `Handle`, it can be freely interconverted.
#[derive(Debug, Eq, PartialEq, Ord, PartialOrd, Hash)]
#[repr(transparent)]
pub struct Clock(Handle);
impl_handle_based!(Clock);

// TODO: This is just a stub to enable these handles to be provided over FIDL. We still need to
// implement the rest of the bindings here.
