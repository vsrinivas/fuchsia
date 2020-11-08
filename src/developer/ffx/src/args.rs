// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    argh::FromArgs, ffx_config::FfxConfigBacked, ffx_core::ffx_command,
    ffx_lib_sub_command::Subcommand,
};

#[ffx_command()]
#[derive(FfxConfigBacked, FromArgs, Debug, PartialEq)]
/// Fuchsia's developer tool
pub struct Ffx {
    #[argh(option, short = 'c')]
    /// override default configuration
    pub config: Option<String>,

    #[argh(option, short = 'e')]
    /// override default environment settings
    pub env: Option<String>,

    #[argh(option, short = 't')]
    #[ffx_config_default("target.default")]
    /// apply operations across single or multiple targets
    pub target: Option<String>,

    #[argh(option, short = 'T')]
    #[ffx_config_default(key = "proxy.timeout_secs", default = "1.0")]
    /// override default proxy timeout
    pub timeout: Option<f64>,

    #[argh(switch, short = 'v')]
    /// use verbose output
    pub verbose: bool,

    #[argh(subcommand)]
    pub subcommand: Option<Subcommand>,
}
