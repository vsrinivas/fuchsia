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

This will run the following checks:
    * Verify that the component's lists of outgoing and exposed capabilities match",
    note = "When using the `doctor` subcommand, the following diagnostic checks are ran on a component:

    1- Check that the lists of `outgoing` and `exposed` capabilities match

All the checks are ran consecutively. In the case of a check failing, the following checks WILL be
ran and the command will return an error code afterwards.
    "
)]
pub struct DoctorCommand {
    #[argh(positional)]
    /// the component's moniker. Example: `/core/appmgr`.
    pub moniker: Vec<String>,
}
