// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern crate magenta_sys;

pub fn main() {
    let time = unsafe { magenta_sys::mx_time_get(magenta_sys::MX_CLOCK_MONOTONIC) };
    println!("before sleep, time = {}", time);
    unsafe { magenta_sys::mx_nanosleep(magenta_sys::mx_deadline_after(1000_000_000)); }
    let time = unsafe { magenta_sys::mx_time_get(magenta_sys::MX_CLOCK_MONOTONIC) };
    println!("after sleep, time = {}", time);
}

