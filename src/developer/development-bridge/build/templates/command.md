// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
  argh::FromArgs,
  ffx_config_args::ConfigCommand,
  ffx_core::args::{DaemonCommand, ListCommand, QuitCommand},
};

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand)]
pub enum Subcommand {
    Daemon(DaemonCommand),
    List(ListCommand),
    Config(ConfigCommand),
    Quit(QuitCommand),
{% for dep in deps %}
    {{dep.enum}}({{dep.lib}}::FfxPluginCommand),
{% endfor %}
}

#[derive(FromArgs, Debug, PartialEq)]
/// Fuchsia Development Bridge
pub struct Ffx {
    #[argh(option)]
    /// configuration information
    pub config: Option<String>,

    #[argh(subcommand)]
    pub subcommand: Subcommand,
}
