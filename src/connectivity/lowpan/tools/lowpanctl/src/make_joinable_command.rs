// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::context::LowpanCtlContext;
use crate::prelude::*;
use std::time::Duration;

/// Contains the arguments decoded for the `make-joinable` command.
#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "make-joinable")]
pub struct MakeJoinableCommand {
    /// period to remain joinable, in seconds. Set to zero to turn off.
    #[argh(option)]
    pub period: u32,

    /// local port to allow commissioning sessions on
    #[argh(option)]
    pub port: u16,
}

impl MakeJoinableCommand {
    pub async fn exec(&self, context: &mut LowpanCtlContext) -> Result<(), Error> {
        let legacy_joining = context.get_default_legacy_joining_proxy().await?;

        legacy_joining
            .make_joinable(
                Duration::from_secs(self.period as u64).as_nanos().try_into()?,
                self.port,
            )
            .await
            .context("Unable to send command")
    }
}
