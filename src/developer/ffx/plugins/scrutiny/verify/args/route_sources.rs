// Copyright 2022 The Fuchsia Authors. All rights reserved.
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
    name = "route-sources",
    description = "Verifies that routes to designated components are routed from designated sources.",
    example = "To verify route sources according to a configuration file on your current build:

        $ ffx scrutiny verify route-sources
            --build-path $(fx get-build-dir) \
            --update path/to/update.far \
            --blobfs path/to/blob.blk \
            --config path/to/verify_route_sources_config.json"
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
    /// path to configuration file that specifies components and their expected
    /// route sources.
    #[argh(option)]
    pub config: PathBuf,
}
