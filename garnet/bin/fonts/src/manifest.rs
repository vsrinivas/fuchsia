// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{self, format_err, ResultExt};
use fidl_fuchsia_fonts as fonts;
use serde::de::{self, Deserialize, Deserializer, Error};
use serde_derive::Deserialize;
use serde_json;
use std::fmt;
use std::fs::{self, File};
use std::io::Read;
use std::path::{Path, PathBuf};

// Following structs are used to parse manifest.json.
#[derive(Debug, Deserialize)]
pub struct FontsManifest {
    pub fallback: Option<String>,
    pub families: Vec<Family>,
}

#[derive(Debug, Deserialize)]
pub struct Family {
    pub family: String,

    pub aliases: Option<Vec<String>>,

    pub fonts: Vec<Font>,

    #[serde(default = "default_fallback")]
    pub fallback: bool,

    #[serde(
        default = "default_fallback_group",
        deserialize_with = "deserialize_fallback_group"
    )]
    pub fallback_group: fonts::FallbackGroup,
}

pub type LanguageSet = Vec<String>;

#[derive(Debug, Deserialize)]
pub struct Font {
    pub asset: PathBuf,

    #[serde(default = "default_index")]
    pub index: u32,

    #[serde(default = "default_slant", deserialize_with = "deserialize_slant")]
    pub slant: fonts::Slant,

    #[serde(default = "default_weight")]
    pub weight: u32,

    #[serde(default = "default_width")]
    pub width: u32,

    #[serde(
        default = "default_language",
        deserialize_with = "deserialize_language"
    )]
    pub language: LanguageSet,
}

fn default_fallback() -> bool {
    false
}

fn default_fallback_group() -> fonts::FallbackGroup {
    fonts::FallbackGroup::None
}

fn default_index() -> u32 {
    0
}

fn default_slant() -> fonts::Slant {
    fonts::Slant::Upright
}

fn default_weight() -> u32 {
    400
}

fn default_width() -> u32 {
    5
}

fn default_language() -> LanguageSet {
    LanguageSet::new()
}

fn deserialize_fallback_group<'d, D>(deserializer: D) -> Result<fonts::FallbackGroup, D::Error>
where
    D: Deserializer<'d>,
{
    let s = String::deserialize(deserializer)?;
    match s.as_str() {
        "serif" => Ok(fonts::FallbackGroup::Serif),
        "sans_serif" | "sans-serif" => Ok(fonts::FallbackGroup::SansSerif),
        "monospace" => Ok(fonts::FallbackGroup::Monospace),
        "cursive" => Ok(fonts::FallbackGroup::Cursive),
        "fantasy" => Ok(fonts::FallbackGroup::Fantasy),
        x => Err(D::Error::custom(format!(
            "unknown value for fallback_group in manifest: {}",
            x
        ))),
    }
}

fn deserialize_slant<'d, D>(deserializer: D) -> Result<fonts::Slant, D::Error>
where
    D: Deserializer<'d>,
{
    let s = String::deserialize(deserializer)?;
    match s.as_str() {
        "upright" => Ok(fonts::Slant::Upright),
        "italic" => Ok(fonts::Slant::Italic),
        x => Err(D::Error::custom(format!(
            "unknown value for slant in manifest: {}",
            x
        ))),
    }
}

// Helper used to deserialize language field for a font. Language field can
// contain either a single string or an array of strings.
fn deserialize_language<'d, D>(deserializer: D) -> Result<LanguageSet, D::Error>
where
    D: Deserializer<'d>,
{
    struct LanguageSetVisitor;

    impl<'de> de::Visitor<'de> for LanguageSetVisitor {
        type Value = Vec<String>;

        fn expecting(&self, formatter: &mut fmt::Formatter) -> fmt::Result {
            formatter.write_str("string or list of strings")
        }

        fn visit_str<E>(self, s: &str) -> Result<Self::Value, E>
        where
            E: de::Error,
        {
            Ok(vec![s.to_owned()])
        }

        fn visit_seq<S>(self, seq: S) -> Result<Self::Value, S::Error>
        where
            S: de::SeqAccess<'de>,
        {
            Deserialize::deserialize(de::value::SeqAccessDeserializer::new(seq))
        }
    }

    deserializer.deserialize_any(LanguageSetVisitor)
}

impl FontsManifest {
    pub fn load_from_file(path: &Path) -> Result<FontsManifest, failure::Error> {
        let path = fs::canonicalize(path)?;
        let base_dir = path
            .parent()
            .ok_or(format_err!("Invalid manifest path: {}", path.display()))?;

        let mut f = File::open(path.clone())?;
        let mut contents = String::new();
        f.read_to_string(&mut contents)?;

        let mut manifest: FontsManifest = serde_json::from_str(&contents)
            .context(format!("Failed to load {}", path.display()))?;

        // Make sure all paths are absolute.
        for family in manifest.families.iter_mut() {
            for font in family.fonts.iter_mut() {
                if font.asset.is_relative() {
                    font.asset = base_dir.join(font.asset.clone());
                }
            }
        }

        Ok(manifest)
    }
}
