// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;

#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "run-tool",
    description = "Runs a driver tool executable in the driver_playground.",
    example = "To run a tool:

    $ driver runtool fuchsia-pkg://fuchsiasamples.com/eductl#bin/eductl -- fact 5",
    error_code(1, "Failed to connect to the driver playground service")
)]
pub struct RunToolCommand {
    #[argh(positional, description = "path of the driver tool binary.")]
    pub tool: String,

    #[argh(positional, description = "the arguments to pass to the tool.")]
    pub args: Vec<String>,
}
