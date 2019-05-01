// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![deny(warnings)]

use fuchsia_zircon as zx;
use std::collections::HashSet;

fn main() -> Result<(), zx::Status> {
    let mut set = HashSet::new();
    for _ in 0..8 {
        let mut buf = vec![0; 16];
        zx::cprng_draw(&mut buf)?;
        if !set.insert(buf) {
            return Err(zx::Status::INTERNAL);
        }
    }
    println!("PASS");
    Ok(())
}
