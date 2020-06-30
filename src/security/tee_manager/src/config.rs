// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    serde_derive::Deserialize,
    serde_json,
    std::{fs::File, io::BufReader},
    uuid::Uuid,
};

const CONFIG_FILE: &'static str = "/config/data/tee_manager.config";

#[derive(Deserialize)]
pub struct Config {
    pub application_uuids: Vec<Uuid>,
}

impl Config {
    pub fn from_file() -> Result<Self, Error> {
        Ok(serde_json::from_reader(BufReader::new(
            File::open(CONFIG_FILE).context("Unable to open config")?,
        ))
        .context("Unable to parse config")?)
    }
}
