// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "list",
    description = "List components that expose inspect",
    note = "Lists all components (relative to the scope where the archivist receives events from)
of components that expose inspect.

For v1: this is the realm path plus the realm name

For v2: this is the moniker without the instances ids."
)]
pub struct ListCommand {
    #[argh(option)]
    /// the name of the manifest file that we are interested in. If this is provided, the output
    /// will only contain monikers for components whose url contains the provided name.
    pub manifest: Option<String>,

    #[argh(switch)]
    /// also print the URL of the component.
    pub with_url: bool,

    #[argh(option)]
    /// the path from where to get the ArchiveAccessor connection. If the given path is a
    /// directory, the command will look for a `fuchsia.diagnostics.ArchiveAccessor` service file.
    /// If the given path is a service file, the command will attempt to connect to it as an
    /// ArchiveAccessor.
    pub accessor: Option<String>,
}

impl From<ListCommand> for iquery::commands::ListCommand {
    fn from(cmd: ListCommand) -> Self {
        Self { manifest: cmd.manifest, with_url: cmd.with_url, accessor: cmd.accessor }
    }
}
