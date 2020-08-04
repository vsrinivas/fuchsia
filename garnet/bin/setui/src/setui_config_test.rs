// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context, Error};
use clap::{App, Arg};
use serde::de::DeserializeOwned;
use settings::{EnabledServicesConfiguration, LightSensorConfig, ServiceFlags};
use std::ffi::OsStr;
use std::fs::File;
use std::io::Read;

fn read_config<C: DeserializeOwned>(path: &OsStr) -> Result<(), Error> {
    println!("Validating {:?}", path);
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
    let matches = App::new("setui_config_tests")
        .arg(Arg::with_name("service_configuration").index(1).required(false))
        .arg(Arg::with_name("controller_flag").index(2).required(false))
        .arg(
            Arg::with_name("light_sensor_configs")
                .short("l")
                .takes_value(true)
                .multiple(true)
                .min_values(0),
        )
        .arg(
            Arg::with_name("service_configurations")
                .short("s")
                .takes_value(true)
                .multiple(true)
                .min_values(0)
                .conflicts_with("service_configuration"),
        )
        .arg(
            Arg::with_name("controller_flags")
                .short("f")
                .takes_value(true)
                .multiple(true)
                .min_values(0)
                .conflicts_with("controller_flag"),
        )
        .get_matches();

    for config in matches
        .value_of_os("service_configuration")
        .into_iter()
        .chain(matches.values_of_os("service_configurations").into_iter().flatten())
    {
        read_config::<EnabledServicesConfiguration>(config)?;
    }

    for config in matches
        .value_of_os("controller_flag")
        .into_iter()
        .chain(matches.values_of_os("controller_flags").into_iter().flatten())
    {
        read_config::<ServiceFlags>(config)?;
    }

    for config in matches.values_of_os("light_sensor_configs").into_iter().flatten() {
        read_config::<LightSensorConfig>(config)?;
    }

    Ok(())
}
