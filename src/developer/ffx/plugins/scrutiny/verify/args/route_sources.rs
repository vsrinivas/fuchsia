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
    example = r#"To verify route sources according to a configuration file on your current build:

    $ ffx scrutiny verify route-sources
        --build-path $(fx get-build-dir) \
        --update obj/build/images/fuchsia/update/update.far \
        --blobfs obj/build/images/fuchsia/fuchsia/blob.blk \
        --blobfs obj/build/images/fuchsia/update/gen/update.blob.blk \
        --config path/to/verify_route_sources/product.board.json5"#
)]
pub struct Command {
    /// absolute or working directory-relative path to root output directory of build.
    #[argh(option)]
    pub build_path: PathBuf,
    /// absolute or build path-relative path to fuchsia update package.
    #[argh(option)]
    pub update: PathBuf,
    /// absolute or build path-relative path to one or more blobfs archives that contain fuchsia
    /// packages and their packages.
    #[argh(option)]
    pub blobfs: Vec<PathBuf>,
    /// absolute or build path-relative path to configuration file that specifies components and
    /// their expected route sources.
    #[argh(option)]
    pub config: PathBuf,
}
