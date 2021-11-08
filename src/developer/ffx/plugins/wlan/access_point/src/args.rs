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
#[argh(subcommand, name = "ap", description = "Controls WLAN AP policy API.")]
pub struct ApCommand {
    #[argh(subcommand)]
    pub subcommand: ApSubcommand,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand)]
pub enum ApSubcommand {
    Listen(Listen),
    Start(Start),
    Stop(Stop),
    StopAll(StopAll),
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "listen",
    description = "Listens for policy AP updates",
    example = "To begin listening for AP events

    $ ffx wlan ap listen"
)]
pub struct Listen {}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "stop-all",
    description = "Stops all active APs",
    note = "Only one application at a time can interact with the WLAN policy layer.",
    example = "To stop all APs

    $ ffx wlan ap stop-all"
)]
pub struct StopAll {}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "start",
    description = "Start an access point interface",
    note = "Only one application at a time can interact with the WLAN policy layer.",
    example = "To start an AP

    $ffx wlan ap start\n
        --ssid TestNetwork\n
        --security-type wpa2\n
        --credential-type password\n
        --credential \"Your very secure password here\""
)]
pub struct Start {
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

impl From<Start> for wlan_policy::NetworkConfig {
    fn from(arg: Start) -> Self {
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
    name = "stop",
    description = "Stop an access point interface",
    note = "Only one application at a time can interact with the WLAN policy layer.",
    example = "To stop an AP

    $ffx wlan ap stop\n
        --ssid TestNetwork\n
        --security-type wpa2\n
        --credential-type password\n
        --credential \"Your very secure password here\""
)]
pub struct Stop {
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

impl From<Stop> for wlan_policy::NetworkConfig {
    fn from(arg: Stop) -> Self {
        ffx_wlan_common::args::config_from_args(
            arg.ssid,
            arg.security_type,
            arg.credential_type,
            arg.credential,
        )
    }
}
