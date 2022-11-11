// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "show",
    description = "Shows detailed information about a component instance",
    example = "To show information about the `brightness_manager` component instance, all of the
following commands are valid:

    $ ffx component show /core/brightness_manager
    $ ffx component show fuchsia-pkg://fuchsia.com/brightness_manager#meta/brightness_manager.cm
    $ ffx component show meta/brightness_manager.cm
    $ ffx component show brightness_manager",
    note = "This command supports partial matches over the moniker, URL and instance ID"
)]
pub struct ComponentShowCommand {
    #[argh(positional)]
    /// component URL, moniker or instance ID. Partial matches allowed.
    pub filter: String,
}
