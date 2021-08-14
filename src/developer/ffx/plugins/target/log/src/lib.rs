// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    ffx_core::ffx_plugin,
    ffx_target_log_args::{LogCommand, LogSubCommand},
};

#[ffx_plugin("proactive_log.enabled")]
pub async fn log(cmd: LogCommand) -> Result<()> {
    match cmd.cmd {
        LogSubCommand::Watch(..) => {
            println!("`ffx target log watch` is now `ffx log`!");
        }
        LogSubCommand::Dump(..) => {
            println!("`ffx target log dump` is now `ffx log --dump`!");
        }
    }
    println!("Note: some parameters may have changed. See `ffx log --help` for details.");
    Ok(())
}
