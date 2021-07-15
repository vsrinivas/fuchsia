// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use libc::uintptr_t;
use std::os::raw::{c_void, c_char, c_int};

pub struct backtrace_state;

pub type backtrace_syminfo_callback =
    extern fn(data: *mut c_void,
              pc: uintptr_t,
              symname: *const c_char,
              symval: uintptr_t,
              symsize: uintptr_t);
pub type backtrace_full_callback =
    extern fn(data: *mut c_void,
              pc: uintptr_t,
              filename: *const c_char,
              lineno: c_int,
              function: *const c_char) -> c_int;
pub type backtrace_error_callback =
    extern fn(data: *mut c_void,
              msg: *const c_char,
              errnum: c_int);

pub fn backtrace_create_state(filename: *const c_char,
                              threaded: c_int,
                              error: backtrace_error_callback,
                              data: *mut c_void) -> *mut backtrace_state {std::ptr::null_mut::<backtrace_state>()}

pub fn backtrace_syminfo(state: *mut backtrace_state,
                          addr: uintptr_t,
                          cb: backtrace_syminfo_callback,
                          error: backtrace_error_callback,
                          data: *mut c_void) -> c_int {0}

pub fn backtrace_pcinfo(state: *mut backtrace_state,
                        addr: uintptr_t,
                        cb: backtrace_full_callback,
                        error: backtrace_error_callback,
                        data: *mut c_void) -> c_int {0}
