// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "doctor",
    description = "Perform diagnostic checks on a component at runtime.",
    example = "To run diagnostics:

$ ffx component doctor /core/appmgr

This will run checks on the capabilities configuration of the component, checking that all of the
`use` and `expose` capabilities can be routed successfully by the component manager."
)]
pub struct DoctorCommand {
    #[argh(positional)]
    /// the component's moniker. Example: `/core/appmgr`.
    pub moniker: String,

    #[argh(switch, long = "plain", short = 'p')]
    /// whether or not to display the output without color and wrapping.
    pub plain_output: bool,
}
