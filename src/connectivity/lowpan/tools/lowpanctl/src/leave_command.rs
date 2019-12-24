// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::context::LowpanCtlContext;
use anyhow::Error;
use argh::FromArgs;

/// Contains the arguments decoded for the `leave` command.
#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "leave")]
pub struct LeaveCommand {}

impl LeaveCommand {
    pub async fn exec(&self, _context: &mut LowpanCtlContext) -> Result<(), Error> {
        println!("Command `leave` not yet implemented");
        Ok(())
    }
}
