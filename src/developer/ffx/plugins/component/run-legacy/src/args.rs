// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "run-legacy",
    example = "To run the 'hello_world_rust' component:

    $ ffx component run-legacy \\
    fuchsia-pkg://fuchsia.com/hello_world_rust#meta/hello_world_rust.cmx",
    description = "Runs a legacy (CMX) component instance on the target",
    note = "To learn more, see https://fuchsia.dev/go/components/url"
)]

pub struct RunComponentCommand {
    #[argh(positional)]
    /// url of component to run
    pub url: String,
    #[argh(positional)]
    /// args for the component
    pub args: Vec<String>,
    #[argh(switch, long = "background", short = 'b')]
    /// switch to turn on background info
    pub background: bool,
}
