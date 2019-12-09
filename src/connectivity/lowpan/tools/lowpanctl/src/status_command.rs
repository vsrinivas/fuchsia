// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::context::LowpanCtlContext;
use argh::FromArgs;
use failure::Error;

/// Contains the arguments decoded for the `status` command.
#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "status")]
pub struct StatusCommand {}

impl StatusCommand {
    pub async fn exec(&self, _context: &mut LowpanCtlContext) -> Result<(), Error> {
        println!("Command `status` not yet implemented");
        Ok(())
    }
}
