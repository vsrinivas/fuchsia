// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
/// Interact with the tracing subsystem
#[argh(subcommand, name = "trace")]
pub struct TraceCommand {
    #[argh(subcommand)]
    pub sub_cmd: TraceSubCommand,
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand)]
pub enum TraceSubCommand {
    ListProviders(ListProviders),
    // More commands including `record` and `convert` to follow.
}

#[derive(FromArgs, PartialEq, Debug)]
/// List the target's trace providers
#[argh(subcommand, name = "list-providers")]
pub struct ListProviders {}
