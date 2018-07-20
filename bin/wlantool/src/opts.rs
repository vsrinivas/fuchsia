// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(deprecated)] // Necessary for AsciiExt usage from clap args_enum macro

use wlan;

arg_enum!{
    #[derive(PartialEq, Copy, Clone, Debug)]
    pub enum RoleArg {
        Client,
        Ap
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

#[derive(StructOpt, Debug)]
pub enum Opt {
    #[structopt(name = "phy")]
    /// commands for wlan phy devices
    Phy(PhyCmd),

    #[structopt(name = "iface")]
    /// commands for wlan iface devices
    Iface(IfaceCmd),

    #[structopt(name = "client")]
    /// commands for client stations
    Client(ClientCmd),
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

#[derive(StructOpt, Copy, Clone, Debug)]
pub enum IfaceCmd {
    #[structopt(name = "new")]
    /// creates a new iface device
    New {
        #[structopt(short = "p", long = "phy", raw(required = "true"))]
        /// id of the phy that will host the iface
        phy_id: u16,

        #[structopt(short = "r", long = "role", raw(possible_values = "&RoleArg::variants()"),
                    default_value = "Client", raw(case_insensitive = "true"))]
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
}

#[derive(StructOpt, Clone, Debug)]
pub enum ClientCmd {
    #[structopt(name = "scan")]
    Scan {
        #[structopt(raw(required = "true"))]
        iface_id: u16
    },
    #[structopt(name = "connect")]
    Connect {
        #[structopt(raw(required = "true"))]
        iface_id: u16,
        #[structopt(raw(required = "true"))]
        ssid: String,
        #[structopt(short = "p", long = "password")]
        password: String
    },
    #[structopt(name = "status")]
    Status {
        #[structopt(raw(required = "true"))]
        iface_id: u16,
    }
}

