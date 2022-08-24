// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use errors::ffx_bail;
use ffx_core::ffx_plugin;
use ffx_emulator_commands::get_engine_by_name;
use ffx_emulator_show_args::ShowCommand;

#[ffx_plugin()]
pub async fn show(mut cmd: ShowCommand) -> Result<()> {
    match get_engine_by_name(&mut cmd.name).await {
        Ok(engine) => {
            engine.show();
        }
        Err(e) => {
            ffx_bail!("{:?}", e);
        }
    };
    Ok(())
}
