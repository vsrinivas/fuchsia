// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow::Result, std::arch::asm};

// Test that accessing CR3 will cause termination of the thread.
fn main() -> Result<()> {
    let mut _cr3: i64;
    unsafe {
        asm!("mov cr3, {x}", x = out(reg) _cr3);
    }
    Ok(())
}
