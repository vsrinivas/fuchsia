// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

/// Discover and get access to product bundle metadata and image data.
#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "product-bundles")]
pub struct ProductBundlesCommand {
    #[argh(subcommand)]
    pub sub: SubCommand,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand)]
pub enum SubCommand {
    Fetch(FetchCommand),
    List(ListCommand),
    Pull(PullCommand),
}

/// Retrieve updated metadata.
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "fetch")]
pub struct FetchCommand {}

/// Display a list of product bundle names.
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "list")]
pub struct ListCommand {}

/// Retrieve image data.
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "pull")]
pub struct PullCommand {}
