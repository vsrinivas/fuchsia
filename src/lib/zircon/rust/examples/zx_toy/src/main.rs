// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern crate fuchsia_zircon as zircon;

use zircon::{DurationNum, Time};

pub fn main() {
    println!("before sleep, time = {:?}", Time::get_monotonic());
    1_000_000_000.nanos().sleep();
    println!("after sleep, time = {:?}", Time::get_monotonic());
}
