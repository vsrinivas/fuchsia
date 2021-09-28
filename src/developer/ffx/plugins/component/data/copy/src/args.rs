// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "copy",
    description = "Copy files to/from a component's persistent data storage. If the file already exists at the destination it is overwritten.",
    example = "To copy `credentials.json` from the current working directory on the host to the `settings` directory of a component's persistent data storage:

    $ ffx component data copy ./credentials.json 2042425d4b16ac396ebdb70e40845dc51516dd25754741a209d1972f126a7520::settings/credentials.json

Note: 2042425d4b16ac396ebdb70e40845dc51516dd25754741a209d1972f126a7520 is the instance ID of
the component whose persistent data storage is being accessed.

To learn about component instance IDs, visit https://fuchsia.dev/fuchsia-src/development/components/component_id_index"
)]

pub struct CopyCommand {
    #[argh(positional)]
    /// the source path of the file to be copied
    pub source_path: String,

    #[argh(positional)]
    /// the destination path of the file to be copied
    pub destination_path: String,
}
