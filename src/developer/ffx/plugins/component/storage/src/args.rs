// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "storage", description = "Manages storage capabilities of components")]
pub struct StorageCommand {
    #[argh(subcommand)]
    pub subcommand: SubcommandEnum,

    #[argh(option, default = "String::from(\"/core\")")]
    /// the moniker of the storage provider component.
    /// Defaults to "/core"
    pub provider: String,

    #[argh(option, default = "String::from(\"data\")")]
    /// the capability name of the storage to use.
    /// Examples: "data", "cache", "tmp"
    /// Defaults to "data"
    pub capability: String,
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand)]
pub enum SubcommandEnum {
    Copy(CopyArgs),
    Delete(DeleteArgs),
    List(ListArgs),
    MakeDirectory(MakeDirectoryArgs),
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(
    subcommand,
    name = "list",
    description = "List the contents of a component's storage.",
    example = "To list the contents of the `settings` directory in a component's storage:

    $ ffx component storage list 2042425d4b16ac396ebdb70e40845dc51516dd25754741a209d1972f126a7520::settings

To list the contents of the root directory of a component's storage:

    $ ffx component storage list 2042425d4b16ac396ebdb70e40845dc51516dd25754741a209d1972f126a7520::/

To list the contents of a directory using a different provider and capability:

    $ ffx component storage --provider /core/test_manager --capability data list f1a52f7b4d7081060a3295fd36df7b68fb0518f80aae0eae8a3fc1d55231375f::/

Note: 2042425d4b16ac396ebdb70e40845dc51516dd25754741a209d1972f126a7520 is the instance ID of
the component whose storage is being accessed.

To learn about component instance IDs, see https://fuchsia.dev/go/components/instance-id"
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
    description = "Create a new directory in a component's storage. If the directory already exists, this operation is a no-op.",
    example = "To make a `settings` directory in a storage:

    $ ffx component storage make-directory 2042425d4b16ac396ebdb70e40845dc51516dd25754741a209d1972f126a7520::settings

To make a `settings` directory in a storage from a different provider and capability:

    $ ffx component storage --provider /core/test_manager --capability data make-directory f1a52f7b4d7081060a3295fd36df7b68fb0518f80aae0eae8a3fc1d55231375f::settings

Note: 2042425d4b16ac396ebdb70e40845dc51516dd25754741a209d1972f126a7520 is the instance ID of
the component whose storage is being accessed.

To learn about component instance IDs, see https://fuchsia.dev/go/components/instance-id"
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
    description = "Copy files to/from a component's storage. If the file already exists at the destination it is overwritten.",
    example = "To copy `credentials.json` from the current working directory on the host to the `settings` directory of a component's storage:

    $ ffx component storage copy ./credentials.json 2042425d4b16ac396ebdb70e40845dc51516dd25754741a209d1972f126a7520::settings/credentials.json

To copy `credentials.json` from the current working directory on the host to the `settings` directory from a different provider and capability:

    $ ffx component storage --provider /core/test_manager --capability data copy ./credentials.json f1a52f7b4d7081060a3295fd36df7b68fb0518f80aae0eae8a3fc1d55231375f::settings/credentials.json

Note: 2042425d4b16ac396ebdb70e40845dc51516dd25754741a209d1972f126a7520 is the instance ID of
the component whose storage is being accessed.

To learn about component instance IDs, see https://fuchsia.dev/go/components/instance-id"
)]
pub struct CopyArgs {
    #[argh(positional)]
    /// the source path of the file to be copied
    pub source_path: String,

    #[argh(positional)]
    /// the destination path of the file to be copied
    pub destination_path: String,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "delete",
    description = "Delete files from a component's storage.",
    example = "To delete `credentials.json` from the root directory of a component's persistent storage:

    $ ffx component storage delete 2042425d4b16ac396ebdb70e40845dc51516dd25754741a209d1972f126a7520::credentials.json

Note: 2042425d4b16ac396ebdb70e40845dc51516dd25754741a209d1972f126a7520 is the instance ID of
the component whose storage is being accessed.

To learn about component instance IDs, see https://fuchsia.dev/go/components/instance-id"
)]
pub struct DeleteArgs {
    #[argh(positional)]
    /// the path of the file to be deleted
    pub path: String,
}
