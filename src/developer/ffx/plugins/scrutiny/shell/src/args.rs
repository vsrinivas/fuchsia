// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "shell", description = "Run the scrutiny shell")]
pub struct ScrutinyShellCommand {
    #[argh(positional)]
    pub command: Option<String>,
    #[argh(option, description = "set a custom path to the build directory", short = 'b')]
    pub build: Option<String>,
    #[argh(option, description = "run a file as a scrutiny script", short = 's')]
    pub script: Option<String>,
    #[argh(option, description = "set a custom path to the data model", short = 'm')]
    pub model: Option<String>,
    #[argh(option, description = "set a custom path output log", short = 'l')]
    pub log: Option<String>,
    #[argh(option, description = "set a custom port to run scrutiny on", short = 'p')]
    pub port: Option<u16>,
    #[argh(option, description = "set the verbosity level of logging", short = 'v')]
    pub verbosity: Option<String>,
}
