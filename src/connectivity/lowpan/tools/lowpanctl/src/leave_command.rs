// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::context::LowpanCtlContext;
use crate::prelude::*;

/// Contains the arguments decoded for the `leave` command.
#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "leave")]
pub struct LeaveCommand {}

impl LeaveCommand {
    pub async fn exec(&self, context: &mut LowpanCtlContext) -> Result<(), Error> {
        let device = context.get_default_device().await.context("Unable to get device instance")?;

        device.leave_network().await.context("Unable to send leave command")?;

        Ok(())
    }
}
