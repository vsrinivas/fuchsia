// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::context::LowpanCtlContext;
use anyhow::{Context as _, Error};
use argh::FromArgs;

/// Contains the arguments decoded for the `set-active` command.
#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "set-active")]
pub struct SetActiveCommand {
    /// name
    #[argh(positional)]
    pub is_active: bool,
}

impl SetActiveCommand {
    pub async fn exec(&self, context: &mut LowpanCtlContext) -> Result<(), Error> {
        let device = context.get_default_device().await.context("Unable to get device instance")?;

        device.set_active(self.is_active).await.context("Unable to send set_active command")?;

        Ok(())
    }
}
