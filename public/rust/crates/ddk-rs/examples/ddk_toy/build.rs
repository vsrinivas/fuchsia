// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern crate gcc;

use std::env;

// TODO(stange): Figure out how to avoid requiring users to use GCC to build
// a binding.c
fn main() {

    let fuchsia_root = match env::var("FUCHSIA_ROOT") {
        Ok(val) => val,
        Err(error) => panic!("Please set FUCHSIA_ROOT {}", error),
    };

    gcc::Build::new()
                 .file("src/binding.c")
                 // Assumes fuchsia is already built in release mode.
                 .compiler(String::from(fuchsia_root + "/out/release-x86-64/host_x64/x86_64-unknown-fuchsia-cc"))
                 .compile("libbinding.a");
}
