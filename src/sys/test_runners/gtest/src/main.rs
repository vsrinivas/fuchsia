// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{fs, path::Path};

fn main() {
    println!("runner starting");
    // We will divide this directory up and pass to  tests as /test_result so that they can write
    // their json output
    let path = Path::new("/data/test_data");
    fs::create_dir(&path).expect("should not fail");
    println!("runner ending");
}
