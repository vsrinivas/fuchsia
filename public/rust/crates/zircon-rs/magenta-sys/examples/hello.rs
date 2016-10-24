// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern crate magenta_sys;

pub fn main() {
    let time = unsafe { magenta_sys::mx_current_time() };
    println!("before sleep, time = {}", time);
    unsafe { magenta_sys::mx_nanosleep(1000_000_000); } 
    let time = unsafe { magenta_sys::mx_current_time() };
    println!("after sleep, time = {}", time);
}

