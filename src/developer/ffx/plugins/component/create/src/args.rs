// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "create",
    description = "Creates a dynamic component instance, adding it to the collection designated by <moniker>",
    example = "To create a component instance designated by the moniker `/core/ffx-laboratory:foo`:

    $ ffx component create /core/ffx-laboratory:foo fuchsia-pkg://fuchsia.com/hello-world#meta/hello-world-rust.cm",
    note = "To learn more about running components, see https://fuchsia.dev/go/components/run"
)]

pub struct CreateComponentCommand {
    #[argh(positional)]
    /// moniker of a component instance in an existing collection. See https://fuchsia.dev/fuchsia-src/reference/components/moniker
    /// The component instance will be added to the collection if the command
    /// succeeds.
    pub moniker: String,

    #[argh(positional)]
    /// url of the component to create.
    pub url: String,
}
