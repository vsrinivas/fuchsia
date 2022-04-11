// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use argh::FromArgs;

#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "lsblk",
    description = "Prints out block device info",
    example = "To show the all block devices:

    $ driver lsblk",
    error_code(1, "Failed to connect to the device manager service")
)]
pub struct LsblkCommand {
    #[argh(switch, short = 's', long = "select")]
    /// if this exists, the user will be prompted for a component to select.
    pub select: bool,
}
