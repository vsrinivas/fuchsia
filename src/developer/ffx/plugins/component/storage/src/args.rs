// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command, std::str::FromStr};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "storage", description = "Manages persistent data storage of components")]
pub struct StorageCommand {
    #[argh(subcommand)]
    pub subcommand: SubcommandEnum,

    #[argh(option, default = "Provider::Data")]
    /// the storage provider to use.
    /// Options: [data, cache, temp].
    /// Defaults to 'data'
    pub provider: Provider,
}

#[derive(PartialEq, Debug)]
pub enum Provider {
    Data,
    Cache,
    Temp,
}

impl FromStr for Provider {
    type Err = String;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s {
            "data" => Ok(Provider::Data),
            "cache" => Ok(Provider::Cache),
            "temp" | "tmp" => Ok(Provider::Temp),
            _ => Err(format!(
                "'{}' is not a valid storage provider: Must be one of 'data', 'cache' or 'temp'",
                s
            )),
        }
    }
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand)]
pub enum SubcommandEnum {
    Copy(CopyArgs),
    List(ListArgs),
    MakeDirectory(MakeDirectoryArgs),
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(
    subcommand,
    name = "list",
    description = "List the contents of a component's persistent data storage.",
    example = "To list the contents of the `settings` directory in a component's persistent data storage:

    $ ffx component storage list 2042425d4b16ac396ebdb70e40845dc51516dd25754741a209d1972f126a7520::settings

To list the contents of the root directory of a component's persistent data storage:

    $ ffx component storage list 2042425d4b16ac396ebdb70e40845dc51516dd25754741a209d1972f126a7520::/

Note: 2042425d4b16ac396ebdb70e40845dc51516dd25754741a209d1972f126a7520 is the instance ID of
the component whose persistent data storage is being accessed.

To learn about component instance IDs, visit https://fuchsia.dev/fuchsia-src/development/components/component_id_index"
)]
pub struct ListArgs {
    #[argh(positional)]
    /// a path to a remote directory
    pub path: String,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "make-directory",
    description = "Create a new directory in a component's persistent data storage. If the directory already exists, this operation is a no-op.",
    example = "To make a `settings` directory in a storage:

    $ ffx component storage make-directory 2042425d4b16ac396ebdb70e40845dc51516dd25754741a209d1972f126a7520::settings

Note: 2042425d4b16ac396ebdb70e40845dc51516dd25754741a209d1972f126a7520 is the instance ID of
the component whose persistent data storage is being accessed.

To learn about component instance IDs, visit https://fuchsia.dev/fuchsia-src/development/components/component_id_index"
)]
pub struct MakeDirectoryArgs {
    #[argh(positional)]
    /// a path to a non-existent remote directory
    pub path: String,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "copy",
    description = "Copy files to/from a component's persistent data storage. If the file already exists at the destination it is overwritten.",
    example = "To copy `credentials.json` from the current working directory on the host to the `settings` directory of a component's persistent data storage:

    $ ffx component storage copy ./credentials.json 2042425d4b16ac396ebdb70e40845dc51516dd25754741a209d1972f126a7520::settings/credentials.json

Note: 2042425d4b16ac396ebdb70e40845dc51516dd25754741a209d1972f126a7520 is the instance ID of
the component whose persistent data storage is being accessed.

To learn about component instance IDs, visit https://fuchsia.dev/fuchsia-src/development/components/component_id_index"
)]
pub struct CopyArgs {
    #[argh(positional)]
    /// the source path of the file to be copied
    pub source_path: String,

    #[argh(positional)]
    /// the destination path of the file to be copied
    pub destination_path: String,
}
