// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Type-safe bindings for Zircon resources.

use crate::{AsHandleRef, Handle, HandleBased, HandleRef};

/// An object representing a Zircon resource.
///
/// As essentially a subtype of `Handle`, it can be freely interconverted.
#[derive(Debug, Eq, PartialEq)]
#[repr(transparent)]
pub struct Resource(Handle);
impl_handle_based!(Resource);
