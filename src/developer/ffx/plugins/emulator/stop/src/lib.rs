// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Context, Result};
use errors::ffx_bail;
use ffx_core::ffx_plugin;
use ffx_emulator_common::config::FfxConfigWrapper;
use ffx_emulator_engines::{
    clean_up_instance_dir, get_all_instances, get_instance_dir, serialization::read_from_disk,
};
use ffx_emulator_stop_args::StopCommand;
use fidl_fuchsia_developer_bridge::TargetCollectionProxy;
use std::path::PathBuf;

async fn attempt_stop(instance_dir: &PathBuf, proxy: &TargetCollectionProxy) -> Result<()> {
    if !instance_dir.exists() {
        return Err(anyhow!(
            "{:?} isn't a valid instance. Please check your spelling and try again. \
                You can use `ffx emu list` to see currently available instances.",
            instance_dir.file_name().ok_or("<unspecified>").unwrap()
        ));
    }
    let engine = read_from_disk(instance_dir).context(
        "Couldn't deserialize engine from disk. \
        Continuing stop, but you may need to terminate the emulator process manually.",
    );
    match engine {
        Ok(engine) => engine.stop(proxy).await,
        Err(e) => Err(e),
    }
}

async fn get_instance_paths(
    name: Option<String>,
    all: bool,
    ffx_config: &FfxConfigWrapper,
) -> Result<Vec<PathBuf>> {
    // If the user gave us a name, we're just going to return that.
    if !all && name.is_some() {
        return Ok(vec![get_instance_dir(&ffx_config, &name.unwrap(), false).await?]);
    }

    let all_instances = get_all_instances(&ffx_config).await?;
    if all {
        return Ok(all_instances);
    } else {
        // name.is_none()
        if all_instances.len() == 1 {
            Ok(all_instances)
        } else if all_instances.len() == 0 {
            Err(anyhow!("No emulators are running."))
        } else {
            Err(anyhow!(
                "Multiple emulators are running. Indicate which emulator to stop using \
                `ffx emu stop <name>` or stop everything with `ffx emu stop --all`.\n\
                See all the emulators available to stop using `ffx emu list`."
            ))
        }
    }
}

#[ffx_plugin("emu.experimental", TargetCollectionProxy = "daemon::protocol")]
pub async fn stop(cmd: StopCommand, proxy: TargetCollectionProxy) -> Result<()> {
    let ffx_config = FfxConfigWrapper::new();
    match get_instance_paths(cmd.name, cmd.all, &ffx_config).await {
        Ok(instances) => {
            for path in instances {
                let result = attempt_stop(&path, &proxy).await;
                if !cmd.persist {
                    let cleanup = clean_up_instance_dir(&path).await;
                    if cleanup.is_err() {
                        ffx_bail!("{:?}", cleanup.unwrap_err());
                    }
                }
                if result.is_err() {
                    ffx_bail!("{:?}", result.unwrap_err());
                }
            }
        }
        Err(e) => {
            ffx_bail!("{:?}", e);
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use ffx_emulator_common::config::EMU_INSTANCE_ROOT_DIR;
    use std::{
        fs::{create_dir_all, File},
        path::PathBuf,
    };
    use tempfile::tempdir;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_get_instance_paths() -> Result<()> {
        let temp_dir_path = tempdir()
            .expect("Couldn't get a temporary directory for testing.")
            .path()
            .to_str()
            .expect("Couldn't convert Path to str")
            .to_string();
        let temp_dir = PathBuf::from(&temp_dir_path);
        let mut ffx_config = FfxConfigWrapper::new();
        ffx_config.overrides.insert(EMU_INSTANCE_ROOT_DIR, temp_dir_path);

        // If a name is given on CLI, we just return that every time, regardless of existence.
        let result = get_instance_paths(Some("instance".to_string()), false, &ffx_config).await;
        assert!(result.is_ok());
        let result = result.unwrap();
        assert_eq!(result.len(), 1);
        assert!(result.contains(&temp_dir.join("instance")));

        // If all is specified, that takes precedence over a specified name.
        let result = get_instance_paths(Some("instance".to_string()), true, &ffx_config).await;
        assert!(result.is_ok());
        let result = result.unwrap();
        assert_eq!(result.len(), 0);

        // If no instances exist, and no name is provided, it's an error.
        assert!(get_instance_paths(None, false, &ffx_config).await.is_err());

        // ...unless they specify --all.
        let result = get_instance_paths(None, true, &ffx_config).await;
        assert!(result.is_ok());
        let result = result.unwrap();
        assert_eq!(result.len(), 0);

        // If only one instance exists, that should be the value returned.
        let instance_dir1 = temp_dir.join("test_dir1");
        create_dir_all(instance_dir1.as_path())?;
        let file_path1 = instance_dir1.join("engine.json");
        let _file1 = File::create(&file_path1)?;
        assert!(file_path1.exists());

        let result = get_instance_paths(None, false, &ffx_config).await;
        assert!(result.is_ok(), "{:?}", result);
        assert_eq!(result.unwrap(), vec![instance_dir1.clone()]);

        // Same if they specify --all when there's only one.
        let result = get_instance_paths(None, true, &ffx_config).await;
        assert!(result.is_ok(), "{:?}", result);
        assert_eq!(result.unwrap(), vec![instance_dir1.clone()]);

        // If more than one instance is up, and no name is provided, it's an error.
        let instance_dir2 = temp_dir.join("test_dir2");
        create_dir_all(instance_dir2.as_path())?;
        let file_path2 = instance_dir2.join("engine.json");
        let _file2 = File::create(&file_path2)?;
        assert!(file_path2.exists());
        assert!(get_instance_paths(None, false, &ffx_config).await.is_err());

        // ...unless the user specified --all.
        let result = get_instance_paths(None, true, &ffx_config).await;
        assert!(result.is_ok(), "{:?}", result);
        let result = result.unwrap();
        assert!(result.contains(&instance_dir1));
        assert!(result.contains(&instance_dir2));

        // We "shut down" the first one, and we get the second one back.
        assert!(std::fs::remove_file(file_path1).is_ok());
        let result = get_instance_paths(None, false, &ffx_config).await;
        assert!(result.is_ok(), "{:?}", result);
        assert_eq!(result.unwrap(), vec![instance_dir2.clone()]);

        // And again, --all should match.
        let result = get_instance_paths(None, true, &ffx_config).await;
        assert!(result.is_ok(), "{:?}", result);
        assert_eq!(result.unwrap(), vec![instance_dir2]);

        Ok(())
    }
}
