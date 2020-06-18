// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use affected_builders_lib::{
    analysis::is_build_required, argument_parsing::ProgramArguments, gn::DefaultGn,
};

fn main() {
    let ProgramArguments { build_directory, gn_path, changed_files } = ProgramArguments::parse();

    println!("{:?}", is_build_required(changed_files, DefaultGn::new(&build_directory, &gn_path)));
}
