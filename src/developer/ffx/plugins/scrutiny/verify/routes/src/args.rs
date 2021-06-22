// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "routes",
    description = "Verifies protocol and directory routes in Fuchsia",
    example = "To verify the routes on your current build:

        $ffx scrutiny verify routes
        $ffx scrutiny verify routes directory
        $ffx scrutiny verify routes protocol",
    note = "Verifies all protocol and directory routes."
)]
pub struct ScrutinyRoutesCommand {
    #[argh(positional, default = "String::from(\"directory protocol\")")]
    pub capability: String,
}
