// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The ffx_emulator_engines crate contains the implementation
//! of each emulator "engine" such as aemu and qemu.

mod femu;
mod qemu;
mod serialization;

use femu::FemuEngine;
use qemu::QemuEngine;

use anyhow::Result;
use ffx_emulator_common::config::{FfxConfigWrapper, EMU_INSTANCE_ROOT_DIR};
use ffx_emulator_config::{
    DeviceConfig, EmulatorConfiguration, EmulatorEngine, EngineType, GuestConfig, HostConfig,
    RuntimeConfig,
};
use std::{fs::create_dir_all, path::PathBuf};

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
    pub fn new() -> Self {
        Self {
            emulator_configuration: EmulatorConfiguration::default(),
            engine_type: EngineType::default(),
            ffx_config: FfxConfigWrapper::new(),
        }
    }

    pub fn config(mut self, config: EmulatorConfiguration) -> EngineBuilder {
        self.emulator_configuration = config;
        self
    }

    pub fn device(mut self, device_config: DeviceConfig) -> EngineBuilder {
        self.emulator_configuration.device = device_config;
        self
    }

    pub fn engine_type(mut self, engine_type: EngineType) -> EngineBuilder {
        self.engine_type = engine_type;
        self
    }

    pub fn ffx_config(mut self, ffx_config: FfxConfigWrapper) -> EngineBuilder {
        self.ffx_config = ffx_config;
        self
    }

    pub fn guest(mut self, guest_config: GuestConfig) -> EngineBuilder {
        self.emulator_configuration.guest = guest_config;
        self
    }

    pub fn host(mut self, host_config: HostConfig) -> EngineBuilder {
        self.emulator_configuration.host = host_config;
        self
    }

    pub fn runtime(mut self, runtime_config: RuntimeConfig) -> EngineBuilder {
        self.emulator_configuration.runtime = runtime_config;
        self
    }

    pub async fn build(mut self) -> Result<Box<dyn EmulatorEngine>> {
        // Set up the instance directory, now that we have enough information.
        let mut path = PathBuf::from(self.ffx_config.get(EMU_INSTANCE_ROOT_DIR).await?);
        path.push(&self.emulator_configuration.runtime.name);
        create_dir_all(&path.as_path())?;
        self.emulator_configuration.runtime.instance_directory = path;

        let engine: Box<dyn EmulatorEngine> = match self.engine_type {
            EngineType::Femu => Box::new(FemuEngine {
                emulator_configuration: self.emulator_configuration,
                _ffx_config: self.ffx_config,
                ..Default::default()
            }),
            EngineType::Qemu => Box::new(QemuEngine {
                emulator_configuration: self.emulator_configuration,
                _ffx_config: self.ffx_config,
                ..Default::default()
            }),
        };
        engine.validate()?;
        Ok(engine)
    }
}
