// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "show",
    description = "Print inspect hierarchies",
    note = "Prints the inspect hierarchies that match the given selectors."
)]
pub struct ShowCommand {
    #[argh(option)]
    /// the name of the manifest file that we are interested in. If this is provided, the output
    /// will only contain monikers for components whose url contains the provided name.
    pub manifest: Option<String>,

    #[argh(positional)]
    /// selectors for which the selectors should be queried. If no selectors are provided, inspect
    /// data for the whole system will be returned. If `--manifest` is provided then the selectors
    /// should be tree selectors, otherwise component selectors or full selectors.
    pub selectors: Vec<String>,

    #[argh(option)]
    /// the filename we are interested in. If this is provided, the output will only
    /// contain data from components which expose Inspect under the given file under
    /// their out/diagnostics directory.
    pub file: Option<String>,

    #[argh(option)]
    /// the path from where to get the ArchiveAccessor connection. If the given path is a
    /// directory, the command will look for a `fuchsia.diagnostics.ArchiveAccessor` service file.
    /// If the given path is a service file, the command will attempt to connect to it as an
    /// ArchiveAccessor.
    pub accessor: Option<String>,
}

impl From<ShowCommand> for iquery::commands::ShowCommand {
    fn from(cmd: ShowCommand) -> Self {
        Self {
            manifest: cmd.manifest,
            selectors: cmd.selectors,
            file: cmd.file,
            accessor: cmd.accessor,
        }
    }
}
