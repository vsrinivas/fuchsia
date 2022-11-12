// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    std::path::PathBuf,
    {argh::FromArgs, ffx_core::ffx_command},
};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "static-pkgs",
    description = "Check the static packages extracted from the ZBI against golden files",
    example = "To verify static packages on your current build:

        $ ffx scrutiny verify static-pkgs \
            --build-path $(fx get-build-dir) \
            --blobfs-manifest path/to/blob.manifest \
            --golden path/to/golden"
)]
pub struct Command {
    /// path to root output directory of build.
    #[argh(option)]
    pub build_path: PathBuf,
    /// path to fuchsia update package.
    #[argh(option)]
    pub update: PathBuf,
    /// path to one or more blobfs archives that contain fuchsia packages and
    /// their packages.
    #[argh(option)]
    pub blobfs: Vec<PathBuf>,
    /// path(s) to golden file(s) used to verify routes.
    #[argh(option)]
    pub golden: Vec<PathBuf>,
}
