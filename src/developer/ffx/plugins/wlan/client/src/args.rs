// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    argh::FromArgs,
    ffx_core::ffx_command,
    ffx_wlan_common::{
        self,
        args::{CredentialType, SecurityType},
    },
    fidl_fuchsia_wlan_policy as wlan_policy,
};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "client", description = "Controls WLAN client policy API.")]
pub struct ClientCommand {
    #[argh(subcommand)]
    pub subcommand: ClientSubCommand,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand)]
pub enum ClientSubCommand {
    BatchConfig(BatchConfig),
    Connect(Connect),
    Listen(Listen),
    List(ListSavedNetworks),
    RemoveNetwork(RemoveNetwork),
    SaveNetwork(SaveNetwork),
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
    pub subcommand: BatchConfigSubCommand,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand)]
pub enum BatchConfigSubCommand {
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

// RemoveNetwork and SaveNetwork both require a NetworkConfig.  There currently is no clean way to
// share that struct argument between them in argh though.  Some failed experiments:
// (1) If the shared struct is passed directly to the top-level argument enum, then both
//     SaveNetwork and RemoveNetwork will have the same command name which is not desirable.
// (2) argh does not support tuple structs so these commands cannot simply inherit a struct's
//     fields.
// (3) Nesting the network config as a subcommand requires the manual implementation of FromStr
//     which for a command that requires this number of flags would be cumbersome.
//
// The main feature that needs to be shared between these structs is the ability to quickly convert
// them into WLAN policy structs.  While it is possible to create a macro to do all of this code
// generation, the macro ends up requiring several dependencies which makes including the shared
// definitions in other modules a confusing battle against the compiler until the dependencies are
// all found.
//
// Until a better solution exists, the arguments module provides a helper to enable construction of
// a NetworkConfig from the argument struct fields.

#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "remove-network",
    description = "WLAN policy network storage container",
    note = "Only one application at a time can interact with the WLAN policy layer.",
    example = "To remove a WLAN network

    $ffx wlan client remove-network\n
        --ssid TestNetwork\n
        --security-type wpa2\n
        --credential-type password\n
        --credential \"Your very secure password here\""
)]
pub struct RemoveNetwork {
    #[argh(option, default = "String::from(\"\")", description = "WLAN network name")]
    pub ssid: String,
    #[argh(
        option,
        default = "SecurityType::None",
        description = "one of None, WEP, WPA, WPA2, WPA3"
    )]
    pub security_type: SecurityType,
    #[argh(option, default = "CredentialType::None", description = "one of None, PSK, Password")]
    pub credential_type: CredentialType,
    #[argh(option, default = "String::from(\"\")", description = "WLAN Password or PSK")]
    pub credential: String,
}

impl From<RemoveNetwork> for wlan_policy::NetworkConfig {
    fn from(arg: RemoveNetwork) -> Self {
        ffx_wlan_common::args::config_from_args(
            arg.ssid,
            arg.security_type,
            arg.credential_type,
            arg.credential,
        )
    }
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "save-network",
    description = "WLAN policy network storage container",
    note = "Only one application at a time can interact with the WLAN policy layer.",
    example = "To save a WLAN network

    $ffx wlan client save-network\n
        --ssid TestNetwork\n
        --security-type wpa2\n
        --credential-type password\n
        --credential \"Your very secure password here\""
)]
pub struct SaveNetwork {
    #[argh(option, default = "String::from(\"\")", description = "WLAN network name")]
    pub ssid: String,
    #[argh(
        option,
        default = "SecurityType::None",
        description = "one of None, WEP, WPA, WPA2, WPA3"
    )]
    pub security_type: SecurityType,
    #[argh(option, default = "CredentialType::None", description = "one of None, PSK, Password")]
    pub credential_type: CredentialType,
    #[argh(option, default = "String::from(\"\")", description = "WLAN Password or PSK")]
    pub credential: String,
}

impl From<SaveNetwork> for wlan_policy::NetworkConfig {
    fn from(arg: SaveNetwork) -> Self {
        ffx_wlan_common::args::config_from_args(
            arg.ssid,
            arg.security_type,
            arg.credential_type,
            arg.credential,
        )
    }
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "connect",
    description = "Connect to the specified WLAN network",
    note = "Only one application at a time can interact with the WLAN policy layer.",
    example = "To remove a WLAN network

    $ffx wlan client connect\n
        --ssid TestNetwork\n
        --security-type wpa2"
)]
pub struct Connect {
    #[argh(option, default = "String::from(\"\")", description = "WLAN network name")]
    pub ssid: String,
    #[argh(option, description = "one of None, WEP, WPA, WPA2, WPA3")]
    pub security_type: Option<SecurityType>,
}
