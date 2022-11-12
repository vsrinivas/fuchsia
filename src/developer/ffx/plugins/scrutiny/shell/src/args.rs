// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "shell",
    description = "Launch the scrutiny shell",
    example = "To start an interactive shell session:

    $ ffx scrutiny shell

To run commands directly:

    $ ffx scrutiny shell components
    $ ffx scrutiny shell \"search.packages --files fdio\"
    ",
    note = "Launches an interactive scrutiny shell where scrutiny specific
commands can be run. This will also launch a server on port 127.0.0.1:8080
by default that provides visual auditing tools.

Inside the shell run help for a full list of available commands. If you wish to
integrate Scrutiny as part of a wider script check out the --script option."
)]
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
