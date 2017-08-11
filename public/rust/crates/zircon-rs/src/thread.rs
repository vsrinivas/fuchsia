// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Type-safe bindings for Magenta thread.

use {AsHandleRef, HandleBased, Handle, HandleRef};

/// An object representing a Magenta thread.
///
/// As essentially a subtype of `Handle`, it can be freely interconverted.
#[derive(Debug, Eq, PartialEq)]
pub struct Thread(Handle);
impl_handle_based!(Thread);