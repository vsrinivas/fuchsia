// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "resolve",
    description = "Resolves a component instance",
    example = "To resolve the component designated by the provided moniker `/core/brightness_manager`:

    $ ffx component resolve /core/brightness_manager"
)]
pub struct ComponentResolveCommand {
    #[argh(positional)]
    /// component URL, moniker or instance ID. Partial matches allowed.
    pub query: String,
}
