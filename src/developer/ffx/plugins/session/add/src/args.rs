// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "add",
    description = "Add an element to the current session",
    example = "To add the bouncing_ball.cm component as an element:

    $ ffx session add fuchsia-pkg://fuchsia.com/bouncing_ball#meta/bouncing_ball.cm

To pass arguments to a legacy (cmx) component, specify them after the component URL:

    $ ffx session add fuchsia-pkg://fuchsia.com/spinning-square-rs#meta/spinning-square-rs.cmx foo --bar=baz

Arguments are not supported for modern (cm) components."
)]
pub struct SessionAddCommand {
    /// component URL for the element to add
    #[argh(positional)]
    pub url: String,

    /// arguments passed to the element
    #[argh(positional)]
    pub args: Vec<String>,
}
