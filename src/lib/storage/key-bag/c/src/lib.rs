// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// C-friendly bindings for KeyBagManager.
pub use fuchsia_zircon::sys::zx_status_t;
pub use key_bag::{Aes256Key, KeyBagManager};

use {
    fuchsia_zircon::{
        self as zx,
        sys::{self},
    },
    std::ffi::CStr,
};

/// Creates a `KeyBagManager` by opening or creating 'path', and returns an opaque pointer to it.
///
/// # Safety
///
/// The passed arguments might point to invalid memory.
#[no_mangle]
pub unsafe extern "C" fn keybag_create(
    path: *const std::os::raw::c_char,
    out: *mut *mut KeyBagManager,
) -> sys::zx_status_t {
    if path.is_null() || out.is_null() {
        return zx::Status::INVALID_ARGS.into_raw();
    }
    let c_str = CStr::from_ptr(path);
    let path_str = match c_str.to_str() {
        Ok(str) => str.to_owned(),
        Err(_) => return zx::Status::INVALID_ARGS.into_raw(),
    };
    let path = std::path::Path::new(&path_str);
    let manager = match KeyBagManager::open(path).map_err(|e| zx::Status::from(e).into_raw()) {
        Ok(manager) => manager,
        Err(status) => return status,
    };

    *out = Box::into_raw(Box::new(manager));
    zx::Status::OK.into_raw()
}

/// Deallocates a previously created `KeyBagManager`.
///
/// # Safety
///
/// The passed arguments might point to invalid memory, or an object we didn't allocate.
#[no_mangle]
pub unsafe extern "C" fn keybag_destroy(keybag: *mut KeyBagManager) {
    if !keybag.is_null() {
        let _ = Box::from_raw(keybag);
    }
}

/// Generates and stores a wrapped key in the key bag.
///
/// # Safety
///
/// The passed arguments might point to invalid memory.
#[no_mangle]
pub unsafe extern "C" fn keybag_new_key(
    keybag: *mut KeyBagManager,
    slot: u16,
    wrapping_key: *const Aes256Key,
) -> sys::zx_status_t {
    if keybag.is_null() || wrapping_key.is_null() {
        return zx::Status::INVALID_ARGS.into_raw();
    }
    // Safety: |keybag| or |key| might not point to a valid object.
    match keybag
        .as_mut()
        .unwrap()
        .new_key(slot, wrapping_key.as_ref().unwrap())
        .map_err(|e| zx::Status::from(e).into_raw())
    {
        Ok(_) => zx::Status::OK.into_raw(),
        Err(s) => s,
    }
}

/// Removes the key at the given slot from the key bag.
///
/// # Safety
///
/// The passed arguments might point to invalid memory.
#[no_mangle]
pub unsafe extern "C" fn keybag_remove_key(
    keybag: *mut KeyBagManager,
    slot: u16,
) -> sys::zx_status_t {
    if keybag.is_null() {
        return zx::Status::INVALID_ARGS.into_raw();
    }
    match keybag.as_mut().unwrap().remove_key(slot).map_err(|e| zx::Status::from(e).into_raw()) {
        Ok(_) => zx::Status::OK.into_raw(),
        Err(s) => s,
    }
}

/// Unwraps all keys which can be unwrapped with |wrapping_key|
///
/// # Safety
///
/// The passed arguments might point to invalid memory.
#[no_mangle]
pub unsafe extern "C" fn keybag_unwrap_key(
    keybag: *mut KeyBagManager,
    slot: u16,
    wrapping_key: *const Aes256Key,
    out_key: *mut Aes256Key,
) -> sys::zx_status_t {
    if keybag.is_null() || wrapping_key.is_null() || out_key.is_null() {
        return 0;
    }
    match keybag
        .as_mut()
        .unwrap()
        .unwrap_key(slot, wrapping_key.as_ref().unwrap())
        .map_err(|e| zx::Status::from(e).into_raw())
    {
        Ok(key) => {
            std::ptr::write(out_key, key);
            zx::Status::OK.into_raw()
        }
        Err(s) => s,
    }
}
