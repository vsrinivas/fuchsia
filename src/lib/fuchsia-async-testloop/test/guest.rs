// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_async as fasync;
use fuchsia_async_testloop::{async_test_subloop_t, new_subloop};
use libc::c_int;

async unsafe fn set_to_zero_later(ret: *mut c_int) {
    fasync::Timer::new(fasync::Time::from_nanos(1000)).await;
    *ret = 0
}

#[no_mangle]
unsafe extern "C" fn make_rust_loop(ret: *mut c_int) -> *mut async_test_subloop_t {
    new_subloop(set_to_zero_later(ret))
}
