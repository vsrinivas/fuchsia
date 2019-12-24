// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use argh::FromArgs;

use crate::context::LowpanCtlContext;
pub use crate::leave_command::*;
pub use crate::provision_command::*;
pub use crate::status_command::*;

/// This struct contains the arguments decoded from the command
/// line invocation of `lowpanctl`.
///
/// Currently, `command` is mandatory. This will change once
/// the interactive command-line mode has been implemented.
#[derive(FromArgs, PartialEq, Debug)]
pub struct LowpanCtlInvocation {
    #[argh(subcommand)]
    command: CommandEnum,
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand)]
enum CommandEnum {
    Status(StatusCommand),
    Provision(ProvisionCommand),
    Leave(LeaveCommand),
}

impl LowpanCtlInvocation {
    pub async fn exec(&self, context: &mut LowpanCtlContext) -> Result<(), Error> {
        match &self.command {
            CommandEnum::Status(x) => x.exec(context).await,
            CommandEnum::Provision(x) => x.exec(context).await,
            CommandEnum::Leave(x) => x.exec(context).await,
        }
    }
}
