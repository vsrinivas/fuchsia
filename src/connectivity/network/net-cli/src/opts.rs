// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use clap::arg_enum;
use fidl_fuchsia_net_stack::{self as netstack};
use structopt::StructOpt;

// Same as https://docs.rs/log/0.4.8/log/enum.Level.html
// but it can't be used for clap parsing here since it is external.
arg_enum! {
    #[derive(PartialEq, Copy, Clone, Debug)]
    pub enum LogLevelArg {
        // See syslog/logger.go for numeric definitions.
        Trace,
        Debug,
        Info ,
        Warn,
        Error,
        Fatal,
    }
}
impl ::std::convert::From<LogLevelArg> for netstack::LogLevelFilter {
    fn from(arg: LogLevelArg) -> Self {
        match arg {
            LogLevelArg::Trace => netstack::LogLevelFilter::Trace,
            LogLevelArg::Debug => netstack::LogLevelFilter::Debug,
            LogLevelArg::Info => netstack::LogLevelFilter::Info,
            LogLevelArg::Warn => netstack::LogLevelFilter::Warn,
            LogLevelArg::Error => netstack::LogLevelFilter::Error,
            LogLevelArg::Fatal => netstack::LogLevelFilter::Fatal,
        }
    }
}

#[derive(StructOpt, Debug)]
pub enum Opt {
    #[structopt(name = "if")]
    /// commands for network interfaces
    If(IfCmd),

    #[structopt(name = "fwd")]
    /// commands for forwarding tables
    Fwd(FwdCmd),

    #[structopt(name = "route")]
    /// commands for routing tables
    Route(RouteCmd),

    #[structopt(name = "filter")]
    /// commands for packet filter
    Filter(FilterCmd),

    #[structopt(name = "log")]
    /// commands for logging
    Log(LogCmd),
}

#[derive(StructOpt, Clone, Debug)]
pub enum IfCmd {
    #[structopt(name = "list")]
    /// lists network interfaces
    List {
        /// name substring to be matched
        name_pattern: Option<String>,
    },
    #[structopt(name = "add")]
    /// adds a network interface by path
    Add {
        #[structopt(raw(required = "true"))]
        // The path must yield a handle to a fuchsia.hardware.ethernet.Device interface.
        // Currently this means paths under /dev/class/ethernet.
        /// path to the device to add
        path: String,
    },
    #[structopt(name = "del")]
    /// removes a network interface
    Del {
        #[structopt(raw(required = "true"))]
        /// id of the network interface to remove
        id: u64,
    },
    #[structopt(name = "get")]
    /// queries a network interface
    Get {
        #[structopt(raw(required = "true"))]
        /// id of the network interface to query
        id: u64,
    },
    #[structopt(name = "enable")]
    /// enables a network interface
    Enable {
        #[structopt(raw(required = "true"))]
        /// id of the network interface to enable
        id: u64,
    },
    #[structopt(name = "disable")]
    /// disables a network interface
    Disable {
        #[structopt(raw(required = "true"))]
        /// id of the network interface to disable
        id: u64,
    },
    #[structopt(name = "addr")]
    /// commands for updating network interface addresses
    Addr(AddrCmd),
}

#[derive(StructOpt, Clone, Debug)]
pub enum AddrCmd {
    #[structopt(name = "add")]
    /// adds an address to the network interface
    Add {
        #[structopt(raw(required = "true"))]
        /// id of the network interface
        id: u64,
        #[structopt(raw(required = "true"))]
        addr: String,
        #[structopt(raw(required = "true"))]
        prefix: u8,
    },
    #[structopt(name = "del")]
    /// deletes an address from the network interface
    Del {
        #[structopt(raw(required = "true"))]
        /// id of the network interface
        id: u64,
        #[structopt(raw(required = "true"))]
        addr: String,
    },
}

#[derive(StructOpt, Clone, Debug)]
pub enum FwdCmd {
    #[structopt(name = "list")]
    /// lists forwarding table entries
    List,
    #[structopt(name = "add-device")]
    /// adds a forwarding table entry to route to a device
    AddDevice {
        #[structopt(raw(required = "true"))]
        /// id of the network interface to route to
        id: u64,
        #[structopt(raw(required = "true"))]
        /// address portion of the subnet for this forwarding rule
        addr: String,
        #[structopt(raw(required = "true"))]
        /// routing prefix for this forwarding rule
        prefix: u8,
    },
    #[structopt(name = "add-hop")]
    /// adds a forwarding table entry to route to a IP address
    AddHop {
        #[structopt(raw(required = "true"))]
        /// IP address of the next hop to route to
        next_hop: String,
        #[structopt(raw(required = "true"))]
        /// address portion of the subnet for this forwarding rule
        addr: String,
        #[structopt(raw(required = "true"))]
        /// routing prefix for this forwarding rule
        prefix: u8,
    },
    #[structopt(name = "del")]
    /// deletes a forwarding table entry
    Del {
        #[structopt(raw(required = "true"))]
        /// address portion of the subnet for this forwarding rule
        addr: String,
        #[structopt(raw(required = "true"))]
        /// routing prefix for this forwarding rule
        prefix: u8,
    },
}

#[derive(StructOpt, Clone, Debug)]
pub enum RouteCmd {
    #[structopt(name = "list")]
    List,
}

#[derive(StructOpt, Clone, Debug)]
pub enum FilterCmd {
    #[structopt(name = "enable")]
    /// enable the packet filter
    Enable,
    #[structopt(name = "disable")]
    /// disable the packet filter
    Disable,
    #[structopt(name = "is_enabled")]
    /// is the packet filter enabled?
    IsEnabled,
    #[structopt(name = "get_rules")]
    /// get filter rules
    GetRules,
    #[structopt(name = "set_rules")]
    /// set filter rules (see the netfilter::parser library for the rules format)
    SetRules { rules: String },
    #[structopt(name = "get_nat_rules")]
    /// get nat rules
    GetNatRules,
    #[structopt(name = "set_nat_rules")]
    /// set nat rules (see the netfilter::parser library for the NAT rules format)
    SetNatRules { rules: String },
    #[structopt(name = "get_rdr_rules")]
    /// get rdr rules
    GetRdrRules,
    #[structopt(name = "set_rdr_rules")]
    /// set rdr rules (see the netfilter::parser library for the RDR rules format)
    SetRdrRules { rules: String },
}

#[derive(StructOpt, Clone, Debug)]
pub enum LogCmd {
    /// Syslog severity level / loglevel
    #[structopt(name = "set-level")]
    SetLevel {
        #[structopt(
            raw(possible_values = "&LogLevelArg::variants()"),
            raw(case_insensitive = "true")
        )]
        log_level: LogLevelArg,
    },
}
