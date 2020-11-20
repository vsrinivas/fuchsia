// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Library and runtime for fidl bindings.

#![deny(missing_docs)]
#![allow(elided_lifetimes_in_paths)]

#[macro_use]
pub mod encoding;

pub mod client;
pub mod endpoints;
pub mod epitaph;
pub mod handle;
pub mod server;

mod error;
pub use self::error::{Error, Result};

pub use encoding::UnknownData;
pub use handle::*;
pub use server::ServeInner;

#[cfg(feature = "fidl_trace")]
pub use {
    fuchsia_trace::blob as trace_blob, fuchsia_trace::duration_begin, fuchsia_trace::duration_end,
};

#[cfg(not(feature = "fidl_trace"))]
#[macro_export]
/// No-op implementation of duration_begin, when FIDL tracing is disabled.
macro_rules! duration_begin {
    ($category:expr, $name:expr $(, $key:expr => $val:expr)*) => {};
}

#[cfg(not(feature = "fidl_trace"))]
#[macro_export]
/// No-op implementation of duration_end, when FIDL tracing is disabled.
macro_rules! duration_end {
    ($category:expr, $name:expr $(, $key:expr => $val:expr)*) => {};
}

#[cfg(not(feature = "fidl_trace"))]
#[macro_export]
/// No-op implementation of trace_blob, when FIDL tracing is disabled.
macro_rules! trace_blob {
    ($category:expr, $name:expr, $bytes:expr $(, $key:expr => $val:expr)*) => {};
}

#[cfg(feature = "fidl_trace")]
fn create_trace_provider() {
    fuchsia_trace_provider::trace_provider_create_with_fdio();
}

#[cfg(not(feature = "fidl_trace"))]
fn create_trace_provider() {}
