// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fuchsia_zircon as zx,
    serde_derive::{Deserialize, Serialize},
    serde_json,
    std::collections::HashMap,
};

const CONFIG_JSON: &str = "/pkg/data/fake_factory_items.json";

pub type ConfigMapValue = HashMap<u32, (zx::Vmo, u32)>;

#[derive(Debug, Deserialize, Serialize)]
pub struct Config {
    manifest: Vec<FactoryItem>,
}

#[derive(Debug, Deserialize, Serialize)]
struct FactoryItem {
    extra: u32,
    path: String,
    #[serde(default)]
    r#type: String,
}

impl Config {
    pub fn load() -> Result<Config, Error> {
        let contents = std::fs::read(CONFIG_JSON)?;
        Ok(serde_json::from_str(&String::from_utf8(contents)?)?)
    }
}

impl Into<ConfigMapValue> for Config {
    fn into(self) -> ConfigMapValue {
        let mut map = HashMap::new();

        for item in self.manifest {
            let mut contents = std::fs::read(&item.path).unwrap();

            if item.r#type == "zbi" {
                // For ZBI files, we expect the file to contain a single payload.
                // We strip 2 ZBI headers from the file, leaving only the payload.
                contents = contents[64..].to_vec();
            }

            let vmo =
                zx::Vmo::create(contents.len() as u64).expect("Failed to create factory file vmo");
            vmo.write(&contents, 0).expect("Failed to write factory file vmo");
            map.insert(item.extra, (vmo, contents.len() as u32));
        }

        map
    }
}
