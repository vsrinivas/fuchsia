// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[allow(deprecated)] // Necessary for AsciiExt usage from clap args_enum macro
use clap::arg_enum;
use eui48::MacAddress;
use fidl_fuchsia_wlan_common as wlan_common;
use fidl_fuchsia_wlan_device as wlan;
use fidl_fuchsia_wlan_policy as wlan_policy;
use structopt::StructOpt;

arg_enum! {
    #[derive(PartialEq, Copy, Clone, Debug)]
    pub enum RoleArg {
        Client,
        Ap
    }
}

arg_enum! {
    #[derive(PartialEq, Copy, Clone, Debug)]
    pub enum ScanTypeArg {
        Active,
        Passive,
    }
}

arg_enum! {
    #[derive(PartialEq, Copy, Clone, Debug)]
    pub enum SecurityTypeArg {
        None,
        Wep,
        Wpa,
        Wpa2,
        Wpa3,
    }
}

arg_enum! {
    #[derive(PartialEq, Copy, Clone, Debug)]
    pub enum CredentialTypeArg {
        None,
        Psk,
        Password,
    }
}

impl ::std::convert::From<RoleArg> for wlan::MacRole {
    fn from(arg: RoleArg) -> Self {
        match arg {
            RoleArg::Client => wlan::MacRole::Client,
            RoleArg::Ap => wlan::MacRole::Ap,
        }
    }
}

impl ::std::convert::From<ScanTypeArg> for wlan_common::ScanType {
    fn from(arg: ScanTypeArg) -> Self {
        match arg {
            ScanTypeArg::Active => wlan_common::ScanType::Active,
            ScanTypeArg::Passive => wlan_common::ScanType::Passive,
        }
    }
}

impl ::std::convert::From<SecurityTypeArg> for wlan_policy::SecurityType {
    fn from(arg: SecurityTypeArg) -> Self {
        match arg {
            SecurityTypeArg::r#None => wlan_policy::SecurityType::None,
            SecurityTypeArg::Wep => wlan_policy::SecurityType::Wep,
            SecurityTypeArg::Wpa => wlan_policy::SecurityType::Wpa,
            SecurityTypeArg::Wpa2 => wlan_policy::SecurityType::Wpa2,
            SecurityTypeArg::Wpa3 => wlan_policy::SecurityType::Wpa3,
        }
    }
}

#[derive(StructOpt, Clone, Debug)]
pub struct PolicyNetworkId {
    #[structopt(long, required = true)]
    pub ssid: String,
    #[structopt(
        long = "security-type",
        default_value = "none",
        raw(possible_values = "&SecurityTypeArg::variants()"),
        raw(case_insensitive = "true")
    )]
    pub security_type: SecurityTypeArg,
}

#[derive(StructOpt, Clone, Debug)]
pub struct PolicyNetworkConfig {
    #[structopt(long, required = true)]
    pub ssid: String,
    #[structopt(
        long = "security-type",
        default_value = "none",
        raw(possible_values = "&SecurityTypeArg::variants()"),
        raw(case_insensitive = "true")
    )]
    pub security_type: SecurityTypeArg,
    #[structopt(
        long = "credential-type",
        default_value = "none",
        raw(possible_values = "&CredentialTypeArg::variants()"),
        raw(case_insensitive = "true")
    )]
    pub credential_type: CredentialTypeArg,
    #[structopt(long)]
    pub credential: Option<String>,
}

#[derive(StructOpt, Clone, Debug)]
pub enum PolicyClientCmd {
    #[structopt(name = "connect")]
    Connect(PolicyNetworkId),
    #[structopt(name = "list-saved-networks")]
    GetSavedNetworks,
    #[structopt(name = "listen")]
    Listen,
    #[structopt(name = "remove-network")]
    RemoveNetwork(PolicyNetworkConfig),
    #[structopt(name = "save-network")]
    SaveNetwork(PolicyNetworkConfig),
    #[structopt(name = "scan")]
    ScanForNetworks,
    #[structopt(name = "start-client-connections")]
    StartClientConnections,
    #[structopt(name = "stop-client-connections")]
    StopClientConnections,
}

#[derive(StructOpt, Clone, Debug)]
pub enum PolicyAccessPointCmd {
    // TODO(sakuma): Allow users to specify connectivity mode and operating band.
    #[structopt(name = "start")]
    Start(PolicyNetworkConfig),
    #[structopt(name = "stop")]
    Stop(PolicyNetworkConfig),
    #[structopt(name = "stop-all")]
    StopAllAccessPoints,
    #[structopt(name = "listen")]
    Listen,
}

#[derive(StructOpt, Clone, Debug)]
pub enum DeprecatedConfiguratorCmd {
    #[structopt(name = "suggest-mac")]
    SuggestAccessPointMacAddress {
        #[structopt(raw(required = "true"))]
        mac: MacAddress,
    },
}

#[derive(StructOpt, Clone, Debug)]
pub enum Opt {
    #[structopt(name = "client")]
    Client(PolicyClientCmd),
    #[structopt(name = "ap")]
    AccessPoint(PolicyAccessPointCmd),
    #[structopt(name = "deprecated")]
    Deprecated(DeprecatedConfiguratorCmd),
}
