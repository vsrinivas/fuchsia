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
                 .compiler(format!("{}/buildtools/linux-x64/clang/bin/clang", fuchsia_root))
                 .flag(format!("--sysroot={}/out/build-zircon/build-zircon-pc-x86-64/sysroot", fuchsia_root).as_str())
                 .compile("libbinding.a");
}
