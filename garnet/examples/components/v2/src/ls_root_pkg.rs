// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// #![deny(warnings)]

use std::fs;

fn main() {
    let paths = fs::read_dir("/root_pkg").unwrap();
    for path in paths {
        println!("{}", path.unwrap().path().display())
    }
}
