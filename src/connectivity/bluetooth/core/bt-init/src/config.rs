// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_fuchsia_bluetooth_control::{ControlMarker, InputCapabilityType, OutputCapabilityType},
    fuchsia_component::client::connect_to_service,
    serde_derive::{Deserialize, Serialize},
    serde_json,
    std::{fs::OpenOptions, io::Read},
};

static CONFIG_FILE_PATH: &'static str = "/pkg/data/default.json";

#[derive(Serialize, Deserialize)]
pub struct Config {
    #[serde(rename = "io-capability")]
    io: IoConfig,
    #[serde(rename = "autostart-snoop")]
    autostart_snoop: bool,
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

impl Config {
    pub fn load() -> Result<Config, Error> {
        let mut config = OpenOptions::new().read(true).write(false).open(CONFIG_FILE_PATH).unwrap();

        let mut contents = String::new();
        config.read_to_string(&mut contents).expect("The bt-init config file is corrupted");

        Ok(serde_json::from_str(contents.as_str())?)
    }

    pub fn autostart_snoop(&self) -> bool {
        self.autostart_snoop
    }

    pub async fn set_capabilities(&self) -> Result<(), Error> {
        let bt_svc = connect_to_service::<ControlMarker>()
            .expect("failed to connect to bluetooth control interface");
        bt_svc.set_io_capabilities(self.io.input, self.io.output).map_err(Into::into)
    }
}
