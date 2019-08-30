// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon::{self as zx, sys::zx_time_t};
use pin_utils::unsafe_pinned;
use std::{marker::PhantomPinned, mem, pin::Pin, ptr};

use crate::subloop::Subloop;

/// The C type `async_test_subloop_t`.
/// It is treated as opaque. `subloop_t` is a type-safe version of this type.
#[repr(C)]
pub struct async_test_subloop_t {
    _private: [u8; 0],
}

/// The internal representation of the async subloop. This is
/// type-safe: pointers and fields have the right types.  The
/// representation of `async_test_subloop_t` is a prefix of this, so
/// pointers to this structure can be used as `*mut
/// async_test_subloop_t`.
#[repr(C)]
struct subloop_t<T> {
    // This field is present in the C structure.
    // It will always contain a pointer to the `ops_storage` field.
    ops: *const subloop_ops_t<Self>,
    // These fields extend the C structure.
    // Store the actual ops table inline, as we allocate it at the same
    // time as the subloop.
    ops_storage: subloop_ops_t<Self>,
    // `data` is pinned if the subloop is pinned.
    data: T,
    // The subloop contains a self-reference and should not be Unpin.
    _pinned: PhantomPinned,
}

impl<T> subloop_t<T>
where
    T: Subloop,
{
    unsafe_pinned!(data: T);

    /// Converts a raw pointer to the subloop to a pinned mutable reference.
    ///
    /// Callers should ensure that the subloop has not been finalized
    /// or moved and that only one of these references is used at a
    /// time.
    unsafe fn from_raw<'a>(self_ptr: *mut Self) -> Pin<&'a mut Self> {
        Pin::new_unchecked(&mut *self_ptr)
    }

    // Unsafe: these operations are safe as long as the returned
    // subloop are used in accordance with the async_test_subloop_t
    // contract: they are always called with a pointer to the
    // corresponding `subloop_t`, there is no concurrent access, and
    // no access after finalization.  We also do not move the value
    // once constructed, and the user of the async_test_subloop_t is
    // unable to do it, so we can use the self pointer as a pinned
    // mutable reference.

    unsafe extern "C" fn advance_time_to(subloop: *mut Self, time: zx_time_t) {
        let subloop = subloop_t::from_raw(subloop);
        Subloop::advance_time_to(subloop.data(), zx::Time::from_nanos(time)).into()
    }

    unsafe extern "C" fn dispatch_next_due_message(subloop: *mut Self) -> u8 {
        let subloop = subloop_t::from_raw(subloop);
        Subloop::dispatch_next_due_message(subloop.data()).into()
    }

    unsafe extern "C" fn has_pending_work(subloop: *mut Self) -> u8 {
        let subloop = subloop_t::from_raw(subloop);
        Subloop::has_pending_work(subloop.data()).into()
    }

    unsafe extern "C" fn get_next_task_due_time(subloop: *mut Self) -> zx_time_t {
        let subloop = subloop_t::from_raw(subloop);
        Subloop::get_next_task_due_time(subloop.data()).into_nanos()
    }

    unsafe extern "C" fn finalize(subloop: *mut Self) {
        let subloop = Box::from_raw(subloop);
        mem::drop(subloop);
    }
}

/// A type-safe representation of the C `async_test_subloop_ops_t` structure. See
/// `zircon/system/ulib/async-testing/include/lib/async-testing/test_subloop.h` for
/// documentation.
#[repr(C)]
struct subloop_ops_t<T> {
    advance_time_to: unsafe extern "C" fn(*mut T, zx_time_t),
    dispatch_next_due_message: unsafe extern "C" fn(*mut T) -> u8,
    has_pending_work: unsafe extern "C" fn(*mut T) -> u8,
    get_next_task_due_time: unsafe extern "C" fn(*mut T) -> zx_time_t,
    finalize: unsafe extern "C" fn(*mut T),
}

impl<T> subloop_ops_t<subloop_t<T>>
where
    T: Subloop,
{
    /// Creates a subloop_ops_t from a Subloop implementation.
    fn build() -> Self {
        subloop_ops_t {
            advance_time_to: subloop_t::advance_time_to,
            dispatch_next_due_message: subloop_t::dispatch_next_due_message,
            has_pending_work: subloop_t::has_pending_work,
            get_next_task_due_time: subloop_t::get_next_task_due_time,
            finalize: subloop_t::finalize,
        }
    }
}

/// Creates a new subloop structure from something implementing Subloop.
pub(crate) fn new_subloop<T>(data: T) -> *mut async_test_subloop_t
where
    T: Subloop,
{
    // The subloop_t structure is self-referential. We first build it
    // with null instead of the self-pointer.
    let mut subloop = Box::new(subloop_t {
        ops: ptr::null(),
        ops_storage: subloop_ops_t::build(),
        data,
        _pinned: PhantomPinned,
    });
    // Safety: the subloop will not be moved again.
    subloop.ops = &subloop.ops_storage;
    // A subloop_t<T> can be used as an async_test_subloop_t.
    Box::into_raw(subloop) as *mut async_test_subloop_t
}
