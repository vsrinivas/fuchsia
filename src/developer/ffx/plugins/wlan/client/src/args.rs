// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "client", description = "Controls WLAN client policy API.")]
pub struct ClientCommand {
    #[argh(subcommand)]
    pub subcommand: ClientSubcommand,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand)]
pub enum ClientSubcommand {
    BatchConfig(BatchConfig),
    Listen(Listen),
    List(ListSavedNetworks),
    Scan(Scan),
    Start(StartClientConnections),
    Stop(StopClientConnections),
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "batch-config",
    description = "Allows WLAN credentials to be extracted and restored."
)]
pub struct BatchConfig {
    #[argh(subcommand)]
    pub subcommand: BatchConfigSubcommand,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand)]
pub enum BatchConfigSubcommand {
    Dump(Dump),
    Restore(Restore),
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "listen",
    description = "Listens for policy client connections updates",
    example = "To begin listening for client events

    $ ffx wlan client listen"
)]
pub struct Listen {}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "list-saved-networks",
    description = "Lists all networks saved by the WLAN policy layer.",
    example = "To list saved networks

    $ ffx wlan client list-saved-networks",
    note = "Only one application at a time can interact with the WLAN policy
layer."
)]
pub struct ListSavedNetworks {}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "scan",
    description = "Scan for nearby WLAN networks.",
    example = "To scan

    $ ffx wlan client scan",
    note = "Only one application at a time can interact with the WLAN policy
layer."
)]
pub struct Scan {}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "start",
    description = "Allows wlancfg to automate WLAN client operation",
    example = "To start client connections

    $ ffx wlan client start",
    note = "Only one application at a time can interact with the WLAN policy
layer."
)]
pub struct StartClientConnections {}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "stop",
    description = "Stops automated WLAN policy control of client interfaces and
destroys all client interfaces.",
    example = "To stop client connections

    $ ffx wlan client stop",
    note = "Only one application at a time can interact with the WLAN policy
layer."
)]
pub struct StopClientConnections {}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "dump",
    description = "Extracts a structured representation of the device's saved WLAN credentials.",
    example = "To dump WLAN client configs

    $ ffx wlan client batch-config dump",
    note = "Only one application at a time can interact with the WLAN policy layer."
)]
pub struct Dump {}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "restore",
    description = "Injects a structure representation of WLAN credentials into a device.",
    example = "To restore WLAN client configs

    $ ffx wlan client batch-config restore <STRUCTURE_CONFIG_DATA>",
    note = "Only one application at a time can interact with the WLAN policy layer."
)]
pub struct Restore {
    #[argh(positional, description = "structured representation of WLAN credentials.")]
    pub serialized_config: String,
}
