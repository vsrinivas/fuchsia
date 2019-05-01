// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(deprecated)] // Necessary for AsciiExt usage from clap args_enum macro

use clap::arg_enum;
use fidl_fuchsia_wlan_common as wlan_common;
use fidl_fuchsia_wlan_device as wlan;
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
    pub enum PhyArg {
        Erp,
        Ht,
        Vht,
    }
}

arg_enum! {
    #[derive(PartialEq, Copy, Clone, Debug)]
    pub enum CbwArg {
        Cbw20,
        Cbw40,
        Cbw80,
    }
}

arg_enum! {
    #[derive(PartialEq, Copy, Clone, Debug)]
    pub enum ScanTypeArg {
        Active,
        Passive,
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

impl ::std::convert::From<PhyArg> for wlan_common::Phy {
    fn from(arg: PhyArg) -> Self {
        match arg {
            PhyArg::Erp => wlan_common::Phy::Erp,
            PhyArg::Ht => wlan_common::Phy::Ht,
            PhyArg::Vht => wlan_common::Phy::Vht,
        }
    }
}

impl ::std::convert::From<CbwArg> for wlan_common::Cbw {
    fn from(arg: CbwArg) -> Self {
        match arg {
            CbwArg::Cbw20 => wlan_common::Cbw::Cbw20,
            CbwArg::Cbw40 => wlan_common::Cbw::Cbw40,
            CbwArg::Cbw80 => wlan_common::Cbw::Cbw80,
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

#[derive(StructOpt, Debug)]
pub enum Opt {
    #[structopt(name = "phy")]
    /// commands for wlan phy devices
    Phy(PhyCmd),

    #[structopt(name = "iface")]
    /// commands for wlan iface devices
    Iface(IfaceCmd),

    /// commands for client stations
    #[structopt(name = "client")]
    Client(ClientCmd),
    #[structopt(name = "connect")]
    Connect(ClientConnectCmd),
    #[structopt(name = "disconnect")]
    Disconnect(ClientDisconnectCmd),
    #[structopt(name = "scan")]
    Scan(ClientScanCmd),
    #[structopt(name = "status")]
    Status(ClientStatusCmd),

    #[structopt(name = "ap")]
    /// commands for AP stations
    Ap(ApCmd),

    #[structopt(name = "mesh")]
    /// commands for mesh stations
    Mesh(MeshCmd),
}

#[derive(StructOpt, Copy, Clone, Debug)]
pub enum PhyCmd {
    #[structopt(name = "list")]
    /// lists phy devices
    List,
    #[structopt(name = "query")]
    /// queries a phy device
    Query {
        #[structopt(raw(required = "true"))]
        /// id of the phy to query
        phy_id: u16,
    },
}

#[derive(StructOpt, Clone, Debug)]
pub enum IfaceCmd {
    #[structopt(name = "new")]
    /// creates a new iface device
    New {
        #[structopt(short = "p", long = "phy", raw(required = "true"))]
        /// id of the phy that will host the iface
        phy_id: u16,

        #[structopt(
            short = "r",
            long = "role",
            raw(possible_values = "&RoleArg::variants()"),
            default_value = "Client",
            raw(case_insensitive = "true")
        )]
        /// role of the new iface
        role: RoleArg,
    },

    #[structopt(name = "del")]
    /// destroys an iface device
    Delete {
        #[structopt(short = "p", long = "phy", raw(required = "true"))]
        /// id of the phy that hosts the iface
        phy_id: u16,

        #[structopt(raw(required = "true"))]
        /// iface id to destroy
        iface_id: u16,
    },

    #[structopt(name = "list")]
    List,
    #[structopt(name = "query")]
    Query {
        #[structopt(raw(required = "true"))]
        iface_id: u16,
    },
    #[structopt(name = "stats")]
    Stats { iface_id: Option<u16> },
    #[structopt(name = "minstrel")]
    Minstrel(MinstrelCmd),
}

#[derive(StructOpt, Clone, Debug)]
pub enum MinstrelCmd {
    #[structopt(name = "list")]
    List { iface_id: Option<u16> },
    #[structopt(name = "show")]
    Show { iface_id: Option<u16>, peer_addr: Option<String> },
}

#[derive(StructOpt, Clone, Debug)]
pub struct ClientConnectCmd {
    #[structopt(short = "i", long = "iface", default_value = "0")]
    pub iface_id: u16,
    #[structopt(short = "p", long = "password", help = "WPA2 PSK")]
    pub password: Option<String>,
    #[structopt(short = "hash", long = "hash", help = "WPA2 PSK as hex string")]
    pub psk: Option<String>,
    #[structopt(
        short = "y",
        long = "phy",
        raw(possible_values = "&PhyArg::variants()"),
        raw(case_insensitive = "true"),
        help = "Specify an upper bound"
    )]
    pub phy: Option<PhyArg>,
    #[structopt(
        short = "w",
        long = "cbw",
        raw(possible_values = "&CbwArg::variants()"),
        raw(case_insensitive = "true"),
        help = "Specify an upper bound"
    )]
    pub cbw: Option<CbwArg>,
    #[structopt(
        short = "s",
        long = "scan-type",
        default_value = "passive",
        raw(possible_values = "&ScanTypeArg::variants()"),
        raw(case_insensitive = "true"),
        help = "Experimental. Default scan type on each channel. \
                Behavior may differ on DFS channel"
    )]
    pub scan_type: ScanTypeArg,
    #[structopt(raw(required = "true"))]
    pub ssid: String,
}

