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
        }
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
}
