// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command, wlan_dev};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "dev", description = "Controls WLAN core.")]
pub struct DevCommand {
    #[argh(subcommand)]
    pub subcommand: DevSubcommand,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand)]
pub enum DevSubcommand {
    Phy(PhyCommand),
    Iface(IfaceCommand),
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "phy", description = "Configure WLAN PHY device.")]
pub struct PhyCommand {
    #[argh(subcommand)]
    pub subcommand: PhySubcommand,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand)]
pub enum PhySubcommand {
    List(PhyList),
    Query(PhyQuery),
    GetCountry(GetCountry),
    SetCountry(SetCountry),
    ClearCountry(ClearCountry),
    SetPsMode(SetPsMode),
    GetPsMode(GetPsMode),
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "iface",
    description = "Create, destroy, and query the states of WLAN interfaces."
)]
pub struct IfaceCommand {
    #[argh(subcommand)]
    pub subcommand: IfaceSubcommand,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand)]
pub enum IfaceSubcommand {
    New(NewIface),
    Delete(Delete),
    List(IfaceList),
    Query(IfaceQuery),
    Stats(Stats),
    Minstrel(Minstrel),
    Status(Status),
}

impl From<PhySubcommand> for wlan_dev::opts::Opt {
    fn from(cmd: PhySubcommand) -> Self {
        wlan_dev::opts::Opt::Phy(match cmd {
            PhySubcommand::List(arg) => wlan_dev::opts::PhyCmd::from(arg),
            PhySubcommand::Query(arg) => wlan_dev::opts::PhyCmd::from(arg),
            PhySubcommand::GetCountry(arg) => wlan_dev::opts::PhyCmd::from(arg),
            PhySubcommand::SetCountry(arg) => wlan_dev::opts::PhyCmd::from(arg),
            PhySubcommand::ClearCountry(arg) => wlan_dev::opts::PhyCmd::from(arg),
            PhySubcommand::SetPsMode(arg) => wlan_dev::opts::PhyCmd::from(arg),
            PhySubcommand::GetPsMode(arg) => wlan_dev::opts::PhyCmd::from(arg),
        })
    }
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "list", description = "List WLAN PHYs")]
pub struct PhyList {}

impl From<PhyList> for wlan_dev::opts::PhyCmd {
    fn from(_: PhyList) -> Self {
        wlan_dev::opts::PhyCmd::List
    }
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "query", description = "Query WLAN PHY properties by ID.")]
pub struct PhyQuery {
    #[argh(positional, description = "PHY ID to query")]
    phy_id: u16,
}

impl From<PhyQuery> for wlan_dev::opts::PhyCmd {
    fn from(phy_query: PhyQuery) -> Self {
        wlan_dev::opts::PhyCmd::Query { phy_id: phy_query.phy_id }
    }
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "get-country", description = "Query PHY country code by ID.")]
pub struct GetCountry {
    #[argh(positional, description = "PHY ID to query")]
    phy_id: u16,
}

impl From<GetCountry> for wlan_dev::opts::PhyCmd {
    fn from(cmd: GetCountry) -> Self {
        wlan_dev::opts::PhyCmd::GetCountry { phy_id: cmd.phy_id }
    }
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "set-country", description = "Set country code for target PHY.")]
pub struct SetCountry {
    #[argh(positional, description = "PHY ID on which country code should be set.")]
    phy_id: u16,
    #[argh(positional, description = "two-character country code to apply to target PHY.")]
    country: String,
}

impl From<SetCountry> for wlan_dev::opts::PhyCmd {
    fn from(cmd: SetCountry) -> Self {
        wlan_dev::opts::PhyCmd::SetCountry { phy_id: cmd.phy_id, country: cmd.country }
    }
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "clear-country", description = "Clear country code on target PHY.")]
pub struct ClearCountry {
    #[argh(positional, description = "PHY ID where country code should be cleared.")]
    phy_id: u16,
}

impl From<ClearCountry> for wlan_dev::opts::PhyCmd {
    fn from(cmd: ClearCountry) -> Self {
        wlan_dev::opts::PhyCmd::ClearCountry { phy_id: cmd.phy_id }
    }
}

#[derive(PartialEq, Debug)]
pub enum PsModeArg {
    PsModeOff,
    PsModeFast,
    PsModePsPoll,
}

