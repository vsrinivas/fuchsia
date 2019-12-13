// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{self, Error},
    serde_derive::{Deserialize, Serialize},
    std::fs::File,
    std::io,
};

#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct Config {
    pub tools: Option<String>,
}

impl Config {
    pub fn new() -> Self {
        Config { tools: None }
    }

    pub fn load_from_config_data(&mut self, path: &str) -> Result<(), Error> {
        let config_map: Config = serde_json::from_reader(io::BufReader::new(File::open(path)?))?;
        self.tools = config_map.tools;
        Ok(())
    }
}
