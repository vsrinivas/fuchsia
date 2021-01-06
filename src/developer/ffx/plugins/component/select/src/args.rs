// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "select",
    description = "Lists components matching a selector",
    example = "To show services exposed by remote-control:

    $ ffx component select remote-control:expose:*'

Or to show all services offered by v1 components:

    $ ffx component select core/appmgr:out:*",
    note = "Component select allows for looking up various services exposed by the
component. The command expects a <selector> with the following format:

`<component moniker>:(in|out|exposed)[:<service name>]`

Wildcards may be used anywhere in the selector.",
    error_code(1, "No matching component paths found")
)]
pub struct SelectCommand {
    #[argh(positional)]
    pub selector: String,
}
