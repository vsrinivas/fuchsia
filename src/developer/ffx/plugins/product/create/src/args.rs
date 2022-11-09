// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;
use ffx_core::ffx_command;
use std::path::PathBuf;

/// Create a Product Bundle using the outputs of Product Assembly.
#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "create")]
pub struct CreateCommand {
    /// path to a partitions config, which lists the physical partitions of the target.
    #[argh(option)]
    pub partitions: PathBuf,

    /// path to an assembly manifest, which specifies images to put in slot A.
    #[argh(option)]
    pub system_a: Option<PathBuf>,

    /// path to an assembly manifest, which specifies images to put in slot B.
    #[argh(option)]
    pub system_b: Option<PathBuf>,

    /// path to an assembly manifest, which specifies images to put in slot R.
    #[argh(option)]
    pub system_r: Option<PathBuf>,

    /// path to the directory of TUF keys, which should include root.json, snapshot.json,
    /// targets.json, and timestamp.json. If provided, then a TUF repository will be created inside
    /// the product bundle and filled with the product blobs.
    #[argh(option)]
    pub tuf_keys: Option<PathBuf>,

    /// file containing the version of the Product to put in the update package.
    #[argh(option)]
    pub update_package_version_file: Option<PathBuf>,

    /// backstop OTA version.
    /// Fuchsia will reject updates with a lower epoch.
    #[argh(option)]
    pub update_package_epoch: Option<u64>,

    /// directory to write the product bundle.
    #[argh(option)]
    pub out_dir: PathBuf,
}
