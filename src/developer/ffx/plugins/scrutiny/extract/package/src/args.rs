// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "package",
    description = "Extracts a Fuchsia package from a Url",
    example = "To extract a Fuchsia package from a url:

        $ ffx scrutiny extract package fuchsia-pkg://fuchsia.com/foo /tmp/foo",
    note = "Extracts a package to a specific directory."
)]
pub struct ScrutinyPackageCommand {
    #[argh(positional)]
    pub url: String,
    #[argh(positional)]
    pub output: String,
}