impl std::str::FromStr for PsModeArg {
    type Err = anyhow::Error;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s.to_lowercase().as_str() {
            "psmodeoff" => Ok(PsModeArg::PsModeOff),
            "psmodefast" => Ok(PsModeArg::PsModeFast),
            "psmodepspoll" => Ok(PsModeArg::PsModePsPoll),
            other => Err(anyhow::format_err!("could not parse PsModeArg: {}", other)),
        }
    }
}

impl From<PsModeArg> for wlan_dev::opts::PsModeArg {
    fn from(arg: PsModeArg) -> Self {
        match arg {
            PsModeArg::PsModeOff => wlan_dev::opts::PsModeArg::PsModeOff,
            PsModeArg::PsModeFast => wlan_dev::opts::PsModeArg::PsModeFast,
            PsModeArg::PsModePsPoll => wlan_dev::opts::PsModeArg::PsModePsPoll,
        }
    }
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "set-ps-mode",
    description = "Sets the power save state of the target PHY."
)]
pub struct SetPsMode {
    #[argh(positional, description = "PHY ID on which power save mode should be set.")]
    phy_id: u16,
    #[argh(positional, description = "desired power save mode.")]
    mode: PsModeArg,
}

impl From<SetPsMode> for wlan_dev::opts::PhyCmd {
    fn from(cmd: SetPsMode) -> Self {
        wlan_dev::opts::PhyCmd::SetPsMode {
            phy_id: cmd.phy_id,
            mode: wlan_dev::opts::PsModeArg::from(cmd.mode),
        }
    }
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "get-ps-mode", description = "Query power save mode of target PHY.")]
pub struct GetPsMode {
    #[argh(positional, description = "PHY ID on which power save mode should be set.")]
    phy_id: u16,
}

impl From<GetPsMode> for wlan_dev::opts::PhyCmd {
    fn from(cmd: GetPsMode) -> Self {
        wlan_dev::opts::PhyCmd::GetPsMode { phy_id: cmd.phy_id }
    }
}

impl From<DevSubcommand> for wlan_dev::opts::Opt {
    fn from(cmd: DevSubcommand) -> Self {
        match cmd {
            DevSubcommand::Phy(phy_cmd) => wlan_dev::opts::Opt::from(phy_cmd.subcommand),
            DevSubcommand::Iface(iface_cmd) => wlan_dev::opts::Opt::from(iface_cmd.subcommand),
        }
    }
}

impl From<IfaceSubcommand> for wlan_dev::opts::Opt {
    fn from(cmd: IfaceSubcommand) -> Self {
        wlan_dev::opts::Opt::Iface(match cmd {
            IfaceSubcommand::New(arg) => wlan_dev::opts::IfaceCmd::from(arg),
            IfaceSubcommand::Delete(arg) => wlan_dev::opts::IfaceCmd::from(arg),
            IfaceSubcommand::List(arg) => wlan_dev::opts::IfaceCmd::from(arg),
            IfaceSubcommand::Query(arg) => wlan_dev::opts::IfaceCmd::from(arg),
            IfaceSubcommand::Stats(arg) => wlan_dev::opts::IfaceCmd::from(arg),
            IfaceSubcommand::Minstrel(arg) => wlan_dev::opts::IfaceCmd::from(arg),
            IfaceSubcommand::Status(arg) => wlan_dev::opts::IfaceCmd::from(arg),
        })
    }
}

#[derive(PartialEq, Debug)]
pub enum RoleArg {
    Client,
    Ap,
}

impl std::str::FromStr for RoleArg {
    type Err = anyhow::Error;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s.to_lowercase().as_str() {
            "client" => Ok(RoleArg::Client),
            "ap" => Ok(RoleArg::Ap),
            other => Err(anyhow::format_err!("could not parse PsModeArg: {}", other)),
        }
    }
}

impl From<RoleArg> for wlan_dev::opts::RoleArg {
    fn from(arg: RoleArg) -> Self {
        match arg {
            RoleArg::Client => wlan_dev::opts::RoleArg::Client,
            RoleArg::Ap => wlan_dev::opts::RoleArg::Ap,
        }
    }
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "new", description = "Create a new WLAN interface.")]
pub struct NewIface {
    #[argh(
        option,
        short = 'p',
        long = "phy",
        description = "PHY ID on which to create the interface."
    )]
    phy_id: u16,
    #[argh(option, short = 'r', long = "role", description = "MAC role for the new interface.")]
    role: RoleArg,
    #[argh(option, short = 'm', description = "MAC address for the new interface.")]
    sta_addr: Option<String>,
}

