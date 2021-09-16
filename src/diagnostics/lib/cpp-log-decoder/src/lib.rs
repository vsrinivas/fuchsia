// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

use diagnostics_message::{Message, MonikerWithUrl};
use std::ffi::CString;
use std::os::raw::c_char;

#[no_mangle]
pub extern "C" fn fuchsia_decode_log_message_to_json(msg: *const u8, size: usize) -> *mut c_char {
    let managed_ptr;
    unsafe {
        managed_ptr = std::slice::from_raw_parts(msg, size);
    }
    let msg = &Message::from_structured(
        MonikerWithUrl { moniker: "<test_moniker>".to_string(), url: "".to_string() },
        managed_ptr,
    )
    .unwrap()
    .data;
    let item = serde_json::to_string(&msg).unwrap();
    CString::new(format!("[{}]", item)).unwrap().into_raw()
}

#[no_mangle]
pub extern "C" fn fuchsia_free_decoded_log_message(msg: *mut c_char) {
    let str_to_free;
    unsafe {
        str_to_free = CString::from_raw(msg);
    }
    let _freer = str_to_free;
}
