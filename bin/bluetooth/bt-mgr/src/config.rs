// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_bluetooth_control::ControlMarker;
use fidl_fuchsia_bluetooth_control::InputCapabilityType;
use fidl_fuchsia_bluetooth_control::OutputCapabilityType;
use fuchsia_app::client::connect_to_service;
use std::fs::OpenOptions;
use std::io::Read;
use serde_json;
use failure::Error;
use serde_derive::{Deserialize, Serialize};

static CONFIG_FILE_PATH: &'static str = "/pkg/data/default.json";

#[derive(Serialize, Deserialize)]
struct Config {
    #[serde(rename = "io-capability")]
    io: IoConfig
}

#[derive(Serialize, Deserialize)]
struct IoConfig {
    #[serde(with = "InputCapabilityTypeDef")]
    input: InputCapabilityType,
    #[serde(with = "OutputCapabilityTypeDef")]
    output: OutputCapabilityType,
}

#[derive(Serialize, Deserialize)]
#[serde(remote = "InputCapabilityType")]
#[allow(dead_code)]
pub enum InputCapabilityTypeDef {
    None = 0,
    Confirmation = 1,
    Keyboard = 2,
}

#[derive(Serialize, Deserialize)]
#[serde(remote = "OutputCapabilityType")]
#[allow(dead_code)]
pub enum OutputCapabilityTypeDef {
    None = 0,
    Display = 1,
}

pub async fn set_capabilities() -> Result<(), Error> {
    let mut config = OpenOptions::new()
        .read(true)
        .write(false)
        .open(CONFIG_FILE_PATH).unwrap();

    let mut contents = String::new();
    config
        .read_to_string(&mut contents)
        .expect("The io config file is corrupted");

    let bt_svc = connect_to_service::<ControlMarker>()
        .expect("failed to connect to bluetooth control interface");

    match serde_json::from_str(contents.as_str()) {
        Ok(conf) => {
            let conf: Config = conf;
            bt_svc.set_io_capabilities(conf.io.input, conf.io.output).map_err(Into::into)
        },
        Err(e) => Err(e.into())
    }
}
