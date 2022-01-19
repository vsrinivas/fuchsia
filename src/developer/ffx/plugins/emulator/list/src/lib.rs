// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use ffx_core::ffx_plugin;
use ffx_emulator_common::config::FfxConfigWrapper;
use ffx_emulator_engines::{get_all_instances, serialization::read_from_disk};
use ffx_emulator_list_args::ListCommand;

#[ffx_plugin("emu.experimental")]
pub async fn list(_cmd: ListCommand) -> Result<()> {
    let ffx_config = FfxConfigWrapper::new();
    let instance_list = get_all_instances(&ffx_config).await?;
    for entry in instance_list {
        if let Some(instance) = entry.as_path().file_name() {
            let name = instance.to_str().unwrap();
            let engine = read_from_disk(&entry)?;
            if engine.is_running() {
                println!("[Active]    {}", name);
            } else {
                println!("[Inactive]  {}", name);
            }
        }
    }
    Ok(())
}
