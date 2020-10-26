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
    /// sets the target to which operations are applied. Overwrites the runtime
    /// config for "target.default". If this isn't set, will fall back to
    /// "target.default" from the user config. This is applied after
    /// --config overrides.
    pub target: Option<String>,

    #[argh(option, short = 'p')]
    #[ffx_config_default(key = "proxy.timeout_secs", default = "1.0")]
    /// sets how long to wait for the proxy to timeout in fractional seconds.
    /// Backed by the config value "proxy.timeout_secs". Defaults to 1.0
    /// if unavailable in the config.
    pub proxy_timeout: Option<f64>,

    #[argh(switch, short = 'v', description = "direct all log output to the launching terminal")]
    /// verbose output always prints to stdio, both frontend and daemon
    pub verbose: bool,

    #[argh(subcommand)]
    pub subcommand: Option<Subcommand>,
}
