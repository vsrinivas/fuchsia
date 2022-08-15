// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command, std::path::PathBuf};

/// Discover and access product bundle metadata and image data.
#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "product")]
pub struct ProductCommand {
    #[argh(subcommand)]
    pub sub: SubCommand,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand)]
pub enum SubCommand {
    Get(GetCommand),
    Verify(VerifyCommand),
}

/// Retrieve image data.
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "get")]
pub struct GetCommand {
    /// get the data again, even if it's already present locally.
    #[argh(switch)]
    pub force: bool,

    /// repositories will be named `NAME`. Defaults to the product bundle name.
    #[argh(option)]
    pub repository: Option<String>,

    /// url to the product bundle to download.
    #[argh(positional)]
    pub product_bundle_url: String,

    /// local directory to download the product bundle into.
    #[argh(positional, default = "PathBuf::from(\"local_pb\")")]
    pub out_dir: PathBuf,
}

/// Verify that the contents of the product bundle are valid and ready for use.
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "verify")]
pub struct VerifyCommand {
    /// local directory containing the product bundle.
    #[argh(positional)]
    pub product_bundle: PathBuf,

    /// optional verified file to write after successfully verifying,
    /// so that build systems have an output to watch.
    #[argh(option)]
    pub verified_file: Option<PathBuf>,
}
