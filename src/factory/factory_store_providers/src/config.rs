// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::Error, fidl::endpoints::DiscoverableService, fuchsia_syslog as syslog,
    serde_derive::Deserialize, serde_json,
};

#[derive(Debug, Deserialize)]
pub struct FactoryFileSpec {
    pub dest: Option<String>,
    pub path: String,
}

#[derive(Debug, Deserialize)]
pub struct Config {
    pub files: Vec<FactoryFileSpec>,
    // TODO(mbrunson): Add validation configuration for factory files.
}
impl Config {
    fn load_file(path: &str) -> Result<Self, Error> {
        let contents = std::fs::read(path)?;
        Ok(serde_json::from_str(&String::from_utf8(contents)?)?)
    }

    pub fn load<T>() -> Result<Self, Error>
    where
        T: DiscoverableService,
    {
        let config_data_file = format!("/config/data/{}.config", &T::SERVICE_NAME);
        syslog::fx_log_info!("Loading {}", &config_data_file);
        Config::load_file(&config_data_file)
    }
}
