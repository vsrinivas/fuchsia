// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command, std::path::PathBuf};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "apply-selectors",
    description = "Apply selectors from a file interactively."
)]
pub struct ApplySelectorsCommand {
    #[argh(positional)]
    /// path to the selector file to apply to the snapshot.
    pub selector_file: PathBuf,

    #[argh(option)]
    /// path to the inspect json file to read
    /// this file contains inspect.json data from snapshot.zip.
    /// If not provided, DiagnosticsProvider will be used to get inspect data.
    pub snapshot_file: Option<PathBuf>,

    #[argh(option)]
    /// the path from where to get the ArchiveAccessor connection. If the given path is a
    /// directory, the command will look for a `fuchsia.diagnostics.ArchiveAccessor` service file.
    /// If the given path is a service file, the command will attempt to connect to it as an
    /// ArchiveAccessor.
    pub accessor_path: Option<String>,
}
