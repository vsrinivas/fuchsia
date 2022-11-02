// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This crate contains utility functions used in GIDL tests and benchmarks.

use {
    fidl::{AsHandleRef, Handle, HandleBased, Rights},
    fuchsia_zircon_status::Status,
};

/// Handle subtypes that can be created via `create_handles`. Each subtype `X`
/// corresponds to a `fidl::X` type that implements `HandleBased`.
pub enum HandleSubtype {
    Event,
    Channel,
}

/// Specifies a handle to be created with `create_handles`. Corresponds to
/// `HandleDef` in //tools/fidl/gidl/ir/test_case.go.
pub struct HandleDef {
    pub subtype: HandleSubtype,
    pub rights: Rights,
}

/// Creates a vector of handles whose concrete subtypes correspond to the given
/// list. It panics if creating any of the handles fails.
pub fn create_handles(defs: &[HandleDef]) -> Vec<Handle> {
    let mut factory: HandleFactory = Default::default();
    let mut handles = Vec::with_capacity(defs.len());
    for def in defs {
        let default_rights_handle = match def.subtype {
            HandleSubtype::Event => factory.create_event().unwrap().into_handle(),
            HandleSubtype::Channel => factory.create_channel().unwrap().into_handle(),
        };
        handles.push(match def.rights {
            Rights::SAME_RIGHTS => default_rights_handle,
            rights => default_rights_handle.replace(rights).unwrap(),
        });
    }
    handles
}

/// HandleFactory creates handles. For handle subtypes that come in pairs, it
/// stores the second one and returns it on the next call to minimize syscalls.
#[derive(Default)]
struct HandleFactory {
    extra_channel: Option<fidl::Channel>,
}

// See src/lib/fidl/rust/fidl/src/lib.rs for handle subtypes. The ones marked
// "Everywhere" are fully emulated on non-Fuchsia, so we can define create_*
// functions that work on all platforms. The ones marked "FuchsiaOnly" are not
// emulated, so we have to define create_* differently based on target_os.

impl HandleFactory {
    fn create_channel(&mut self) -> Result<fidl::Channel, Status> {
        match self.extra_channel.take() {
            Some(channel) => Ok(channel),
            None => {
                let (c1, c2) = fidl::Channel::create()?;
                self.extra_channel = Some(c2);
                Ok(c1)
            }
        }
    }
}

#[cfg(target_os = "fuchsia")]
impl HandleFactory {
    fn create_event(&mut self) -> Result<fidl::Event, Status> {
        fidl::Event::create()
    }
}

#[cfg(not(target_os = "fuchsia"))]
impl HandleFactory {
    fn create_event(&mut self) -> Result<fidl::Event, Status> {
        Ok(Handle::invalid().into())
    }
}

/// Unsafely copies `handle`, i.e. makes a new `Handle` object with the same raw
/// value, and converts it to handle subtype `T`.
///
/// # Safety
///
/// The caller is responsible for manually leaking one of handles (`handle` or
/// the returned copy) to avoid double-close errors when handles are dropped.
/// This can be done with `std::mem::forget` or in aggregate with `disown_vec`.
pub unsafe fn copy_handle<T: HandleBased>(handle: &Handle) -> T {
    T::from_handle(Handle::from_raw(handle.raw_handle()))
}

/// Makes unsafe copies of handles at the given indices, i.e. new `Handle`
/// objects with the same raw values.
///
/// # Safety
///
/// The caller must use `disown_handles` on one of the lists (`handles` or the
/// returned vector) to prevent double-close errors when handles are dropped.
pub unsafe fn copy_handles_at(handles: &[Handle], indices: &[usize]) -> Vec<Handle> {
    let mut copy = Vec::with_capacity(indices.len());
    for &i in indices {
        copy.push(Handle::from_raw(handles[i].raw_handle()));
    }
    copy
}

/// Wraps `Vec<T>` in a structure that prevents elements from being dropped. The
/// vector itself will release its memory, but it will not invoke `T::drop`.
///
/// To use the contained vector after, use this pattern:
///
///     let items = disown_vec(...);
///     let items = items.as_ref(); // or as_mut
///
/// The seperate let-bindings are necessary for the `Disowned` value to outlive
/// the reference obtained from it.
pub fn disown_vec<T>(vec: Vec<T>) -> Disowned<T> {
    Disowned(vec)
}

pub struct Disowned<T>(Vec<T>);

impl<T> AsRef<Vec<T>> for Disowned<T> {
    fn as_ref(&self) -> &Vec<T> {
        &self.0
    }
}

impl<T> AsMut<Vec<T>> for Disowned<T> {
    fn as_mut(&mut self) -> &mut Vec<T> {
        &mut self.0
    }
}

impl<T> Drop for Disowned<T> {
    fn drop(&mut self) {
        for h in self.0.drain(..) {
            std::mem::forget(h);
        }
    }
}

/// Returns the result of the `zx_object_get_info` syscall with topic
/// `ZX_INFO_HANDLE_VALID`. In particular, returns `Status::BAD_HANDLE` if
/// the handle is dangling because it was already closed or never assigned
/// to the process in the first place.
///
/// This should only be used in a single-threaded process immediately after
/// a handle is created/closed, since "The kernel is free to re-use the
/// integer values of closed handles for newly created objects".
/// https://fuchsia.dev/fuchsia-src/concepts/kernel/handles#invalid_handles_and_handle_reuse
#[cfg(target_os = "fuchsia")]
pub fn get_info_handle_valid(handle: &Handle) -> Result<(), Status> {
    use fuchsia_zircon::sys;
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

#[cfg(not(target_os = "fuchsia"))]
pub fn get_info_handle_valid(handle: &Handle) -> Result<(), Status> {
    use fidl::EmulatedHandleRef;
    // Match the behavior of the syscall, returning BAD_HANDLE if the handle is
    // the special "never a valid handle" ZX_HANDLE_INVALID, or if it is invalid
    // because it was closed or never assigned in the first place (dangling).
    if handle.is_invalid() || handle.is_dangling() {
        Err(Status::BAD_HANDLE)
    } else {
        Ok(())
    }
}