#[derive(StructOpt, Clone, Debug)]
pub struct ClientDisconnectCmd {
    #[structopt(short = "i", long = "iface", default_value = "0")]
    pub iface_id: u16,
}

#[derive(StructOpt, Clone, Debug)]
pub struct ClientScanCmd {
    #[structopt(short = "i", long = "iface", default_value = "0")]
    pub iface_id: u16,
    #[structopt(
        short = "s",
        long = "scan-type",
        default_value = "passive",
        raw(possible_values = "&ScanTypeArg::variants()"),
        raw(case_insensitive = "true"),
        help = "Experimental. Default scan type on each channel. \
                Behavior may differ on DFS channel"
    )]
    pub scan_type: ScanTypeArg,
}

#[derive(StructOpt, Clone, Debug)]
pub struct ClientStatusCmd {
    #[structopt(short = "i", long = "iface", default_value = "0")]
    pub iface_id: u16,
}

#[derive(StructOpt, Clone, Debug)]
pub enum ClientCmd {
    #[structopt(name = "scan")]
    Scan(ClientScanCmd),
    #[structopt(name = "connect")]
    Connect(ClientConnectCmd),
    #[structopt(name = "disconnect")]
    Disconnect(ClientDisconnectCmd),
    #[structopt(name = "status")]
    Status(ClientStatusCmd),
}

#[derive(StructOpt, Clone, Debug)]
pub enum ApCmd {
    #[structopt(name = "start")]
    Start {
        #[structopt(short = "i", long = "iface", default_value = "0")]
        iface_id: u16,
        #[structopt(short = "s", long = "ssid")]
        ssid: String,
        #[structopt(short = "p", long = "password")]
        password: Option<String>,
        #[structopt(short = "c", long = "channel")]
        // TODO(porce): Expand to support PHY and CBW
        channel: u8,
    },
    #[structopt(name = "stop")]
    Stop {
        #[structopt(short = "i", long = "iface", default_value = "0")]
        iface_id: u16,
    },
}

#[derive(StructOpt, Clone, Debug)]
pub enum MeshCmd {
    #[structopt(name = "join")]
    Join {
        #[structopt(short = "i", long = "iface", default_value = "0")]
        iface_id: u16,
        #[structopt(short = "m", long = "mesh_id")]
        mesh_id: String,
        #[structopt(short = "c", long = "channel")]
        // TODO(porce): Expand to support PHY and CBW
        channel: u8,
    },
    #[structopt(name = "leave")]
    Leave {
        #[structopt(short = "i", long = "iface", default_value = "0")]
        iface_id: u16,
    },
    #[structopt(name = "paths")]
    Paths {
        #[structopt(short = "i", long = "iface", default_value = "0")]
        iface_id: u16,
    },
}
