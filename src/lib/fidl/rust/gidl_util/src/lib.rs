// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This crate contains utility functions used in GIDL tests and benchmarks.

use {
    fidl::{AsHandleRef, Handle, HandleBased},
    fuchsia_zircon_status::Status,
};

/// Unsafely copies `handle`, i.e. makes a new `Handle` object with the same raw
/// value, and converts it to handle subtype `T`.
pub unsafe fn copy_handle<T: HandleBased>(handle: &Handle) -> T {
    T::from_handle(Handle::from_raw(handle.raw_handle()))
}

/// Makes unsafe copies of handles at the given indices, i.e. new `Handle`
/// objects with the same raw values. Callers should use `disown_handles` on
/// one of the lists to prevent double-close errors when handles are dropped.
pub unsafe fn copy_handles_at(handles: &[Handle], indices: &[usize]) -> Vec<Handle> {
    let mut copy = Vec::with_capacity(indices.len());
    for &i in indices {
        copy.push(Handle::from_raw(handles[i].raw_handle()));
    }
    copy
}

/// Wraps `handles` in a structure that prevents them from being closed.
///
/// To use the contained vector after, use this pattern:
///
///     let handles = unsafe { disown_handles(...) };
///     let handles = handles.as_ref(); // or as_mut
///
/// The seperate let-bindings are necessary for the `Disowned` value to outlive
/// the reference obtained from it.
pub unsafe fn disown_handles(handles: Vec<Handle>) -> Disowned {
    Disowned(handles)
}

pub struct Disowned(Vec<Handle>);

impl AsRef<Vec<Handle>> for Disowned {
    fn as_ref(&self) -> &Vec<Handle> {
        &self.0
    }
}

impl AsMut<Vec<Handle>> for Disowned {
    fn as_mut(&mut self) -> &mut Vec<Handle> {
        &mut self.0
    }
}

impl Drop for Disowned {
    fn drop(&mut self) {
        for h in self.0.drain(..) {
            // Handles are just u32 wrappers, so this doesn't leak memory.
            std::mem::forget(h);
        }
    }
}

// See src/lib/fidl/rust/fidl/src/mod.rs for handle subtypes. The ones marked
// "Everywhere" are fully emulated on non-Fuchsia, so we can define create_*
// functions that work on all platforms. The ones marked "FuchsiaOnly" are not
// emulated, so we have to define create_* differently based on target_os (on
// non-Fuchsia, they simply return an invalid handle).

pub fn create_channel() -> Result<fidl::Handle, Status> {
    let (channel, _) = fidl::Channel::create()?;
    Ok(channel.into_handle())
}

#[cfg(target_os = "fuchsia")]
pub use fuchsia_impl::*;

#[cfg(target_os = "fuchsia")]
mod fuchsia_impl {
    use super::*;
    use fuchsia_zircon_sys as sys;

    /// Returns the result of the `zx_object_get_info` syscall with topic
    /// `ZX_INFO_HANDLE_VALID`. In particular, returns `Status::BAD_HANDLE` if
    /// the handle is dangling because it was already closed or never assigned
    /// to the process in the first place.
    ///
    /// This should only be used in a single-threaded process immediately after
    /// a handle is created/closed, since "The kernel is free to re-use the
    /// integer values of closed handles for newly created objects".
    /// https://fuchsia.dev/fuchsia-src/concepts/kernel/handles#invalid_handles_and_handle_reuse
    pub fn get_info_handle_valid(handle: &Handle) -> Result<(), Status> {
        Status::ok(unsafe {
            sys::zx_object_get_info(
                handle.raw_handle(),
                sys::ZX_INFO_HANDLE_VALID,
                std::ptr::null_mut(),
                0,
                std::ptr::null_mut(),
                std::ptr::null_mut(),
            )
        })
    }

    pub fn create_event() -> Result<Handle, Status> {
        fidl::Event::create().map(Into::into)
    }
}

#[cfg(not(target_os = "fuchsia"))]
pub use non_fuchsia_impl::*;

#[cfg(not(target_os = "fuchsia"))]
mod non_fuchsia_impl {
    use super::*;
    use fidl::EmulatedHandleRef;

    pub fn get_info_handle_valid(handle: &Handle) -> Result<(), Status> {
        if handle.is_dangling() {
            Err(Status::BAD_HANDLE)
        } else {
            Ok(())
        }
    }

    pub fn create_event() -> Result<Handle, Status> {
        Ok(Handle::invalid())
    }
}
