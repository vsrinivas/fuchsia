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
    process_command(cmd.command, &mut std::io::stdout())
}

fn process_command<W: std::io::Write>(cmd: Args, writer: &mut W) -> Result<()> {
    match cmd {
        Args::Run(_run_cmd) => {
            writeln!(writer, "Run: WIP")?;
        }
    }
    Ok(())
}
