// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use affected_targets_lib::{
    analysis::is_build_required, argument_parsing::ProgramArguments, gn::DefaultGn,
};

fn main() {
    let ProgramArguments { gn_path, build_directory, source_directory, changed_files } =
        ProgramArguments::parse();

    println!(
        "{:?}",
        is_build_required(
            changed_files,
            DefaultGn::new(&gn_path, &build_directory, &source_directory)
        )
    );
}
