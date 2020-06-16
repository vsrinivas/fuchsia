// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use affected_builders_lib::argument_parsing::Arguments;

fn main() {
    let Arguments { build_directory, changed_files } = Arguments::parse();
    println!("Checking if builder is affected by changes.");
    println!("Build Directory: {:?}", build_directory);
    println!("Changed Files: {:?}", changed_files);
}
