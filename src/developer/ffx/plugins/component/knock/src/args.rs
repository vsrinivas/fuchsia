// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "knock",
    description = "Connects to a capability described by a given selector",
    example = "To connect to a service:

    $ ffx component knock 'core/appmgr:out:fuchsia.hwinfo.Product'",
    note = "Knock verifies the existence of a capability by attempting to connect to it.
The command expects a <selector> with the following format:

`<component moniker>:(in|out|exposed)[:<service name>].`

Note that wildcards can be used but must match exactly one service.

`ffx component select` can be used to explore the component
topology and find the correct selector for use in this command."
)]
pub struct KnockCommand {
    #[argh(positional)]
    pub selector: String,
}
