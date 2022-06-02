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
    name = "component-resolvers",
    description = "Verifies that component configured to use custom component resolvers are permitted by an allowlist.",
    example = "To verify component resolvers on your current eng build:

        $ ffx scrutiny verify component-resolvers \
            --build-path $(fx get-build-dir) \
            --repository-path $(fx get-build-dir)/amber-files/repository \
            --allowlist path/to/allowlist.json5",
    note = "Verifies all components that use a custom component resolver."
)]
pub struct Command {
    /// path to fuchsia build directory.
    #[argh(option)]
    pub build_path: PathBuf,
    /// path to fuchsia update package.
    #[argh(option)]
    pub update: PathBuf,
    /// path to one or more blobfs archives that contain fuchsia packages and
    /// their packages.
    #[argh(option)]
    pub blobfs: Vec<PathBuf>,
    /// path to allowlist file that specifies which components may use
    /// particular custom component resolvers.
    #[argh(option)]
    pub allowlist: PathBuf,
}
