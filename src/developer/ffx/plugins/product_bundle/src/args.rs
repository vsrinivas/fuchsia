// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

/// Discover and get access to product bundle metadata and image data.
#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "product-bundle")]
pub struct ProductBundleCommand {
    #[argh(subcommand)]
    pub sub: SubCommand,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand)]
pub enum SubCommand {
    List(ListCommand),
    Get(GetCommand),
}

/// Display a list of product bundle names.
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "list")]
pub struct ListCommand {
    /// do no network IO, use the locally cached version or fail.
    #[argh(switch)]
    pub cached: bool,
}

/// Retrieve image data.
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "get")]
pub struct GetCommand {
    /// get (and cache) data for specific product bundle.
    #[argh(positional)]
    pub product_bundle_name: Option<String>,
}
