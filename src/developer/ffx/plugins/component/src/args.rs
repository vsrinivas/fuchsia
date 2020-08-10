// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_component_sub_command::Subcommand, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "component", description = "Discover and manage components")]
pub struct ComponentCommand {
    #[argh(subcommand)]
    pub subcommand: Subcommand,
}
