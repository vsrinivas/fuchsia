// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

pub use ffx_component_storage_args::{Provider, StorageCommand, SubcommandEnum};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "data",
    description = "Manages persistent data storage of components. DEPRECATED: Will be replaced by `ffx component storage`"
)]
pub struct DataCommand {
    #[argh(subcommand)]
    pub subcommand: SubcommandEnum,
}
