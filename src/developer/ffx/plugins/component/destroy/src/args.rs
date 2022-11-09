// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "destroy",
    description = "Destroys a dynamic component instance, removing it from the collection designated by <moniker>",
    example = "To destroy a component instance designated by the moniker `/core/ffx-laboratory:foo`:

    $ ffx component destroy /core/ffx-laboratory:foo",
    note = "To learn more about running components, see https://fuchsia.dev/go/components/run"
)]

pub struct DestroyComponentCommand {
    #[argh(positional)]
    /// moniker of an existing component instance in a collection.
    /// This component instance will be removed from the collection if this command succeeds.
    pub moniker: String,
}
