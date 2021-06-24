// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "list-accessors",
    description = "List ArchiveAccessor files.",
    note = "Lists all ArchiveAccessor files under the provided paths.
All paths are implicitly rooted under `/hub-v2`.
If no paths are provided, it'll list everything under /hub-v2.
It is possible to reach v1 components by starting with the path prefix
children/core/children/appmgr/exec/out/hub."
)]
pub struct ListAccessorsCommand {
    #[argh(positional)]
    /// paths from where to list files.
    pub paths: Vec<String>,
}

impl From<ListAccessorsCommand> for iquery::commands::ListAccessorsCommand {
    fn from(cmd: ListAccessorsCommand) -> Self {
        Self { paths: cmd.paths }
    }
}
