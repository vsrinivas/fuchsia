// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern crate zircon;

use zircon::ClockId;

pub fn main() {
    println!("before sleep, time = {}", zircon::time_get(ClockId::Monotonic));
    zircon::nanosleep(zircon::deadline_after(1_000_000_000));
    println!("after sleep, time = {}", zircon::time_get(ClockId::Monotonic));
}
