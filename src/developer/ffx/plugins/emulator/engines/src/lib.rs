// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The ffx_emulator_engines crate contains the implementation
//! of each emulator "engine" such as aemu and qemu.

mod arg_templates;
mod femu;
mod qemu;
pub mod serialization;

use femu::FemuEngine;
use qemu::QemuEngine;
use serialization::read_from_disk;

use anyhow::{bail, Context, Result};
use ffx_emulator_common::config::{FfxConfigWrapper, EMU_INSTANCE_ROOT_DIR};
use ffx_emulator_config::{
    DeviceConfig, EmulatorConfiguration, EmulatorEngine, EngineType, GuestConfig, HostConfig,
    RuntimeConfig,
};
use std::{fs::create_dir_all, path::PathBuf};

pub use femu::FemuEngine as TestEngineDoNotUseOutsideOfTests;

pub(crate) const SERIALIZE_FILE_NAME: &str = "engine.json";

/// The EngineBuilder is used to create and configure an EmulatorEngine, while ensuring the
/// configuration will result in a valid emulation instance.
///
/// Create an EngineBuilder using EngineBuilder::new(). This will populate the builder with the
/// defaults for all configuration options. Then use the setter methods to update configuration
/// options, and call "build()" when configuration is complete.
///
/// Setters are independent, optional, and idempotent; i.e. callers may call as many or as few of
/// the setters as needed, and repeat calls if necessary. However, setters consume the data that
/// are passed in, so the caller must set up a new structure for each call.
///
/// Once "build" is called, an engine will be instantiated of the indicated type, the configuration
/// will be loaded into that engine, and the engine's "validate" function will be invoked to ensure
/// the configuration is acceptable. If validation fails, the engine will be destroyed. The
/// EngineBuilder instance is consumed when invoking "build" regardless of the outcome.
///
/// Example:
///
///    let builder = EngineBuilder::new()
///         .engine_type(EngineType::Femu)
///         .device(my_device_config)
///         .guest(my_guest_config)
///         .host(my_host_config)
///         .runtime(my_runtime_config);
///
///     let mut engine: Box<dyn EmulatorEngine> = builder.build()?;
///     (*engine).start().await
///
pub struct EngineBuilder {
    emulator_configuration: EmulatorConfiguration,
    engine_type: EngineType,
    ffx_config: FfxConfigWrapper,
}

impl EngineBuilder {
    /// Create a new EngineBuilder, populated with default values for all configuration.
    pub fn new() -> Self {
        Self {
            emulator_configuration: EmulatorConfiguration::default(),
            engine_type: EngineType::default(),
            ffx_config: FfxConfigWrapper::new(),
        }
    }

    /// Set the configuration to use when building a new engine.
    pub fn config(mut self, config: EmulatorConfiguration) -> EngineBuilder {
        self.emulator_configuration = config;
        self
    }

    /// Set the engine's virtual device configuration.
    pub fn device(mut self, device_config: DeviceConfig) -> EngineBuilder {
        self.emulator_configuration.device = device_config;
        self
    }

    /// Set the type of the engine to be built.
    pub fn engine_type(mut self, engine_type: EngineType) -> EngineBuilder {
        self.engine_type = engine_type;
        self
    }

    /// Set the FfxConfigWrapper to be used when building this engine.
    pub fn ffx_config(mut self, ffx_config: FfxConfigWrapper) -> EngineBuilder {
        self.ffx_config = ffx_config;
        self
    }

    /// Set the engine's guest configuration.
    pub fn guest(mut self, guest_config: GuestConfig) -> EngineBuilder {
        self.emulator_configuration.guest = guest_config;
        self
    }

    /// Set the engine's host configuration.
    pub fn host(mut self, host_config: HostConfig) -> EngineBuilder {
        self.emulator_configuration.host = host_config;
        self
    }

    /// Set the engine's runtime configuration.
    pub fn runtime(mut self, runtime_config: RuntimeConfig) -> EngineBuilder {
        self.emulator_configuration.runtime = runtime_config;
        self
    }

