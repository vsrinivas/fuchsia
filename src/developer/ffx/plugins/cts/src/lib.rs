// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    ffx_core::ffx_plugin,
    ffx_cts_args::{Args, CtsCommand},
};

#[ffx_plugin("cts.experimental")]
pub async fn cts(cmd: CtsCommand) -> Result<()> {
    process_command(cmd.command)
}

fn process_command(cmd: Args) -> Result<()> {
    match cmd {
        Args::Run(_run_cmd) => {
            println!("Run: WIP");
        }
    }
    Ok(())
}
