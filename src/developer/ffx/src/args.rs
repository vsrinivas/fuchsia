// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;
use ffx_core::ffx_command;
use ffx_lib_sub_command::SubCommand;

/// Fuchsia's developer tool
#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
pub struct FfxBuiltIn {
    #[argh(subcommand)]
    pub subcommand: Option<SubCommand>,
}
