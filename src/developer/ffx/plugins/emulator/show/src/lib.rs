// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Result};
use ffx_core::ffx_plugin;
use ffx_emulator_common::config::FfxConfigWrapper;
use ffx_emulator_engines::{get_instance_dir, serialization::read_from_disk};
use ffx_emulator_show_args::ShowCommand;

async fn show_internal(ffx_config: &FfxConfigWrapper, name: &str) -> Result<()> {
    let instance_dir = get_instance_dir(&ffx_config, &name, false).await?;
    if !instance_dir.exists() {
        Err(anyhow!(
            "{} isn't a valid instance. Please check your spelling and try again. \
                You can use `ffx emu list` to see currently available instances.",
            name
        ))
    } else {
        let engine = read_from_disk(&instance_dir).await;
        if let Ok(engine) = engine {
            engine.show();
            Ok(())
        } else {
            Err(anyhow!(
                "Couldn't read the emulator information from disk. \
                    The file was likely corrupted or removed."
            ))
        }
    }
}

#[ffx_plugin("emu.experimental")]
pub async fn show(cmd: ShowCommand) -> Result<()> {
    let ffx_config = FfxConfigWrapper::new();
    let name = cmd.name;

    if let Err(e) = show_internal(&ffx_config, &name).await {
        println!("{:?}", e);
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use ffx_emulator_common::config::EMU_INSTANCE_ROOT_DIR;
    use ffx_emulator_engines::TestEngineDoNotUseOutsideOfTests;
    use std::{
        fs::{create_dir_all, File},
        io::Write,
        path::PathBuf,
    };
    use tempfile::tempdir;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_show() -> Result<()> {
        let temp_dir = tempdir()
            .expect("Couldn't get a temporary directory for testing.")
            .path()
            .to_str()
            .expect("Couldn't convert Path to str")
            .to_string();
        let mut ffx_config = FfxConfigWrapper::new();
        ffx_config.overrides.insert(EMU_INSTANCE_ROOT_DIR, temp_dir.clone());

        // Test showing a non-existing directory.
        assert!(show_internal(&ffx_config, "test_dir").await.is_err());

        // Create the directory, but don't populate it.
        let instance_dir = PathBuf::from(&temp_dir).join("test_dir");
        create_dir_all(instance_dir.as_path())?;
        assert!(instance_dir.exists());
        assert!(show_internal(&ffx_config, "test_dir").await.is_err());

        // Create the engine.json file, but don't populate it.
        let file_path = instance_dir.join("engine.json");
        let mut file = File::create(&file_path)?;
        assert!(file_path.exists());
        assert!(show_internal(&ffx_config, "test_dir").await.is_err());

        // Generate an engine, put it in engine.json, then show it.
        let engine = TestEngineDoNotUseOutsideOfTests::default();
        let engine_text = serde_json::to_string(&engine)?;
        file.write(engine_text.as_bytes())?;
        file.flush()?;
        show_internal(&ffx_config, "test_dir").await
    }
}
