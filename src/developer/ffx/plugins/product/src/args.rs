// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;
use ffx_core::ffx_command;
use ffx_product_sub_command::SubCommand;

/// Discover and access product bundle metadata and image data.
#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "product")]
pub struct ProductCommand {
    #[argh(subcommand)]
    pub subcommand: SubCommand,
}
