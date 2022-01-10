// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::task::Context;

/// Trait for OpenThread platform implementations.
pub trait Platform {
    /// Asynchronously process platform implementation tasks.
    ///
    /// This method is unsafe because it MUST ONLY be called from the
    /// same thread that the OpenThread instance is being used on.
    ///
    /// You should never need to call this directly.
    unsafe fn process_poll(self: &mut Self, instance: &crate::ot::Instance, cx: &mut Context<'_>);
}

/// Platform instance which does nothing.
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub struct NullPlatform;

impl Platform for NullPlatform {
    unsafe fn process_poll(
        self: &mut Self,
        _instance: &crate::ot::Instance,
        _cx: &mut Context<'_>,
    ) {
    }
}
