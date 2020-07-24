// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command, ffx_lib_sub_command::Subcommand};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
/// Fuchsia Development Bridge
pub struct Ffx {
    #[argh(option)]
    /// runtime configuration information
    pub config: Option<String>,

    #[argh(option)]
    /// environment file where configuration is initialized
    pub environment_file: Option<String>,

    #[argh(option)]
    /// target selection
    pub target: Option<String>,

    #[argh(subcommand)]
    pub subcommand: Subcommand,
}

impl Default for Ffx {
    fn default() -> Self {
        Self {
            target: None,
            config: None,
            environment_file: None,
            subcommand: Subcommand::FfxDaemonSuite(ffx_daemon_suite_args::DaemonCommand {
                subcommand: ffx_daemon_suite_sub_command::Subcommand::FfxDaemonStart(
                    ffx_daemon_start_args::StartCommand {},
                ),
            }),
        }
    }
}
