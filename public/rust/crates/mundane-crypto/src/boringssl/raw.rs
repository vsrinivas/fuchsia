// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Almost-raw bindings to the BoringSSL API.
//!
//! The `raw` module provides bindings to the BoringSSL API which add a little
//! bit of safety beyond the safety provided by completely raw bindings by
//! ensuring that all return values are checked for errors, and converting these
//! C-style return values into Rust `Result`s.
//!
//! This module also directly re-exports any raw bindings which are infallible
//! (e.g., `void` functions).

// infallible functions
pub use boringssl_sys::{CBB_cleanup, CBB_len, CBS_init, CBS_len, EC_GROUP_get_curve_name,
                        ERR_error_string_n, ERR_get_error, ERR_print_errors_cb};

use std::num::NonZeroUsize;
use std::os::raw::{c_char, c_int, c_uint};
use std::ptr::NonNull;

use boringssl_sys::{CBB, CBS, EC_GROUP, EC_KEY, EVP_PKEY};

use boringssl::BoringError;

// bytestring.h

impl_traits!(CBB, CDestruct => CBB_cleanup);
impl_traits!(CBS, CDestruct => _);

#[allow(non_snake_case)]
#[must_use]
pub unsafe fn CBB_init(cbb: *mut CBB, initial_capacity: usize) -> Result<(), BoringError> {
    one_or_err("CBB_init", ::boringssl_sys::CBB_init(cbb, initial_capacity))
}

#[allow(non_snake_case)]
#[must_use]
pub unsafe fn CBB_data(cbb: *const CBB) -> Result<NonNull<u8>, BoringError> {
    ptr_or_err("CBB_init", ::boringssl_sys::CBB_data(cbb) as *mut _)
}

// ec.h

#[allow(non_snake_case)]
#[must_use]
pub unsafe fn EC_GROUP_new_by_curve_name(nid: c_int) -> Result<NonNull<EC_GROUP>, BoringError> {
    ptr_or_err(
        "EC_GROUP_new_by_curve_name",
        ::boringssl_sys::EC_GROUP_new_by_curve_name(nid),
    )
}

// ec_key.h

impl_traits!(EC_KEY, CNew => EC_KEY_new, CUpRef => EC_KEY_up_ref, CFree => EC_KEY_free);
impl_traits!(EVP_PKEY, CNew => EVP_PKEY_new, CUpRef => EVP_PKEY_up_ref, CFree => EVP_PKEY_free);

#[allow(non_snake_case)]
#[must_use]
pub unsafe fn EC_curve_nid2nist(nid: c_int) -> Result<NonNull<c_char>, BoringError> {
    ptr_or_err(
        "EC_curve_nid2nist",
        ::boringssl_sys::EC_curve_nid2nist(nid) as *mut _,
    )
}

#[allow(non_snake_case)]
#[must_use]
pub unsafe fn EC_KEY_generate_key(key: *mut EC_KEY) -> Result<(), BoringError> {
    one_or_err(
        "EC_KEY_generate_key",
        ::boringssl_sys::EC_KEY_generate_key(key),
    )
}

#[allow(non_snake_case)]
#[must_use]
pub unsafe fn EC_KEY_get0_group(key: *const EC_KEY) -> Result<NonNull<EC_GROUP>, BoringError> {
    ptr_or_err(
        "EC_KEY_get0_group",
        ::boringssl_sys::EC_KEY_get0_group(key) as *mut _,
    )
}

#[allow(non_snake_case)]
#[must_use]
pub unsafe fn EC_KEY_marshal_private_key(
    cbb: *mut CBB, key: *const EC_KEY, enc_flags: c_uint,
) -> Result<(), BoringError> {
    one_or_err(
        "EC_KEY_marshal_private_key",
        ::boringssl_sys::EC_KEY_marshal_private_key(cbb, key, enc_flags),
    )
}

