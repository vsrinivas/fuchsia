// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::path::Path;
use std::process::exit;

fn main() {
    let should_fail_path = Path::new("/fake/install-disk-image-should-fail");

    if should_fail_path.exists() {
        println!("The test asked for me to fail, so I will");
        exit(1);
    } else {
        println!("The test did not ask me to fail, so I won't");
    }
}
