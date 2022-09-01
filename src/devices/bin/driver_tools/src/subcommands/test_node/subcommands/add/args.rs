// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;

#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "add",
    description = "Add a test node with a given bind property",
    example = "To add a test node to the driver framework:

    $ driver test-node add my-node my-key-string=my-value-string",
    error_code(1, "Failed to connect to the driver development service")
)]
pub struct AddTestNodeCommand {
    /// the name of the test node.
    #[argh(positional)]
    pub name: String,

    /// the test node's property. Should be in the format key=value.
    #[argh(positional)]
    pub property: String,
}
