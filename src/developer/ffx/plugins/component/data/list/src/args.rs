// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "list",
    description = "List the contents of a component's persistent data storage.",
    example = "To list the contents of the `settings` directory in a component's persistent data storage:

    $ ffx component data list 2042425d4b16ac396ebdb70e40845dc51516dd25754741a209d1972f126a7520::settings

To list the contents of the root directory of a component's persistent data storage:

    $ ffx component data list 2042425d4b16ac396ebdb70e40845dc51516dd25754741a209d1972f126a7520::/

Note: 2042425d4b16ac396ebdb70e40845dc51516dd25754741a209d1972f126a7520 is the instance ID of
the component whose persistent data storage is being accessed.

To learn about component instance IDs, visit https://fuchsia.dev/fuchsia-src/development/components/component_id_index"
)]

pub struct ListCommand {
    #[argh(positional)]
    /// a path to a remote directory
    pub path: String,
}
