// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_component_data_sub_command::Subcommand, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "data", description = "Manage persistent data storage of components")]
pub struct DataCommand {
    #[argh(subcommand)]
    pub subcommand: Subcommand,
}
