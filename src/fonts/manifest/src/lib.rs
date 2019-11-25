// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[macro_use]
pub mod serde_ext;
mod v1_to_v2;
pub mod v2;

use {
    crate::{serde_ext::*, v2::FontsManifest as FontsManifestV2},
    char_set::CharSet,
    failure::{self, format_err, ResultExt},
    fidl_fuchsia_fonts::{GenericFontFamily, Slant, Width, WEIGHT_NORMAL},
    fuchsia_url::pkg_url::PkgUrl,
    offset_string::OffsetString,
    serde::{
        de::{self, Deserialize, Deserializer, Error},
        ser::{Serialize, Serializer},
    },
    serde_derive::{Deserialize, Serialize},
    serde_json,
    std::{
        convert::TryFrom,
        fmt,
        fs::{self, File},
        io::Read,
        path::{Path, PathBuf},
    },
};

#[derive(Deserialize, Serialize)]
#[serde(tag = "version")]
pub enum FontManifestWrapper {
    #[serde(rename = "1")]
    Version1(FontsManifest),
    #[serde(rename = "2")]
    Version2(FontsManifestV2),
}

// Following structs are used to parse manifest.json.
#[derive(Debug, Deserialize, Serialize)]
pub struct FontsManifest {
    pub families: Vec<Family>,
}

#[derive(Debug, Deserialize, Serialize)]
pub struct Family {
    pub family: String,

    pub aliases: Option<Vec<String>>,

    pub fonts: Vec<Font>,

    #[serde(default = "default_fallback")]
    pub fallback: bool,

    #[serde(
        alias = "fallback_group",
        default = "default_generic_family",
        with = "OptGenericFontFamily"
    )]
    pub generic_family: Option<GenericFontFamily>,
}

pub type LanguageSet = Vec<String>;

#[derive(Debug, Deserialize, Serialize)]
pub struct Font {
    pub asset: PathBuf,

    #[serde(default = "default_index")]
    pub index: u32,

    #[serde(default = "default_slant", with = "SlantDef")]
    pub slant: Slant,

    #[serde(default = "default_weight")]
    pub weight: u16,

    #[serde(default = "default_width", with = "WidthDef")]
    pub width: Width,

    #[serde(
        alias = "language",
        default = "default_languages",
        deserialize_with = "deserialize_languages"
    )]
    pub languages: LanguageSet,

    #[serde(default = "default_package")]
    pub package: Option<PkgUrl>,

    #[serde(
        default,
        deserialize_with = "deserialize_code_points",
        serialize_with = "serialize_code_points"
    )]
    pub code_points: CharSet,
}

fn default_fallback() -> bool {
    false
}

fn default_generic_family() -> Option<GenericFontFamily> {
    None
}

fn default_index() -> u32 {
    0
}

fn default_slant() -> Slant {
    Slant::Upright
}

fn default_weight() -> u16 {
    WEIGHT_NORMAL
}

fn default_width() -> Width {
    Width::Normal
}

fn default_languages() -> LanguageSet {
    LanguageSet::new()
}

fn default_package() -> Option<PkgUrl> {
    None
}

/// Helper used to deserialize language field for a font. Language field can contain either a single
/// string or an array of strings.
fn deserialize_languages<'d, D>(deserializer: D) -> Result<LanguageSet, D::Error>
where
    D: Deserializer<'d>,
{
    struct LanguageSetVisitor;

    impl<'de> de::Visitor<'de> for LanguageSetVisitor {
        type Value = Vec<String>;

        fn expecting(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
            formatter.write_str("string or list of strings")
        }

        fn visit_str<E>(self, s: &str) -> Result<Self::Value, E>
        where
            E: de::Error,
        {
            Ok(vec![s.to_string()])
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

fn deserialize_code_points<'d, D>(deserializer: D) -> Result<CharSet, D::Error>
where
    D: Deserializer<'d>,
{
    let offset_string = OffsetString::deserialize(deserializer)?;
    CharSet::try_from(offset_string).map_err(|e| D::Error::custom(format!("{:?}", e)))
}

fn serialize_code_points<S>(code_points: &CharSet, serializer: S) -> Result<S::Ok, S::Error>
where
    S: Serializer,
{
    let offset_string: OffsetString = code_points.into();
    offset_string.serialize(serializer)
}

impl FontsManifest {
    // TODO(8892): Implement loading of v2 manifests.
    pub fn load_from_file(path: &Path) -> Result<FontsManifest, failure::Error> {
        let path = fs::canonicalize(path)?;
        let base_dir =
            path.parent().ok_or(format_err!("Invalid manifest path: {}", path.display()))?;

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
