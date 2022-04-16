// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "triage",
    description = "Analyze Logs and Inspect in snapshots to find problems."
)]
pub struct TriageCommand {
    #[argh(option)]
    /// path to config file or dir
    pub config: Vec<String>,

    #[argh(option)]
    /// path to snapshot.zip or uncompressed dir
    pub data: Option<String>,

    #[argh(option, long = "tag")]
    /// adds an action tags to include
    pub tags: Vec<String>,

    #[argh(option, long = "exclude-tag")]
    /// adds an action tags to exclude
    pub exclude_tags: Vec<String>,
}
