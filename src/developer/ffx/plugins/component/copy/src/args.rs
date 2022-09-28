// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "copy",
    description = "allows copying files to/from a component's namespace",
    example = "TODO:

    $ ffx component run /core/ffx-laboratory:copy",
    note = "TODO: To learn more about running components, see https://fuchsia.dev/go/components/run"
)]

pub struct CopyComponentCommand {
    #[argh(positional)]
    /// source path of either a host filepath or a component namespace entry
    pub source_path: String,

    #[argh(positional)]
    /// destination path to a host filepath or a component namespace entry
    pub destination_path: String,
}
