// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use ffx_core::ffx_plugin;
use ffx_emulator_common::config::FfxConfigWrapper;
use ffx_emulator_engines::{get_instance_dir, serialization::read_from_disk};
use ffx_emulator_show_args::ShowCommand;

#[ffx_plugin("emu.experimental")]
pub async fn show(cmd: ShowCommand) -> Result<()> {
    let ffx_config = FfxConfigWrapper::new();
    let name = cmd.name;
    let instance_dir = get_instance_dir(&ffx_config, &name, false).await?;
    if !instance_dir.exists() {
        println!(
            "{} isn't a valid instance. Please check your spelling and try again. \
                You can use `ffx emu list` to see currently available instances.",
            name
        );
    } else {
        let engine = read_from_disk(&instance_dir).await;
        if let Ok(engine) = engine {
            engine.show();
        } else {
            println!(
                "Couldn't read the emulator information from disk. \
                    The file was likely corrupted or removed."
            )
        }
    }
    Ok(())
}
