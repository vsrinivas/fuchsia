// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use errors::ffx_bail;
use ffx_core::ffx_plugin;
use ffx_emulator_commands::get_engine_by_name;
use ffx_emulator_common::instances::{clean_up_instance_dir, get_all_instances, get_instance_dir};
use ffx_emulator_stop_args::StopCommand;
use fidl_fuchsia_developer_ffx::TargetCollectionProxy;

#[ffx_plugin(TargetCollectionProxy = "daemon::protocol")]
pub async fn stop(cmd: StopCommand, proxy: TargetCollectionProxy) -> Result<()> {
    let mut names = vec![cmd.name];
    if cmd.all {
        names = match get_all_instances().await {
            Ok(list) => list.into_iter().map(|v| Some(v)).collect(),
            Err(e) => ffx_bail!("Error encountered looking up emulator instances: {:?}", e),
        };
    }
    for mut some_name in names {
        let engine = get_engine_by_name(&mut some_name).await;
        if engine.is_err() && some_name.is_none() {
            // This happens when the program doesn't know which instance to use. The
            // get_engine_by_name returns a good error message, and the loop should terminate
            // early.
            eprintln!("{:?}", engine.err().unwrap());
            break;
        }
        let name = some_name.unwrap_or("<unspecified>".to_string());
        if engine.is_err() {
            eprintln!(
                "{:?}",
                engine.err().unwrap().context(format!(
                    "Couldn't deserialize engine '{}' from disk. Continuing stop, \
                    but you may need to terminate the emulator process manually.",
                    name
                ))
            );
        } else {
            // Unwrap is safe because get_engine_by_name sets the some_name parameter if needed.
            println!("Stopping emulator '{}'...", name);
            if let Err(e) = engine.unwrap().stop(&proxy).await {
                eprintln!("Failed with the following error: {:?}", e);
            }
        }

        if !cmd.persist {
            if let Ok(path) = get_instance_dir(&name, false).await {
                let cleanup = clean_up_instance_dir(&path).await;
                if cleanup.is_err() {
                    eprintln!(
                        "Cleanup of '{}' failed with the following error: {:?}",
                        name,
                        cleanup.unwrap_err()
                    );
                }
            }
        }
    }
    Ok(())
}
