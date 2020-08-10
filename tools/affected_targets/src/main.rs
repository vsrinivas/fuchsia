// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use affected_targets_lib::{
    analysis::{is_build_required, AnalysisResult},
    argument_parsing::ProgramArguments,
    files::{contains_disabled_file_types, FileType},
    gn::DefaultGn,
};
use serde::Serialize;

#[derive(Debug, Serialize, PartialEq)]
pub struct Output {
    /// The result of the analysis.
    result: AnalysisResult,

    /// The result of running the analysis, regardless of the presence of disabled file types.
    result_without_explicit_disables: AnalysisResult,

    /// Whether or not the change contained explicitly disabled file types.
    explicitly_disabled_file_types: bool,
}

fn main() {
    let ProgramArguments { gn_path, build_directory, source_directory, disable_cpp, changed_files } =
        ProgramArguments::parse();

    let disabled_file_types = if disable_cpp { vec![FileType::Cpp] } else { vec![] };
    let explicitly_disabled = contains_disabled_file_types(&changed_files, disabled_file_types);

    let result_without_explicit_disables = is_build_required(
        changed_files,
        DefaultGn::new(&gn_path, &build_directory, &source_directory),
    );

    let result = if explicitly_disabled {
        AnalysisResult::Unknown("Explicitly disabled file types present.".to_string())
    } else {
        result_without_explicit_disables.clone()
    };

    let output = Output {
        result,
        result_without_explicit_disables,
        explicitly_disabled_file_types: explicitly_disabled,
    };

    // Print the JSON output.
    println!("{:}", serde_json::to_string(&output).unwrap_or_default());
}