impl From<NewIface> for wlan_dev::opts::IfaceCmd {
    fn from(new: NewIface) -> Self {
        wlan_dev::opts::IfaceCmd::New {
            phy_id: new.phy_id,
            role: wlan_dev::opts::RoleArg::from(new.role),
            sta_addr: new.sta_addr,
        }
    }
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "del", description = "Destroy a WLAN interface.")]
pub struct Delete {
    #[argh(positional, description = "interface ID to destroy.")]
    iface_id: u16,
}

impl From<Delete> for wlan_dev::opts::IfaceCmd {
    fn from(del: Delete) -> Self {
        wlan_dev::opts::IfaceCmd::Delete { iface_id: del.iface_id }
    }
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "list", description = "Lists all interface IDs.")]
pub struct IfaceList {}

impl From<IfaceList> for wlan_dev::opts::IfaceCmd {
    fn from(_: IfaceList) -> Self {
        wlan_dev::opts::IfaceCmd::List
    }
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "query", description = "Query the properties of a WLAN interface.")]
pub struct IfaceQuery {
    #[argh(positional, description = "interface ID to query.")]
    iface_id: u16,
}

impl From<IfaceQuery> for wlan_dev::opts::IfaceCmd {
    fn from(query: IfaceQuery) -> Self {
        wlan_dev::opts::IfaceCmd::Query { iface_id: query.iface_id }
    }
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "stats", description = "Query WLAN interface stats.")]
pub struct Stats {
    #[argh(
        positional,
        description = "interface ID to display stats from, if no ID is provided, all interfaces are queried."
    )]
    iface_id: Option<u16>,
}

impl From<Stats> for wlan_dev::opts::IfaceCmd {
    fn from(stats: Stats) -> Self {
        wlan_dev::opts::IfaceCmd::Stats { iface_id: stats.iface_id }
    }
}

// The ffx minstrel command differs from the original definition provided by wlan-dev.  While
// structopt allows for multiple optional positional arguments, argh does not.  To provide a
// similar experience, the `show` arguments have been converted into options instead of
// positionals.
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand)]
pub enum MinstrelSubcommand {
    List(MinstrelList),
    Show(Show),
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "list", description = "List minstrel peers.")]
pub struct MinstrelList {
    #[argh(
        positional,
        description = "interface ID to query, if no ID is provided, all interfaces are queried."
    )]
    iface_id: Option<u16>,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "show", description = "Show stats for minstrel peers.")]
pub struct Show {
    #[argh(
        option,
        short = 'i',
        long = "iface",
        description = "interface ID to query, if no ID is provided, all interfaces are queried."
    )]
    iface_id: Option<u16>,
    #[argh(
        option,
        short = 'p',
        long = "peer",
        description = "target peer address, if none is provided, all will be shown."
    )]
    peer_addr: Option<String>,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "minstrel", description = "Query interface minstrel stats.")]
pub struct Minstrel {
    #[argh(subcommand)]
    pub subcommand: MinstrelSubcommand,
}

impl From<Minstrel> for wlan_dev::opts::IfaceCmd {
    fn from(minstrel: Minstrel) -> Self {
        match minstrel.subcommand {
            MinstrelSubcommand::List(list) => {
                wlan_dev::opts::IfaceCmd::Minstrel(wlan_dev::opts::MinstrelCmd::List {
                    iface_id: list.iface_id,
                })
            }
            MinstrelSubcommand::Show(show) => {
                wlan_dev::opts::IfaceCmd::Minstrel(wlan_dev::opts::MinstrelCmd::Show {
                    iface_id: show.iface_id,
                    peer_addr: show.peer_addr,
                })
            }
        }
    }
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "status", description = "Query status of the target interface.")]
pub struct Status {
    #[argh(option, short = 'i', long = "iface", description = "iface ID to query status on.")]
    iface_id: Option<u16>,
}

