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
        switch,
        description = "outputs the json returned by memory_monitor. For debug purposes only, no garantee is made on the stability of the output of this command."
    )]
    pub debug_json: bool,

    #[argh(option, description = "filters by process koids. Repeatable flag.")]
    pub process_koids: Vec<u64>,

    #[argh(
        option,
        description = "repeats the command at the given interval (in seconds) until terminated."
    )]
    pub interval: Option<f64>,

    #[argh(switch, description = "prints a bucketized digest of the memory usage.")]
    pub buckets: bool,

    #[argh(
        switch,
        description = "outputs csv that for every process shows the device uptime in nano seconds, the process koid, the process name, and the private, scale, and total memory usage. This option is not supported with other output options like --machine."
    )]
    pub csv: bool,
}
