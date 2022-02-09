// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    serde::{Deserialize, Serialize},
    serde_json5::from_str,
    std::fs::read_to_string,
};

const CONFIG_PATH: &str = "/pkg/data/test_config.json5";

/// Shared configuration file for security package delivery tests.
#[derive(Serialize, Deserialize)]
pub struct Config {
    /// Domain name (that is, hostname of package server) used for OTA updates.
    pub update_domain: String,
}

/// Default path in fuchsia test packages where test the configuration file is
/// stored.
pub fn default_target_config_path() -> &'static str {
    CONFIG_PATH
}

pub fn load_config(config_path: &str) -> Config {
    from_str(&read_to_string(config_path).unwrap()).unwrap()
}
