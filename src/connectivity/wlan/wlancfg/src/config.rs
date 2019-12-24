// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context as _, Error};
use fidl_fuchsia_wlan_device as wlan;
use serde_derive::Deserialize;
use std::{
    collections::BTreeMap,
    fs::File,
    io::{Read, Write},
};

const CONFIG_FILE: &str = "/data/config.json";
const DEFAULT_CONFIG_FILE: &str = "/config/data/default.json";

#[derive(Copy, Clone, Debug, Deserialize, PartialEq)]
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
    // Using BTreeMap as keys need to be sorted for the fuzzy path matching with fold() to work.
    pub phy: BTreeMap<String, Vec<Role>>,
}

impl Config {
    pub fn load_from_file() -> Result<Config, Error> {
        Self::load_data_config().or_else(|_| Self::load_default_config())
    }

    /// finds the longest prefix that matches the PHY's topological path
    pub fn roles_for_path(&self, path: &str) -> Option<&[Role]> {
        let mut key = "*";
        for p in self.phy.keys() {
            if (p.ends_with("/*") && path.starts_with(&p[..p.len() - 1])) || path == p {
                key = p;
            }
        }

        if let Some(roles) = self.phy.get(key) {
            if key == "*" {
                println!("using default wlan config entry for phy");
            } else {
                println!("using wlan config entry {} for phy {}", key, path);
            }
            Some(roles)
        } else {
            println!("no wlan config entry found for phy at {}", path);
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

#[cfg(test)]
mod tests {
    use {
        super::{Config, Role::*},
        maplit::btreemap,
    };

    #[test]
    fn get_role_from_path() {
        let mut cfg = Config {
            phy: btreemap! {
                "/vendor/chip/1/good".to_string() => vec![Client],
                "/vendor/chip/1/g".to_string() => vec![Ap, Ap],
                "/vendor/chip/1/*".to_string() => vec![Ap],
                "/vendor/chipmunk/good".to_string() => vec![Client, Ap],
                "*".to_string() => vec![Mesh],
            },
        };

        // exact match
        assert_eq!(cfg.roles_for_path("/vendor/chip/1/good"), Some(&[Client][..]));
        // exact match, same length as /*
        assert_eq!(cfg.roles_for_path("/vendor/chip/1/g"), Some(&[Ap, Ap][..]));
        // prefix matches
        assert_eq!(cfg.roles_for_path("/vendor/chip/1/better"), Some(&[Ap][..]));
        // exact match
        assert_eq!(cfg.roles_for_path("/vendor/chipmunk/good"), Some(&[Client, Ap][..]));
        // no match since no prefix defined
        assert_eq!(cfg.roles_for_path("/vendor/chipmunk/good/child"), Some(&[Mesh][..]));
        // nothing defined
        assert_eq!(cfg.roles_for_path("/vendor/not-chip/1/mine"), Some(&[Mesh][..]));

        // Now let's remove the default
        cfg.phy.remove("*");
        assert_eq!(cfg.roles_for_path("/vendor/chipmunk/good/child"), None);
        assert_eq!(cfg.roles_for_path("/vendor/not-chip/1/mine"), None);
    }
}
