// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure;
use failure::ResultExt;
use fidl_fuchsia_fonts as fonts;
use serde::de::{Deserialize, Deserializer, Error};
use serde_derive::Deserialize;
use serde_json;
use std::fs::File;
use std::io::Read;

// Following structs are used to parse manifest.json.
#[derive(Debug, Deserialize)]
pub struct FontsManifest {
    pub fallback: Option<String>,
    pub families: Vec<Family>,
}

#[derive(Debug, Deserialize)]
pub struct Family {
    pub family: String,
    pub fonts: Vec<Font>,
}

#[derive(Debug, Deserialize)]
pub struct Font {
    pub asset: String,

    #[serde(
        default = "default_slant",
        deserialize_with = "deserialize_slant"
    )]
    pub slant: fonts::FontSlant,

    #[serde(default = "default_weight")]
    pub weight: u32,
}

fn default_slant() -> fonts::FontSlant {
    fonts::FontSlant::Upright
}

fn default_weight() -> u32 {
    400
}

fn deserialize_slant<'d, D>(deserializer: D) -> Result<fonts::FontSlant, D::Error>
where
    D: Deserializer<'d>,
{
    let s = String::deserialize(deserializer)?;
    match s.as_str() {
        "upright" => Ok(fonts::FontSlant::Upright),
        "italic" => Ok(fonts::FontSlant::Italic),
        x => Err(D::Error::custom(format!(
            "unknown value for slant in manifest: {}",
            x
        ))),
    }
}

impl FontsManifest {
    pub fn load_from_file(path: &str) -> Result<FontsManifest, failure::Error> {
        let mut f = File::open(path)?;
        let mut contents = String::new();
        f.read_to_string(&mut contents)?;
        Ok(serde_json::from_str(&contents).context(format!("Failed to load {}", path))?)
    }
}
