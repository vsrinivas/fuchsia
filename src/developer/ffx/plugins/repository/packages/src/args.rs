// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "packages", description = "List the packages inside a repository")]
pub struct PackagesCommand {
    /// list packages from this repository.
    #[argh(positional)]
    pub name: String,

    /// if true, package hashes will be displayed in full (i.e. not truncated).
    #[argh(switch)]
    pub full_hash: bool,
}