    /// Finalize and validate the configuration, set up the engine's instance directory,
    ///  and return the built engine.
    pub async fn build(mut self) -> Result<Box<dyn EmulatorEngine>> {
        // Set up the instance directory, now that we have enough information.
        let name = &self.emulator_configuration.runtime.name;
        self.emulator_configuration.runtime.instance_directory =
            get_instance_dir(&self.ffx_config, name, true).await?;

        // Make sure we don't overwrite an existing instance.
        let filepath =
            self.emulator_configuration.runtime.instance_directory.join(SERIALIZE_FILE_NAME);
        if filepath.exists() {
            let engine = read_from_disk(&self.emulator_configuration.runtime.instance_directory)
                .context(format!(
                    "Found an existing emulator with the name {}, but couldn't load it from disk. \
                    Use `ffx emu stop {}` to terminate and clean up the existing emulator.",
                    name, name
                ))?;
            if engine.is_running() {
                bail!(
                    "An emulator named {} is already running. \
                    Use a different name, or run `ffx emu stop {}` \
                    to stop the running emulator.",
                    name,
                    name
                );
            }
        }
        log::debug!("Serialized engine file will be created at {:?}", filepath);

        // Build and validate the engine, then pass it back to the caller.
        let engine: Box<dyn EmulatorEngine> = match self.engine_type {
            EngineType::Femu => Box::new(FemuEngine {
                emulator_configuration: self.emulator_configuration,
                ffx_config: self.ffx_config,
                engine_type: self.engine_type,
                ..Default::default()
            }),
            EngineType::Qemu => Box::new(QemuEngine {
                emulator_configuration: self.emulator_configuration,
                ffx_config: self.ffx_config,
                engine_type: self.engine_type,
                ..Default::default()
            }),
        };
        engine.validate()?;
        Ok(engine)
    }
}

/// Return a PathBuf with the path to the instance directory for this engine. If the "create" flag
/// is set, the directory and its ancestors will be created if it doesn't already exist.
pub async fn get_instance_dir(
    ffx_config: &FfxConfigWrapper,
    instance_name: &str,
    create: bool,
) -> Result<PathBuf> {
    let path = PathBuf::from(ffx_config.get(EMU_INSTANCE_ROOT_DIR).await?).join(&instance_name);
    if !path.exists() {
        if create {
            log::debug!("Creating {:?} for {}", path, instance_name);
            create_dir_all(&path.as_path())?;
        } else {
            log::debug!(
                "Path {} doesn't exist. Check the spelling of the instance name.",
                instance_name
            );
        }
    }
    Ok(path)
}

/// Given an instance name, empty and remove the instance directory associated with that name.
/// Fails if the directory can't be removed; returns Ok(()) if the directory doesn't exist.
pub async fn clean_up_instance_dir(path: &PathBuf) -> Result<()> {
    if path.exists() {
        log::debug!("Removing {:?} for {:?}", path, path.as_path().file_name().unwrap());
        std::fs::remove_dir_all(&path.as_path()).context("Request to remove directory failed")
    } else {
        // It's already gone, so just return Ok(()).
        Ok(())
    }
}

