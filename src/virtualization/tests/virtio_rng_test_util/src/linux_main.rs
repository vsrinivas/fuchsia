// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![deny(warnings)]

use std::collections::HashSet;
use std::fs::File;
use std::io::{Error, ErrorKind, Read};

fn run_test() -> std::io::Result<()> {
    let mut set = HashSet::new();
    let mut rng = File::open("/dev/random")?;
    for _ in 0..8 {
        let mut buf = vec![0; 16];
        rng.read_exact(&mut buf)?;
        if !set.insert(buf) {
            return Err(Error::new(ErrorKind::Other, "Repeated random draw"));
        }
    }
    Ok(())
}

fn main() -> std::io::Result<()> {
    run_test().map(|_| println!("PASS"))
}
