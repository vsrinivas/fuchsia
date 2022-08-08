// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

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
}

/// Retrieve image data.
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "get")]
pub struct GetCommand {
    /// do no network IO, use the locally cached version or fail.
    #[argh(switch)]
    pub cached: bool,

    /// get (and cache) data for specific product bundle.
    #[argh(positional)]
    pub product_bundle_name: Option<String>,

    /// display list of downloaded files and other details.
    #[argh(switch)]
    pub verbose: bool,
}
