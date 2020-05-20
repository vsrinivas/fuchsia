// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;

/// Given a path in the hub, prints the inspect contained in it. At the moment this command only
/// works for v1 components as we only have a v1 hub.
#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "show-file")]
pub struct ShowFileCommand {
    #[argh(positional)]
    /// paths to query. Minimum: 1
    pub paths: Vec<String>,
}
