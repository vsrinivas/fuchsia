// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;
use ffx_core::ffx_command;
use std::path::PathBuf;

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "structured-config",
    description = "Extracts the structured configuration from Fuchsia build artifacts.",
    note = "Extracts structured configuration"
)]
pub struct ScrutinyStructuredConfigCommand {
    /// path to a product bundle.
    #[argh(option)]
    pub product_bundle: PathBuf,
    /// path to the build directory which is used to rebase the paths in the depfile.
    #[argh(option)]
    pub build_path: PathBuf,
    /// path to a depfile that should be written for build integration
    #[argh(option)]
    pub depfile: PathBuf,
    /// path to file to which to write the extracted configuration.
    #[argh(option)]
    pub output: PathBuf,
}
