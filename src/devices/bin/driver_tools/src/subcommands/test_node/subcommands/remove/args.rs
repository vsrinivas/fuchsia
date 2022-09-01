// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;

#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "remove",
    description = "Remove a test node.
    ",
    example = "To remove a test node to the driver framework:

    $ driver test-node remove my-node",
    error_code(1, "Failed to connect to the driver development service")
)]
pub struct RemoveTestNodeCommand {
    /// the name of the test node.
    #[argh(positional)]
    pub name: String,
}
