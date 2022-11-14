// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command, std::path::PathBuf};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "structured-config",
    description = "Verifies component configuration according to configured assertions."
)]
pub struct Command {
    /// absolute or working directory-relative path to a policy file for structured configuration
    #[argh(option)]
    pub policy: PathBuf,

    /// path to a product bundle.
    #[argh(option)]
    pub product_bundle: PathBuf,
}
