// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use argh::FromArgs;

use crate::context::LowpanCtlContext;
pub use crate::energy_scan_command::*;
pub use crate::form_command::*;
pub use crate::get_credential::*;
pub use crate::get_supported_channels::*;
pub use crate::get_supported_network_types::*;
pub use crate::join_command::*;
pub use crate::leave_command::*;
pub use crate::list_command::*;
pub use crate::mfg_command::*;
pub use crate::network_scan_command::*;
pub use crate::provision_command::*;
pub use crate::repeat_command::*;
pub use crate::reset_command::*;
pub use crate::set_active_comamnd::*;
pub use crate::status_command::*;

/// This struct contains the arguments decoded from the command
/// line invocation of `lowpanctl`.
///
/// Currently, `command` is mandatory. This will change once
/// the interactive command-line mode has been implemented.
#[derive(FromArgs, PartialEq, Debug)]
pub struct LowpanCtlInvocation {
    #[argh(
        option,
        long = "server",
        description = "package URL",
        default = "\"fuchsia-pkg://fuchsia.com/lowpanservice#meta/lowpanservice.cmx\".to_string()"
    )]
    pub server_url: String,

    #[argh(option, long = "iface", description = "interface/device name")]
    pub device_name: Option<String>,

    #[argh(subcommand)]
    pub command: CommandEnumWithRepeat,
}

/// Enum containing all of the normal commands, INCLUDING the repeat command.
///
/// New commands must be added to both this enum and `CommandEnum`.
#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand)]
pub enum CommandEnumWithRepeat {
    Status(StatusCommand),
    Provision(ProvisionCommand),
    Leave(LeaveCommand),
    List(ListCommand),
    Reset(ResetCommand),
    Join(JoinCommand),
    Form(FormCommand),
    EnergyScan(EnergyScanCommand),
    NetworkScan(NetworkScanCommand),
    SetActive(SetActiveCommand),
    GetSupportedNetworkTypes(GetSupportedNetworkTypesCommand),
    GetSupportedChannels(GetSupportedChannelsCommand),
    GetCredential(GetCredentialCommand),
    Mfg(MfgCommand),
    Repeat(RepeatCommand),
}

impl CommandEnumWithRepeat {
    pub async fn exec(&self, context: &mut LowpanCtlContext) -> Result<(), Error> {
        match self {
            CommandEnumWithRepeat::Status(x) => x.exec(context).await,
            CommandEnumWithRepeat::Provision(x) => x.exec(context).await,
            CommandEnumWithRepeat::Leave(x) => x.exec(context).await,
            CommandEnumWithRepeat::List(x) => x.exec(context).await,
            CommandEnumWithRepeat::Reset(x) => x.exec(context).await,
            CommandEnumWithRepeat::Join(x) => x.exec(context).await,
            CommandEnumWithRepeat::Form(x) => x.exec(context).await,
            CommandEnumWithRepeat::EnergyScan(x) => x.exec(context).await,
            CommandEnumWithRepeat::NetworkScan(x) => x.exec(context).await,
            CommandEnumWithRepeat::Mfg(x) => x.exec(context).await,
            CommandEnumWithRepeat::SetActive(x) => x.exec(context).await,
            CommandEnumWithRepeat::GetSupportedNetworkTypes(x) => x.exec(context).await,
            CommandEnumWithRepeat::GetSupportedChannels(x) => x.exec(context).await,
            CommandEnumWithRepeat::GetCredential(x) => x.exec(context).await,
            CommandEnumWithRepeat::Repeat(x) => x.exec(context).await,
        }
    }
}

/// Enum containing all of the normal commands, EXCEPT the repeat command.
///
/// New commands must be added to both this enum and `CommandEnumWithRepeat`.
#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand)]
pub enum CommandEnum {
    Status(StatusCommand),
    Provision(ProvisionCommand),
    Leave(LeaveCommand),
    List(ListCommand),
    Reset(ResetCommand),
    Join(JoinCommand),
    Form(FormCommand),
    EnergyScan(EnergyScanCommand),
    NetworkScan(NetworkScanCommand),
    SetActive(SetActiveCommand),
    GetSupportedNetworkTypes(GetSupportedNetworkTypesCommand),
    GetSupportedChannels(GetSupportedChannelsCommand),
    GetCredential(GetCredentialCommand),
    Mfg(MfgCommand),
}

impl CommandEnum {
    pub async fn exec(&self, context: &mut LowpanCtlContext) -> Result<(), Error> {
        match self {
            CommandEnum::Status(x) => x.exec(context).await,
            CommandEnum::Provision(x) => x.exec(context).await,
            CommandEnum::Leave(x) => x.exec(context).await,
            CommandEnum::List(x) => x.exec(context).await,
            CommandEnum::Reset(x) => x.exec(context).await,
            CommandEnum::Join(x) => x.exec(context).await,
            CommandEnum::Form(x) => x.exec(context).await,
            CommandEnum::EnergyScan(x) => x.exec(context).await,
            CommandEnum::NetworkScan(x) => x.exec(context).await,
            CommandEnum::SetActive(x) => x.exec(context).await,
            CommandEnum::GetSupportedNetworkTypes(x) => x.exec(context).await,
            CommandEnum::GetSupportedChannels(x) => x.exec(context).await,
            CommandEnum::GetCredential(x) => x.exec(context).await,
            CommandEnum::Mfg(x) => x.exec(context).await,
        }
    }
}

impl LowpanCtlInvocation {
    pub async fn exec(&self, context: &mut LowpanCtlContext) -> Result<(), Error> {
        self.command.exec(context).await
    }
}
