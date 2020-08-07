// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::context::LowpanCtlContext;
use crate::invocation::CommandEnum;
use anyhow::{format_err, Error};
use argh::FromArgs;
use fuchsia_async::{Time, Timer};
use fuchsia_zircon::Duration;

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "repeat", description = "repeats the given command")]
pub struct RepeatCommand {
    #[argh(option, short = 'C', description = "repeat count", default = "0")]
    pub count: u32,

    #[argh(option, short = 'i', description = "repeat wait interval, in seconds", default = "1.0")]
    pub wait: f64,

    #[argh(subcommand)]
    pub command: CommandEnum,
}

impl RepeatCommand {
    pub async fn exec(&self, context: &mut LowpanCtlContext) -> Result<(), Error> {
        if self.wait < 0.0 {
            return Err(format_err!("Wait value must be positive"));
        }

        let wait_duration = Duration::from_nanos(
            (self.wait * (Duration::from_seconds(1).into_nanos() as f64)) as i64,
        );

        let mut count = self.count;

        loop {
            self.command.exec(context).await?;

            if count != 0 {
                if count == 1 {
                    // This is the last iteration.
                    break Ok(());
                }
                count -= 1;
            }

            if self.wait > 0.0 {
                // Wait a little bit until we execute the command again.
                Timer::new(Time::after(wait_duration)).await;
            }
        }
    }
}
