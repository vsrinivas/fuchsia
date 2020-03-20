// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::context::LowpanCtlContext;
use anyhow::{Context as _, Error};
use argh::FromArgs;

/// Contains the arguments decoded for the `reset` command.
#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "reset")]
pub struct ResetCommand {}

impl ResetCommand {
    pub async fn exec(&self, context: &mut LowpanCtlContext) -> Result<(), Error> {
        let (_, _, device_test) =
            context.get_default_device_proxies().await.context("Unable to get device instance")?;

        device_test.reset().await.context("Unable to send reset command")?;

        Ok(())
    }
}
