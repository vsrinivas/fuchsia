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
pub mod handle;
pub mod server;

mod error;
pub use self::error::{Error, Result};

pub use server::ServeInner;

pub use handle::*;

pub mod epitaph;

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

/// invoke_for_handle_types!{mmm} calls the macro `mmm!` with two arguments: one is the name of a
/// Zircon handle, the second is one of:
///   * Everywhere for handle types that are supported everywhere FIDL is
///   * FuchsiaOnly for handle types that are supported only on Fuchsia
///   * Stub for handle types that have not yet had a Fuchsia API implemented in the zircon crate
///
/// To make a handle available everywhere, a polyfill must be implemented in
/// crate::handle::non_fuchsia_handles.
#[macro_export]
macro_rules! invoke_for_handle_types {
    ($x:ident) => {
        $x! {Process, FuchsiaOnly}
        $x! {Thread, FuchsiaOnly}
        $x! {Vmo, FuchsiaOnly}
        $x! {Channel, Everywhere}
        $x! {Event, FuchsiaOnly}
        $x! {Port, FuchsiaOnly}
        $x! {Interrupt, FuchsiaOnly}
        $x! {PciDevice, Stub}
        $x! {DebugLog, FuchsiaOnly}
        $x! {Socket, Everywhere}
        $x! {Resource, FuchsiaOnly}
        $x! {EventPair, FuchsiaOnly}
        $x! {Job, FuchsiaOnly}
        $x! {Vmar, FuchsiaOnly}
        $x! {Fifo, FuchsiaOnly}
        $x! {Guest, FuchsiaOnly}
        $x! {Vcpu, Stub}
        $x! {Timer, FuchsiaOnly}
        $x! {Iommu, Stub}
        $x! {Bti, Stub}
        $x! {Profile, FuchsiaOnly}
        $x! {Pmt, Stub}
        $x! {SuspendToken, Stub}
        $x! {Pager, Stub}
        $x! {Exception, Stub}
        $x! {Clock, FuchsiaOnly}
        $x! {Stream, FuchsiaOnly}
        $x! {MsiAllocation, Stub}
        $x! {MsiInterrupt, Stub}
    };
}
