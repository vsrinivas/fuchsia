// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;

#[derive(FromArgs)]
///Inputs used to auto generate CFv2 tests.
pub struct AutoTestGeneratorCommand {
    /// path to cm file location.
    #[argh(option, short = 'l')]
    pub cm_location: String,

    /// directory to store generated test code files.
    #[argh(option, short = 'o')]
    pub out_dir: String,

    /// component under test url, if not specified the filename specified in --cm-location will be used.
    #[argh(option, short = 'u')]
    pub component_url: Option<String>,

    /// if true, will generate all dependent services as mocked services.
    #[argh(switch, short = 'm')]
    pub generate_mocks: bool,

    /// generate test code in cpp, default is rust.
    #[argh(switch, short = 'c')]
    pub cpp: bool,

    /// also print to stdout.
    #[argh(switch, short = 'v')]
    pub verbose: bool,
}
