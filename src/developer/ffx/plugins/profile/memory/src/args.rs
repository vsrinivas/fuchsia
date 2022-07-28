// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command, ffx_profile_memory_sub_command::SubCommand};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "memory", description = "Query memory related information")]
pub struct MemoryCommand {
    #[argh(subcommand)]
    pub subcommand: Option<SubCommand>,

    #[argh(
        option,
        default = "false",
        description = "outputs the json returned by memory_monitor. For debug purposes only, no garantee is made on the stability of the output of this command."
    )]
    pub print_json_from_memory_monitor: bool,

    #[argh(option, description = "filters by process koids. Repeatable flag.")]
    pub process_koids: Vec<u64>,
}
