// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "list-files",
    description = "List all inspect files",
    note = "
Lists all inspect files (*inspect vmo files, fuchsia.inspect.Tree and
fuchsia.inspect.deprecated.Inspect) under the out/diagnostics directory
of the provided monikers or all if none are provided.
"
)]
pub struct ListFilesCommand {
    #[argh(positional)]
    /// optional monikers to query on. If none are provided it will list files across all components.
    pub monikers: Vec<String>,
}

impl From<ListFilesCommand> for iquery::commands::ListFilesCommand {
    fn from(cmd: ListFilesCommand) -> Self {
        Self { monikers: cmd.monikers }
    }
}
