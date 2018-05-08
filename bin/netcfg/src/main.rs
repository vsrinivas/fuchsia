// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

extern crate failure;
#[macro_use]
extern crate serde_derive;
extern crate serde_json;
extern crate fidl_device_settings;
extern crate fuchsia_app as app;
extern crate fuchsia_async as async;
extern crate fuchsia_zircon as zx;
extern crate futures;

use failure::{Error, ResultExt};
use fidl_device_settings::{DeviceSettingsManagerMarker};
use std::fs;

const DEFAULT_CONFIG_FILE: &str = "/pkg/data/default.json";

#[derive(Debug, Deserialize)]
pub struct Config {
    pub device_name: String,
}

fn parse_config(config: String) -> Result<Config, Error> {
    serde_json::from_str(&config).map_err(Into::into)
}

fn main() -> Result<(), Error> {
    let config = parse_config(fs::read_to_string(DEFAULT_CONFIG_FILE)?)?;
    let mut executor = async::Executor::new().context("error creating event loop")?;
    let device_settings_manager = app::client::connect_to_service::<DeviceSettingsManagerMarker>()
        .context("failed to connect to device settings manager")?;

    let f = device_settings_manager.set_string("DeviceName", &config.device_name);
    let res = executor.run_singlethreaded(f)?;
    print!("{:?}", res);
    Ok(())
}
