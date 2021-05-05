// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "package",
    description = "Lists all the files in a package",
    example = "To list all the files in a package:

        $ffx scrutiny list package fuchsia-pkg://fuchsia.com/foo",
    note = "Lists all the package contents in json format."
)]
pub struct ScrutinyPackageCommand {
    #[argh(positional)]
    pub url: String,
}
