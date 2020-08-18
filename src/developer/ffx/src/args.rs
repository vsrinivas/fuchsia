// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command, ffx_lib_sub_command::Subcommand};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
/// Fuchsia's developer tool
pub struct Ffx {
    #[argh(option, short = 'c')]
    /// override default configuration
    pub config: Option<String>,

    #[argh(option, short = 'e')]
    /// override default environment settings
    pub env: Option<String>,

    #[argh(option, short = 't')]
    /// apply operations across single or multiple targets
    pub target: Option<String>,

    #[argh(subcommand)]
    pub subcommand: Option<Subcommand>,
}

impl Default for Ffx {
    fn default() -> Self {
        Self {
            target: None,
            config: None,
            env: None,
            subcommand: Some(Subcommand::FfxDaemonPlugin(ffx_daemon_plugin_args::DaemonCommand {
                subcommand: ffx_daemon_plugin_sub_command::Subcommand::FfxDaemonStart(
                    ffx_daemon_start_args::StartCommand {},
                ),
            })),
        }
    }
}
