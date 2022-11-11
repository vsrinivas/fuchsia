// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "stop",
    description = "Stops a component instance",
    example = "To stop the component instance designated by the moniker `/core/brightness_manager`:

    $ ffx component stop /core/brightness_manager",
    note = "To learn more about running components, see https://fuchsia.dev/go/components/run"
)]
pub struct ComponentStopCommand {
    #[argh(positional)]
    /// component URL, moniker or instance ID. Partial matches allowed.
    pub query: String,
    #[argh(switch, short = 'r')]
    /// whether or not to stop the component recursively
    pub recursive: bool,
}
