// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "list",
    description = "List all components",
    example = "To list all components in the topology:

    $ ffx component list",
    note = "Lists all the components on the running target. The command outputs a
tree of all v1 and v2 components on the system.

If the command fails or times out, ensure RCS is running on the target.
This can be verified by running `ffx target list` and seeing the status
on the RCS column.",
    error_code(1, "The command has timed out")
)]
pub struct ComponentListCommand {}
