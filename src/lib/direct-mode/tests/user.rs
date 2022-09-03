// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    std::io::{stderr, Write},
    std::thread,
};

// Test that launching a thread from a thread other than the primary thread
// works correctly, and behaves as the primary thread does.
fn main() -> Result<()> {
    let _res =
        thread::spawn(|| thread::spawn(|| stderr().write_all(b"Hello direct mode\n")).join())
            .join();
    Ok(())
}
