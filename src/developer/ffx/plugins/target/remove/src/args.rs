// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq, Clone)]
#[argh(
    subcommand,
    name = "remove",
    description = "Make the daemon forget a specific target",
    example = "To remove a target by its target name:

    $ ffx target remove correct-horse-battery-staple

Or to remove a target using its IP address:

    $ ffx target remove fe80::32fd:38ff:fea8:a00a",
    note = "IP addresses are matched by their full string representation.
for best results, copy the exact address from ffx target list."
)]

pub struct RemoveCommand {
    #[argh(positional)]
    /// name or IP of the target.
    pub id: String,
}
