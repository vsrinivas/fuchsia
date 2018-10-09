// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern crate fuchsia_zircon as zircon;

use zircon::{ClockId, DurationNum, Time};

pub fn main() {
    println!("before sleep, time = {:?}", Time::get(ClockId::Monotonic));
    1_000_000_000.nanos().sleep();
    println!("after sleep, time = {:?}", Time::get(ClockId::Monotonic));
}
