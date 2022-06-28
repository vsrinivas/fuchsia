// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This mod provides utilities for simplifying the collection of trace events.

/// This macro simplifies collecting async trace events. It uses "setui" as the category name.
#[macro_export]
macro_rules! trace {
    ($nonce:expr, $name:expr $(, $key:expr => $val:expr)* $(,)?) => {
        let _guard = ::fuchsia_trace::async_enter!($nonce, "setui", $name $(, $key => $val)*);
    }
}

/// This macro simplifies collecting async trace events. It returns a guard that can be used to
/// control the scope of the tracing event. It uses "setui" as the category name.
#[macro_export]
macro_rules! trace_guard {
    ($nonce:expr, $name:expr $(, $key:expr => $val:expr)* $(,)?) => {
        ::fuchsia_trace::async_enter!($nonce, "setui", $name $(, $key => $val)*)
    }
}
