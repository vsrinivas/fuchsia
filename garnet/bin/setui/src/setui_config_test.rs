// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context, Error};
use argh::FromArgs;
use fuchsia_syslog::fx_log_info;
use serde::de::DeserializeOwned;
use settings::{
    AgentConfiguration, EnabledServicesConfiguration, LightHardwareConfiguration,
    LightSensorConfig, ServiceFlags,
};
use std::ffi::{OsStr, OsString};
use std::fs::File;
use std::io::Read;

/// setui_config_tests validates configuration files passed to the settings service.
#[derive(FromArgs)]
struct TestConfig {
    /// these configurations are the ones that will determine which controllers are enabled
    /// within the settings service.
    #[argh(option, short = 's')]
    service_config: Vec<OsString>,

    /// these configurations are the one that will determine the behavior of individual controllers.
    #[argh(option, short = 'f')]
    controller_flags: Vec<OsString>,

    /// these configurations control specific settings within the light sensor controller.
    #[argh(option, short = 'l')]
    light_sensor_config: Vec<OsString>,

    /// these configurations control specific settings for light hardware.
    #[argh(option, short = 'h')]
    light_hardware_config: Vec<OsString>,

    /// these configurations control which agents are enabled.
    #[argh(option, short = 'a')]
    agent_config: Vec<OsString>,
}

fn read_config<C: DeserializeOwned>(path: &OsStr) -> Result<(), Error> {
    fx_log_info!("Validating {:?}", path);
    let mut file = File::open(path)
        .with_context(|| format!("Couldn't open path `{}`", path.to_string_lossy()))?;
    let mut contents = String::new();
    file.read_to_string(&mut contents)
        .with_context(|| format!("Couldn't read file at path `{}`", path.to_string_lossy()))?;
    let _ =
        serde_json::from_str::<C>(&contents).context("Failed to deserialize flag configuration")?;
    Ok(())
}

fn main() -> Result<(), Error> {
    let test_config: TestConfig = argh::from_env();
    for config in test_config.service_config.into_iter() {
        read_config::<EnabledServicesConfiguration>(&config)?;
    }

    for config in test_config.controller_flags.into_iter() {
        read_config::<ServiceFlags>(&config)?;
    }

    for config in test_config.light_sensor_config.into_iter() {
        read_config::<LightSensorConfig>(&config)?;
    }

    for config in test_config.light_hardware_config.into_iter() {
        read_config::<LightHardwareConfiguration>(&config)?;
    }

    for config in test_config.agent_config.into_iter() {
        read_config::<AgentConfiguration>(&config)?;
    }

    Ok(())
}