/// Retrieve a list of all of the names of instances currently present on the local system.
pub async fn get_all_instances(ffx_config: &FfxConfigWrapper) -> Result<Vec<PathBuf>> {
    let mut result = Vec::new();
    let buf = PathBuf::from(ffx_config.file(EMU_INSTANCE_ROOT_DIR).await?);
    let root = buf.as_path();
    if root.is_dir() {
        for entry in root.read_dir()? {
            if let Ok(entry) = entry {
                if !entry.path().is_dir() {
                    continue;
                }
                if entry.path().join(SERIALIZE_FILE_NAME).exists() {
                    result.push(entry.path());
                }
            }
        }
    }
    return Ok(result);
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs::{remove_file, File};
    use tempfile::tempdir;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_get_instance_dir() -> Result<()> {
        let mut config = FfxConfigWrapper::new();
        let temp_dir = tempdir()
            .expect("Couldn't get a temporary directory for testing.")
            .path()
            .to_str()
            .expect("Couldn't convert Path to str")
            .to_string();
        config.overrides.insert(EMU_INSTANCE_ROOT_DIR, temp_dir.clone());

        // Create a new directory.
        let path1 = get_instance_dir(&config, "create_me", true).await?;
        assert_eq!(path1, PathBuf::from(&temp_dir).join("create_me"));
        assert!(path1.exists());

        // Look for a dir that doesn't exist, but don't create it.
        let path2 = get_instance_dir(&config, "dont_create", false).await?;
        assert!(!path2.exists());

        // Look for a dir that already exists, but don't allow creation.
        let mut path3 = get_instance_dir(&config, "create_me", false).await?;
        assert_eq!(path3, PathBuf::from(&temp_dir).join("create_me"));
        assert!(path3.exists());

        // Get an existing directory, but set the create flag too. Make sure it didn't get replaced.
        path3 = path3.join("foo.txt");
        let _ = File::create(&path3)?;
        let path4 = get_instance_dir(&config, "create_me", true).await?;
        assert!(path4.exists());
        assert!(path3.exists());
        assert_eq!(path4, PathBuf::from(&temp_dir).join("create_me"));
        for entry in path4.as_path().read_dir()? {
            assert_eq!(entry?.path(), path3);
        }

        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_get_all_instances() -> Result<()> {
        let mut config = FfxConfigWrapper::new();
        let temp_dir = tempdir()
            .expect("Couldn't get a temporary directory for testing.")
            .path()
            .to_str()
            .expect("Couldn't convert Path to str")
            .to_string();
        config.overrides.insert(EMU_INSTANCE_ROOT_DIR, temp_dir.clone());

        // Create three mock instance directories, and make sure they're all included.
        let path1 = PathBuf::from(&temp_dir).join("path1");
        create_dir_all(path1.as_path())?;
        let file1_path = path1.join(SERIALIZE_FILE_NAME);
        let _file1 = File::create(&file1_path)?;

        let path2 = PathBuf::from(&temp_dir).join("path2");
        create_dir_all(path2.as_path())?;
        let file2_path = path2.join(SERIALIZE_FILE_NAME);
        let _file2 = File::create(&file2_path)?;

        let path3 = PathBuf::from(&temp_dir).join("path3");
        create_dir_all(path3.as_path())?;
        let file3_path = path3.join(SERIALIZE_FILE_NAME);
        let _file3 = File::create(&file3_path)?;

        let instances = get_all_instances(&config).await?;
        assert!(instances.contains(&path1));
        assert!(instances.contains(&path2));
        assert!(instances.contains(&path3));

        // If the directory doesn't contain an engine.json file, it's not an instance.
        // Remove the file for path2, and make sure it's excluded from the results.
        assert!(remove_file(&file2_path).is_ok());

        let instances = get_all_instances(&config).await?;
        assert!(instances.contains(&path1));
        assert!(!instances.contains(&path2));
        assert!(instances.contains(&path3));

        // Other files in the root shouldn't be included either. Create an empty file in the root
        // and make sure it's excluded too.
        let file_path = PathBuf::from(&temp_dir).join("empty_file");
        let _empty_file = File::create(&file_path)?;

        let instances = get_all_instances(&config).await?;
        assert!(instances.contains(&path1));
        assert!(!instances.contains(&path2));
        assert!(instances.contains(&path3));
        assert!(!instances.contains(&file_path));

        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_clean_up_instance_dir() -> Result<()> {
        let mut config = FfxConfigWrapper::new();
        let temp_dir = tempdir()
            .expect("Couldn't get a temporary directory for testing.")
            .path()
            .to_str()
            .expect("Couldn't convert Path to str")
            .to_string();
        config.overrides.insert(EMU_INSTANCE_ROOT_DIR, temp_dir.clone());

        let path1 = PathBuf::from(&temp_dir).join("path1");
        create_dir_all(path1.as_path())?;
        assert!(path1.exists());

        let path2 = PathBuf::from(&temp_dir).join("path2");
        create_dir_all(path2.as_path())?;
        assert!(path2.exists());

        let file_path = path2.join("foo.txt");
        let _ = File::create(&file_path)?;
        assert!(file_path.exists());

        // Clean up an existing, empty directory
        assert!(clean_up_instance_dir(&path1).await.is_ok());
        assert!(!path1.exists());

        // Clean up an existing, populated directory
        assert!(clean_up_instance_dir(&path2).await.is_ok());
        assert!(!path2.exists());
        assert!(!file_path.exists());

        // Clean up an non-existing directory
        assert!(clean_up_instance_dir(&path1).await.is_ok());
        assert!(!path1.exists());
        Ok(())
    }
}
