// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "start",
    description = "Starts a component",
    example = "To start the component instance designated by the moniker `/core/brightness_manager`:

    $ ffx component start /core/brightness_manager",
    note = "To learn more about running components, see https://fuchsia.dev/go/components/run"
)]
pub struct ComponentStartCommand {
    #[argh(positional)]
    /// component URL, moniker or instance ID. Partial matches allowed.
    pub query: String,
}
