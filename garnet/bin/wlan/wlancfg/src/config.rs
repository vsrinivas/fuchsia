// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{Error, ResultExt};
use fidl_fuchsia_wlan_device as wlan;
use serde_derive::Deserialize;
use std::{
    collections::HashMap,
    fs::File,
    io::{Read, Write},
};

const CONFIG_FILE: &str = "/data/config.json";
const DEFAULT_CONFIG_FILE: &str = "/config/data/default.json";

#[derive(Copy, Clone, Debug, Deserialize)]
pub enum Role {
    Client,
    Ap,
    Mesh,
}

impl From<Role> for wlan::MacRole {
    fn from(r: Role) -> Self {
        match r {
            Role::Client => wlan::MacRole::Client,
            Role::Ap => wlan::MacRole::Ap,
            Role::Mesh => wlan::MacRole::Mesh,
        }
    }
}

#[derive(Debug, Deserialize)]
pub struct Device {
    pub path: String,
    pub roles: Vec<Role>,
}

#[derive(Debug, Deserialize)]
pub struct Config {
    pub phy: HashMap<String, Vec<Role>>,
}

impl Config {
    pub fn load_from_file() -> Result<Config, Error> {
        Self::load_data_config().or_else(|_| Self::load_default_config())
    }

    pub fn roles_for_path(&self, path: &str) -> Option<&[Role]> {
        if let Some(roles) = self.phy.get(path) {
            println!("found wlan config entry for phy at {}", path);
            Some(roles)
        } else if let Some(roles) = self.phy.get("*") {
            println!("using default wlan config entry for phy");
            Some(roles)
        } else {
            None
        }
    }

    fn load_data_config() -> Result<Config, Error> {
        let mut file = File::open(CONFIG_FILE).context("could not open config file")?;
        let mut contents = String::new();
        file.read_to_string(&mut contents).context("could not read config file")?;
        serde_json::from_str(&contents).map_err(Into::into)
    }

    fn load_default_config() -> Result<Config, Error> {
        {
            let mut default_file =
                File::open(DEFAULT_CONFIG_FILE).context("could not find default config file")?;
            let mut contents = String::new();
            default_file
                .read_to_string(&mut contents)
                .context("could not read default config file")?;

            let mut data_file =
                File::create(CONFIG_FILE).context("could not create new config file")?;
            data_file.write_all(contents.as_bytes()).context("could not write new config file")?;
        }
        Self::load_data_config()
    }
}
