// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use affected_builders_lib::{
    argument_parsing::Arguments,
    gn::{DefaultGn, Gn, GnAnalyzeInput},
};

fn main() {
    let Arguments { build_directory, gn_path, changed_files } = Arguments::parse();
    println!("Checking if builder is affected by changes.");
    println!("Build Directory: {:?}", build_directory);
    println!("GN Path: {:?}", gn_path);
    println!("Changed Files: {:?}", changed_files);

    let input = GnAnalyzeInput::all_targets(changed_files);
    println!("{:?}", DefaultGn::new(&build_directory, &gn_path).analyze(input));
}
