// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{self, format_err, ResultExt},
    fidl_fuchsia_fonts as fonts,
    lazy_static::lazy_static,
    regex::Regex,
    serde::de::{self, Deserialize, Deserializer, Error},
    serde_derive::Deserialize,
    serde_json,
    std::{
        fmt,
        fs::{self, File},
        io::Read,
        path::{Path, PathBuf},
    },
};

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
        alias = "fallback_group",
        default = "default_generic_family",
        deserialize_with = "deserialize_generic_family"
    )]
    pub generic_family: Option<fonts::GenericFontFamily>,
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
    pub weight: u16,

    #[serde(default = "default_width", deserialize_with = "deserialize_width")]
    pub width: fonts::Width,

    #[serde(
        alias = "language",
        default = "default_languages",
        deserialize_with = "deserialize_languages"
    )]
    pub languages: LanguageSet,
}

fn default_fallback() -> bool {
    false
}

fn default_generic_family() -> Option<fonts::GenericFontFamily> {
    None
}

fn default_index() -> u32 {
    0
}

fn default_slant() -> fonts::Slant {
    fonts::Slant::Upright
}

fn default_weight() -> u16 {
    fonts::WEIGHT_NORMAL
}

fn default_width() -> fonts::Width {
    fonts::Width::Normal
}

fn default_languages() -> LanguageSet {
    LanguageSet::new()
}

lazy_static! {
    static ref SEPARATOR_REGEX: Regex = Regex::new(r"[_ ]").unwrap();
}

fn deserialize_generic_family<'d, D>(
    deserializer: D,
) -> Result<Option<fonts::GenericFontFamily>, D::Error>
where
    D: Deserializer<'d>,
{
    use fonts::GenericFontFamily::*;

    let s = String::deserialize(deserializer)?;
    let s: String = SEPARATOR_REGEX.replace_all(&s, "-").to_string();
    match s.as_str() {
        "serif" => Ok(Some(Serif)),
        "sans-serif" => Ok(Some(SansSerif)),
        "monospace" => Ok(Some(Monospace)),
        "cursive" => Ok(Some(Cursive)),
        "fantasy" => Ok(Some(Fantasy)),
        "system-ui" => Ok(Some(SystemUi)),
        "emoji" => Ok(Some(Emoji)),
        "math" => Ok(Some(Math)),
        "fangsong" => Ok(Some(Fangsong)),
        x => Err(D::Error::custom(format!("unknown value for generic_family in manifest: {}", x))),
    }
}

fn deserialize_slant<'d, D>(deserializer: D) -> Result<fonts::Slant, D::Error>
where
    D: Deserializer<'d>,
{
    use fonts::Slant::*;

    let s = String::deserialize(deserializer)?;
    match s.as_str() {
        "upright" => Ok(Upright),
        "italic" => Ok(Italic),
        "oblique" => Ok(Oblique),
        x => Err(D::Error::custom(format!("unknown value for slant in manifest: {}", x))),
    }
}

fn deserialize_width<'d, D>(deserializer: D) -> Result<fonts::Width, D::Error>
where
    D: Deserializer<'d>,
{
    use fonts::Width::*;

    let s = String::deserialize(deserializer)?;

    if let Ok(numeric) = s.parse::<u16>() {
        match fonts::Width::from_primitive(numeric.into()) {
            Some(value) => Ok(value),
            None => {
                Err(D::Error::custom(format!("unknown value for width in manifest: {}", numeric)))
            }
        }
    } else {
        let s: String = SEPARATOR_REGEX.replace_all(&s, "-").to_string();
        match s.as_str() {
            "ultra-condensed" => Ok(UltraCondensed),
            "extra-condensed" => Ok(ExtraCondensed),
            "condensed" => Ok(Condensed),
            "semi-condensed" => Ok(SemiCondensed),
            "normal" => Ok(Normal),
            "semi-expanded" => Ok(SemiExpanded),
            "expanded" => Ok(Expanded),
            "extra-expanded" => Ok(ExtraExpanded),
            "ultra-expanded" => Ok(UltraExpanded),
            x => Err(D::Error::custom(format!("unknown value for width in manifest: {}", x))),
        }
    }
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

        fn expecting(&self, formatter: &mut fmt::Formatter) -> fmt::Result {
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

impl FontsManifest {
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
