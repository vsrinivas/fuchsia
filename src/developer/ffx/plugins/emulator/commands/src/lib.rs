// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This library contains the shared functions that are specific to commands.

use anyhow::{anyhow, Context, Result};
use ffx_emulator_common::instances::{get_all_instances, get_instance_dir};
use ffx_emulator_config::EmulatorEngine;
use ffx_emulator_engines::serialization::read_from_disk;

pub async fn get_engine_by_name(name: &mut Option<String>) -> Result<Box<dyn EmulatorEngine>> {
    if name.is_none() {
        let mut all_instances = match get_all_instances().await {
            Ok(list) => list,
            Err(e) => {
                return Err(anyhow!("Error encountered looking up emulator instances: {:?}", e))
            }
        };
        if all_instances.len() == 1 {
            *name = all_instances.pop();
        } else if all_instances.len() == 0 {
            return Err(anyhow!("No emulators are running."));
        } else {
            return Err(anyhow!(
                "Multiple emulators are running. Indicate which emulator to access\n\
                by specifying the emulator name with your command.\n\
                See all the emulators available using `ffx emu list`."
            ));
        }
    }

    // If we got this far, name is set to either what the user asked for, or the only one running.
    let local_name = name.clone().unwrap();
    let instance_dir = get_instance_dir(&local_name, false).await?;
    if !instance_dir.exists() {
        Err(anyhow!(
            "{:?} isn't a valid instance. Please check your spelling and try again. \
                You can use `ffx emu list` to see currently available instances.",
            local_name
        ))
    } else {
        read_from_disk(&instance_dir).context("Couldn't read the emulator information from disk.")
    }
}
