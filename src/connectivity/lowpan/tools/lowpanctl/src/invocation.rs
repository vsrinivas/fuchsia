// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use argh::FromArgs;

use crate::context::LowpanCtlContext;
pub use crate::leave_command::*;
pub use crate::list_command::*;
pub use crate::provision_command::*;
pub use crate::reset_command::*;
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
    pub command: CommandEnum,
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand)]
pub enum CommandEnum {
    Status(StatusCommand),
    Provision(ProvisionCommand),
    Leave(LeaveCommand),
    List(ListCommand),
    Reset(ResetCommand),
}

impl LowpanCtlInvocation {
    pub async fn exec(&self, context: &mut LowpanCtlContext) -> Result<(), Error> {
        match &self.command {
            CommandEnum::Status(x) => x.exec(context).await,
            CommandEnum::Provision(x) => x.exec(context).await,
            CommandEnum::Leave(x) => x.exec(context).await,
            CommandEnum::List(x) => x.exec(context).await,
            CommandEnum::Reset(x) => x.exec(context).await,
        }
    }
}
