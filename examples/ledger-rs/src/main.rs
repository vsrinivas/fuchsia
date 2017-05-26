// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern crate magenta;

use magenta::ClockId;

pub fn main() {
    println!("rust_ledger_example: This will eventually interface with ledger. For now it does nothing.");
    println!("before sleep, time = {}", magenta::time_get(ClockId::Monotonic));
    magenta::nanosleep(magenta::deadline_after(1_000_000_000));
    println!("after sleep, time = {}", magenta::time_get(ClockId::Monotonic));
}
