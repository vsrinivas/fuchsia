// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    serde::{Deserialize, Serialize},
    serde_json,
    std::{fs::OpenOptions, io::Read},
};

static CONFIG_FILE_PATH: &'static str = "/pkg/data/bt-init-default.json";

#[derive(Serialize, Deserialize)]
pub struct Config {
    #[serde(rename = "autostart-snoop")]
    autostart_snoop: bool,
}

impl Config {
    pub fn load() -> Result<Config, Error> {
        let mut config = OpenOptions::new().read(true).write(false).open(CONFIG_FILE_PATH).unwrap();

        let mut contents = String::new();
        let _ = config.read_to_string(&mut contents).expect("bt-init config file is corrupted");

        Ok(serde_json::from_str(contents.as_str()).context("failed to parse config file")?)
    }

    pub fn autostart_snoop(&self) -> bool {
        self.autostart_snoop
    }
}
