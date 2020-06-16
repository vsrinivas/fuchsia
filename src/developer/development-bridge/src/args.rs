// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command, ffx_lib_sub_command::Subcommand};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
/// Fuchsia Development Bridge
pub struct Ffx {
    #[argh(option)]
    /// configuration information
    pub config: Option<String>,

    #[argh(option)]
    /// target selection
    pub target: Option<String>,

    #[argh(subcommand)]
    pub subcommand: Subcommand,
}

pub const DEFAULT_FFX: Ffx = Ffx {
    target: None,
    config: None,
    subcommand: Subcommand::Daemon(ffx_core::args::DaemonCommand {}),
};
