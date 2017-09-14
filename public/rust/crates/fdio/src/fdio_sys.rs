// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use zircon_sys as sys;
use std::os::raw;

#[allow(non_camel_case_types)]
pub type watchdir_func_t = unsafe extern "C" fn(
    dirfd: ::std::os::raw::c_int,
    event: ::std::os::raw::c_int,
    fn_: *const ::std::os::raw::c_char,
    cookie: *const ::std::os::raw::c_void,
) -> sys::zx_status_t;

#[link(name = "fdio")]
extern "C" {
    pub fn fdio_watch_directory(
        dirfd: raw::c_int,
        cb: watchdir_func_t,
        deadline: sys::zx_time_t,
        cookie: *const raw::c_void,
    ) -> sys::zx_status_t;

    pub fn fdio_ioctl(
        fd: i32,
        op: i32,
        in_buf: *const u8,
        in_len: usize,
        out_buf: *mut u8,
        out_len: usize,
    ) -> isize;
}

pub const WATCH_EVENT_ADD_FILE: raw::c_int = 1;
pub const WATCH_EVENT_REMOVE_FILE: raw::c_int = 2;
pub const WATCH_EVENT_IDLE: raw::c_int = 3;
