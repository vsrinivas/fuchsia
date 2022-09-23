// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "process_explorer", description = "Query processes related information")]
pub struct QueryCommand {
    #[argh(subcommand)]
    pub arg: Args,
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand)]
pub enum Args {
    List(ListArg),
    Filter(FilterArg),
    GenerateFuchsiaMap(GenerateFuchsiaMapArg),
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(
    subcommand,
    name = "list",
    description = "outputs a list containing the name and koid of all processes"
)]
pub struct ListArg {
    #[argh(
        switch,
        description = "outputs all processes and the kernel objects owned by each of them"
    )]
    pub verbose: bool,
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(
    subcommand,
    name = "filter",
    description = "outputs information about the processes that correspond to the koids input"
)]
pub struct FilterArg {
    #[argh(positional, description = "process koids")]
    pub process_koids: Vec<u64>,
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(
    subcommand,
    name = "generate-fuchsia-map",
    description = "outputs the json required to generate a map of all processes and channels"
)]
pub struct GenerateFuchsiaMapArg {}