impl From<Status> for wlan_dev::opts::IfaceCmd {
    fn from(status: Status) -> Self {
        wlan_dev::opts::IfaceCmd::Status(wlan_dev::opts::IfaceStatusCmd {
            iface_id: status.iface_id,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_phy_list_conversion() {
        assert_eq!(wlan_dev::opts::PhyCmd::from(PhyList {}), wlan_dev::opts::PhyCmd::List);
    }

    #[test]
    fn test_phy_query_conversion() {
        assert_eq!(
            wlan_dev::opts::PhyCmd::from(PhyQuery { phy_id: 123 }),
            wlan_dev::opts::PhyCmd::Query { phy_id: 123 }
        );
    }

    #[test]
    fn test_get_country_conversion() {
        assert_eq!(
            wlan_dev::opts::PhyCmd::from(GetCountry { phy_id: 123 }),
            wlan_dev::opts::PhyCmd::GetCountry { phy_id: 123 }
        );
    }

    #[test]
    fn test_set_country_conversion() {
        assert_eq!(
            wlan_dev::opts::PhyCmd::from(SetCountry { phy_id: 123, country: "US".to_string() }),
            wlan_dev::opts::PhyCmd::SetCountry { phy_id: 123, country: "US".to_string() }
        );
    }

    #[test]
    fn test_clear_conversion() {
        assert_eq!(
            wlan_dev::opts::PhyCmd::from(ClearCountry { phy_id: 123 }),
            wlan_dev::opts::PhyCmd::ClearCountry { phy_id: 123 }
        );
    }

    #[test]
    fn test_set_ps_mode_conversion() {
        assert_eq!(
            wlan_dev::opts::PhyCmd::from(SetPsMode { phy_id: 123, mode: PsModeArg::PsModeOff }),
            wlan_dev::opts::PhyCmd::SetPsMode {
                phy_id: 123,
                mode: wlan_dev::opts::PsModeArg::PsModeOff
            }
        );
    }

    #[test]
    fn test_get_ps_mode_conversion() {
        assert_eq!(
            wlan_dev::opts::PhyCmd::from(GetPsMode { phy_id: 123 }),
            wlan_dev::opts::PhyCmd::GetPsMode { phy_id: 123 }
        );
    }

    #[test]
    fn test_iface_list_conversion() {
        assert_eq!(wlan_dev::opts::IfaceCmd::from(IfaceList {}), wlan_dev::opts::IfaceCmd::List);
    }

    #[test]
    fn test_iface_new_conversion() {
        assert_eq!(
            wlan_dev::opts::IfaceCmd::from(NewIface {
                phy_id: 123,
                role: RoleArg::Client,
                sta_addr: Some(String::from("test"))
            }),
            wlan_dev::opts::IfaceCmd::New {
                phy_id: 123,
                role: wlan_dev::opts::RoleArg::Client,
                sta_addr: Some(String::from("test"))
            }
        );
    }

    #[test]
    fn test_iface_delete_conversion() {
        assert_eq!(
            wlan_dev::opts::IfaceCmd::from(Delete { iface_id: 123 }),
            wlan_dev::opts::IfaceCmd::Delete { iface_id: 123 }
        );
    }

    #[test]
    fn test_iface_query_conversion() {
        assert_eq!(
            wlan_dev::opts::IfaceCmd::from(IfaceQuery { iface_id: 123 }),
            wlan_dev::opts::IfaceCmd::Query { iface_id: 123 }
        );
    }

    #[test]
    fn test_iface_stats_conversion() {
        assert_eq!(
            wlan_dev::opts::IfaceCmd::from(Stats { iface_id: Some(123) }),
            wlan_dev::opts::IfaceCmd::Stats { iface_id: Some(123) }
        );
    }

    #[test]
    fn test_iface_minstrel_list_conversion() {
        assert_eq!(
            wlan_dev::opts::IfaceCmd::from(Minstrel {
                subcommand: MinstrelSubcommand::List(MinstrelList { iface_id: Some(123) })
            }),
            wlan_dev::opts::IfaceCmd::Minstrel({
                wlan_dev::opts::MinstrelCmd::List { iface_id: Some(123) }
            })
        );
    }

    #[test]
    fn test_iface_minstrel_show_conversion() {
        assert_eq!(
            wlan_dev::opts::IfaceCmd::from(Minstrel {
                subcommand: MinstrelSubcommand::Show(Show {
                    iface_id: Some(123),
                    peer_addr: Some(String::from("peer addr")),
                })
            }),
            wlan_dev::opts::IfaceCmd::Minstrel({
                wlan_dev::opts::MinstrelCmd::Show {
                    iface_id: Some(123),
                    peer_addr: Some(String::from("peer addr")),
                }
            })
        );
    }

    #[test]
    fn test_iface_status_conversion() {
        assert_eq!(
            wlan_dev::opts::IfaceCmd::from(Status { iface_id: Some(123) }),
            wlan_dev::opts::IfaceCmd::Status(wlan_dev::opts::IfaceStatusCmd {
                iface_id: Some(123)
            })
        );
    }
}
