// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "knock",
    description = "Attempts to connect to a service on the target.",
    note = "Useful for starting a service or verifying that a service exists.

Selector format: <component moniker>:(in|out|exposed)[:<service name>]. Wildcards may be used anywhere in the selector.
Example: 'remote-control:expose:*' would return all services exposed by the component remote-control."
)]
pub struct KnockCommand {
    #[argh(positional)]
    pub selector: String,
}
