// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "reload",
    description = "Recursively stops, unresolves, and starts a component instance, updating the code and topology while preserving resources",
    example = "To reload a component instance designated by the moniker `/core/ffx-laboratory:foo`:

    $ ffx component reload /core/ffx-laboratory:foo",
    note = "To learn more about running components, see https://fuchsia.dev/go/components/run"
)]

pub struct ReloadComponentCommand {
    #[argh(positional)]
    /// component URL, moniker or instance ID. Partial matches allowed.
    pub query: String,
}
