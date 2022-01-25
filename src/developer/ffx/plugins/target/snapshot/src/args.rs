// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq, Clone)]
#[argh(
    subcommand,
    name = "snapshot",
    description = "Takes a snapshot of the target's state",
    example = "Store the target's snapshot in the current directory:

    $ ffx target snapshot -d .
    Exported ./snapshot.zip",
    note = "This command connects to a running target to acquire its snapshot, which contains
useful debugging information about the target. The `--dir` can be supplied to override the default
snapshot directory `/tmp/snapshots/YYYYMMDD_HHMMSS/`.

Snapshot contents:
- Build information and annotations
- Kernel and system logs
- Inspect data"
)]
pub struct SnapshotCommand {
    /// valid directory where the snapshot will be stored
    #[argh(option, long = "dir", short = 'd')]
    pub output_file: Option<String>,

    #[argh(switch, long = "dump-annotations")]
    /// print annotations without capturing the snapshot, ignores `dir` flag
    pub dump_annotations: bool,
}
