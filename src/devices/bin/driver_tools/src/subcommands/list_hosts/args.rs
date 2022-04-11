// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;

#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "list-hosts",
    description = "List driver hosts and drivers loaded within them",
    example = "To list all driver hosts:

    $ driver list-hosts",
    error_code(1, "Failed to connect to the driver development service")
)]
pub struct ListHostsCommand {
    /// if this exists, the user will be prompted for a component to select.
    #[argh(switch, short = 's', long = "select")]
    pub select: bool,
}
