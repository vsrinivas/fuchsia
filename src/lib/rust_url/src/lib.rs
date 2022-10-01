// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon::Status;
use std::ffi::{c_char, c_void, CStr, CString};
use url::Url;

/// Parse a URL, returning success/failure as a status code. On success, `out` will contain a
/// pointer to the parsed URL.
///
/// # Safety
///
/// * `input` must be a valid pointer to a null-terminated string. The string must consist solely of
///   UTF-8 characters but failure to provide UTF-8 will result in defined behavior.
/// * `out` must be a valid pointer to write to. The pointer written there must be freed with
///   `rust_url_free`.
#[no_mangle]
unsafe extern "C" fn rust_url_parse(input: *const c_char, out: *mut *mut c_void) -> Status {
    if let Ok(raw_url) = CStr::from_ptr(input).to_str() {
        match Url::parse(raw_url) {
            Ok(url) => {
                *out = Box::into_raw(Box::new(url)) as *mut c_void;
                Status::OK
            }
            Err(_) => Status::INVALID_ARGS,
        }
    } else {
        Status::INVALID_ARGS
    }
}

/// Free a URL parsed with `rust_url_parse`.
///
/// # Safety
///
/// * `url` must have been produced from `rust_url_parse`.
/// * This function can only be called once per pointer.
#[no_mangle]
unsafe extern "C" fn rust_url_free(url: *mut c_void) {
    drop(Box::from_raw(url as *mut Url));
}

/// Get the domain from a parsed URL, returning a C-string if available. If no domain is present,
/// a null pointer is returned.
///
/// # Safety
///
/// * `url` must have been produced from a successful call to `rust_url_parse`.
/// * `url` cannot have been freed before calling this function.
#[no_mangle]
unsafe extern "C" fn rust_url_get_domain(url: *const c_void) -> *const c_char {
    let url = &*(url as *const Url);

    if let Some(domain) = url.domain() {
        CString::new(domain).expect("no null bytes in a valid URL's domain").into_raw()
    } else {
        std::ptr::null()
    }
}

/// Free a domain returned by `rust_url_get_domain`.
///
/// # Safety
///
/// * `domain` must be a valid non-null pointer returned by `rust_url_get_domain`.
/// * This function can only be called once per pointer.
#[no_mangle]
unsafe extern "C" fn rust_url_free_domain(domain: *mut c_char) {
    drop(CString::from_raw(domain));
}
