// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::context::LowpanCtlContext;
use anyhow::{format_err, Context as _, Error};
use argh::FromArgs;

/// Contains the arguments decoded for the `list` command.
#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "list")]
pub struct ListCommand {}

impl ListCommand {
    pub async fn exec(&self, context: &mut LowpanCtlContext) -> Result<(), Error> {
        let lookup = &context.lookup;
        let device_names = lookup.get_devices().await.context("Unable to list devices")?;

        if device_names.is_empty() {
            Err(format_err!("No LoWPAN interfaces present"))
        } else {
            for name in device_names {
                println!("{}", name);
            }
            Ok(())
        }
    }
}
