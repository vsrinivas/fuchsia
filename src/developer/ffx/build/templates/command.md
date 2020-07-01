// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


// TODO: Once ListCommand is made into plugins they can be removed from 
// here and then this can merged with suite_command.md

use {
  argh::FromArgs,
  ffx_core::{
    args::ListCommand,
  }
};

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand)]
pub enum Subcommand {
    List(ListCommand),
{% for dep in deps %}
    {{dep.enum}}({{dep.lib}}::FfxPluginCommand),
{% endfor %}
}