#[allow(non_snake_case)]
#[must_use]
pub unsafe fn EC_KEY_parse_private_key(
    cbs: *mut CBS, group: *const EC_GROUP,
) -> Result<NonNull<EC_KEY>, BoringError> {
    ptr_or_err(
        "EC_KEY_parse_private_key",
        ::boringssl_sys::EC_KEY_parse_private_key(cbs, group),
    )
}

#[allow(non_snake_case)]
#[must_use]
pub unsafe fn EC_KEY_set_group(
    key: *mut EC_KEY, group: *const EC_GROUP,
) -> Result<(), BoringError> {
    one_or_err(
        "EC_KEY_set_group",
        ::boringssl_sys::EC_KEY_set_group(key, group),
    )
}

// ecdsa.h

#[allow(non_snake_case)]
#[must_use]
pub unsafe fn ECDSA_sign(
    type_: c_int, digest: *const u8, digest_len: usize, sig: *mut u8, sig_len: *mut c_uint,
    key: *const EC_KEY,
) -> Result<(), BoringError> {
    one_or_err(
        "ECDSA_sign",
        ::boringssl_sys::ECDSA_sign(type_, digest, digest_len, sig, sig_len, key),
    )
}

#[allow(non_snake_case)]
#[must_use]
pub unsafe fn ECDSA_size(key: *const EC_KEY) -> Result<NonZeroUsize, BoringError> {
    NonZeroUsize::new(::boringssl_sys::ECDSA_size(key))
        .ok_or_else(|| BoringError::consume_stack("ECDSA_size"))
}

#[allow(non_snake_case)]
#[must_use]
pub unsafe fn ECDSA_verify(
    type_: c_int, digest: *const u8, digest_len: usize, sig: *const u8, sig_len: usize,
    key: *const EC_KEY,
) -> bool {
    match ::boringssl_sys::ECDSA_verify(type_, digest, digest_len, sig, sig_len, key) {
        1 => true,
        0 => false,
        _ => unreachable!(),
    }
}

// evp.h

#[allow(non_snake_case)]
#[must_use]
pub unsafe fn EVP_marshal_public_key(
    cbb: *mut CBB, key: *const EVP_PKEY,
) -> Result<(), BoringError> {
    one_or_err(
        "EVP_marshal_public_key",
        ::boringssl_sys::EVP_marshal_public_key(cbb, key),
    )
}

#[allow(non_snake_case)]
#[must_use]
pub unsafe fn EVP_parse_public_key(cbs: *mut CBS) -> Result<NonNull<EVP_PKEY>, BoringError> {
    ptr_or_err(
        "EVP_parse_public_key",
        ::boringssl_sys::EVP_parse_public_key(cbs),
    )
}

#[allow(non_snake_case)]
#[must_use]
pub unsafe fn EVP_PKEY_assign_EC_KEY(
    pkey: *mut EVP_PKEY, key: *mut EC_KEY,
) -> Result<(), BoringError> {
    one_or_err(
        "EVP_PKEY_assign_EC_KEY",
        ::boringssl_sys::EVP_PKEY_assign_EC_KEY(pkey, key),
    )
}

#[allow(non_snake_case)]
#[must_use]
pub unsafe fn EVP_PKEY_get1_EC_KEY(pkey: *mut EVP_PKEY) -> Result<NonNull<EC_KEY>, BoringError> {
    ptr_or_err(
        "EVP_PKEY_get1_EC_KEY",
        ::boringssl_sys::EVP_PKEY_get1_EC_KEY(pkey),
    )
}

// utility functions

// If code is 1, returns Ok, otherwise returns Err. f should be the name of the
// function that returned this value.
#[must_use]
pub fn one_or_err(f: &str, code: c_int) -> Result<(), BoringError> {
    if code == 1 {
        Ok(())
    } else {
        Err(BoringError::consume_stack(f))
    }
}

// If ptr is non-NULL, returns Ok, otherwise returns Err. f should be the name
// of the function that returned this value.
fn ptr_or_err<T>(f: &str, ptr: *mut T) -> Result<NonNull<T>, BoringError> {
    NonNull::new(ptr).ok_or_else(|| BoringError::consume_stack(f))
}
