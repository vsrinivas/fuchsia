// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern crate magenta;

pub fn main() {
    println!("before sleep, time = {}", magenta::current_time());
    magenta::nanosleep(1_000_000_000);
    println!("after sleep, time = {}", magenta::current_time());
}
