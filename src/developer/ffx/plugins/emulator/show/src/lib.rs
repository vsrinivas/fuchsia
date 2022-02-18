// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Context, Result};
use errors::ffx_bail;
use ffx_core::ffx_plugin;
use ffx_emulator_common::config::FfxConfigWrapper;
use ffx_emulator_engines::{get_all_instances, get_instance_dir, serialization::read_from_disk};
use ffx_emulator_show_args::ShowCommand;

async fn show_internal(ffx_config: &FfxConfigWrapper, name: &str) -> Result<()> {
    let instance_dir = get_instance_dir(&ffx_config, &name, false).await?;
    if !instance_dir.exists() {
        Err(anyhow!(
            "{:?} isn't a valid instance. Please check your spelling and try again. \
                You can use `ffx emu list` to see currently available instances.",
            name
        ))
    } else {
        let engine = read_from_disk(&instance_dir)
            .context("Couldn't read the emulator information from disk.");
        match engine {
            Ok(engine) => {
                engine.show();
                Ok(())
            }
            Err(e) => Err(e),
        }
    }
}

async fn get_instance_name(
    instance_name: Option<String>,
    ffx_config: &FfxConfigWrapper,
) -> Result<String> {
    if instance_name.is_none() {
        let all_instances = get_all_instances(&ffx_config).await?;
        if all_instances.len() == 1 {
            if let Some(name) = all_instances[0].file_name() {
                Ok(name.to_string_lossy().into_owned())
            } else {
                // This should never happen.
                // filename() only returns None if the path ends in "..", which shouldn't be
                // possible to get from a call to read_dir() and also contain an engine.json file.
                Err(anyhow!(
                    "Paths ending in '..' are not valid emulator working directories. \
                    The path returned was {:?}.",
                    all_instances[0]
                ))
            }
        } else if all_instances.len() == 0 {
            Err(anyhow!("No emulators are running."))
        } else {
            Err(anyhow!(
                "Multiple emulators are running. \
                Indicate which emulator to show using `ffx emu show <name>`.\n\
                See all possible names using `ffx emu list`."
            ))
        }
    } else {
        Ok(instance_name.unwrap())
    }
}

#[ffx_plugin("emu.experimental")]
pub async fn show(cmd: ShowCommand) -> Result<()> {
    let ffx_config = FfxConfigWrapper::new();

    match get_instance_name(cmd.name, &ffx_config).await {
        Ok(name) => {
            if let Err(e) = show_internal(&ffx_config, &name).await {
                ffx_bail!("{:?}", e);
            }
        }
        Err(e) => {
            ffx_bail!("{:?}", e);
        }
    };

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

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_get_instance_name() -> Result<()> {
        let temp_dir = tempdir()
            .expect("Couldn't get a temporary directory for testing.")
            .path()
            .to_str()
            .expect("Couldn't convert Path to str")
            .to_string();
        let mut ffx_config = FfxConfigWrapper::new();
        ffx_config.overrides.insert(EMU_INSTANCE_ROOT_DIR, temp_dir.clone());

        // If a name is given on CLI, we just return that every time, regardless of existence.
        assert!(get_instance_name(Some("instance".to_string()), &ffx_config).await.is_ok());

        // If no instances exist, and no name is provided, it's an error.
        assert!(get_instance_name(None, &ffx_config).await.is_err());

        // If only one instance exists, that should be the value returned.
        let instance_dir = PathBuf::from(&temp_dir).join("test_dir1");
        create_dir_all(instance_dir.as_path())?;
        let file_path1 = instance_dir.join("engine.json");
        let _file1 = File::create(&file_path1)?;
        assert!(file_path1.exists());

        let result = get_instance_name(None, &ffx_config).await;
        assert!(result.is_ok(), "{:?}", result);
        assert_eq!(result.unwrap(), "test_dir1");

        // If more than one instance is up, and no name is provided, it's an error.
        let instance_dir = PathBuf::from(&temp_dir).join("test_dir2");
        create_dir_all(instance_dir.as_path())?;
        let file_path2 = instance_dir.join("engine.json");
        let _file2 = File::create(&file_path2)?;
        assert!(file_path2.exists());
        assert!(get_instance_name(None, &ffx_config).await.is_err());

        // We "shut down" the first one, and we get the second one back.
        assert!(std::fs::remove_file(file_path1).is_ok());
        let result = get_instance_name(None, &ffx_config).await;
        assert!(result.is_ok(), "{:?}", result);
        assert_eq!(result.unwrap(), "test_dir2");

        Ok(())
    }
}
