// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::context::LowpanCtlContext;
use crate::prelude::*;
use argh::{CommandInfo, EarlyExit, SubCommand};

#[derive(PartialEq, Debug)]
pub struct MfgCommand(String);

impl FromArgs for MfgCommand {
    fn from_args(_command_name: &[&str], args: &[&str]) -> Result<Self, EarlyExit> {
        Ok(MfgCommand(args.join(" ")))
    }
}

impl SubCommand for MfgCommand {
    const COMMAND: &'static CommandInfo =
        &CommandInfo { name: "mfg", description: "Send a manufacturing command to the NCP/RCP" };
}

impl MfgCommand {
    pub async fn exec(&self, context: &mut LowpanCtlContext) -> Result<(), Error> {
        let device_factory = context
            .get_default_device_factory()
            .await
            .context("Unable to get device factory instance")?;

        let result =
            device_factory.send_mfg_command(&self.0).await.context("Unable to send mfg command")?;

        println!("{}", result.trim());

        Ok(())
    }
}
