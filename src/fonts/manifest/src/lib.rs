// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Library for deserializing and converting Fuchsia font manifests.

#[macro_use]
pub mod serde_ext;
mod v1_to_v2;
pub mod v2;

use {
    crate::{serde_ext::*, v2::FontsManifest as FontsManifestV2},
    anyhow::Error,
    char_set::CharSet,
    clonable_error::ClonableError,
    fidl_fuchsia_fonts::{GenericFontFamily, Slant, Width, WEIGHT_NORMAL},
    fuchsia_url::pkg_url::PkgUrl,
    offset_string::OffsetString,
    serde::{
        de::{self, Deserialize, Deserializer, Error as DeError},
        ser::{Serialize, Serializer},
    },
    serde_derive::{Deserialize, Serialize},
    serde_json,
    std::{
        convert::TryFrom,
        fmt,
        fs::{self, File},
        io::BufReader,
        path::{Path, PathBuf},
    },
    thiserror::Error,
};

/// The various possible versions of the fonts manifest.
#[derive(Debug, Deserialize, Serialize)]
#[serde(tag = "version")]
// This is a trick to implement custom deserialization that sets a default "version" field if
// missing, without introducing another `Deserialize` struct as an additional wrapper.
// See https://github.com/serde-rs/serde/issues/1174#issuecomment-372411280 for another example.
#[serde(remote = "Self")]
pub enum FontManifestWrapper {
    /// Version 1. Deprecated.
    #[serde(rename = "1")]
    Version1(FontsManifest),

    /// Version 2. New and shiny. And most importantly, machine-generated, so production manifests
    /// can never get out of sync with the schema. (Checked in test manifests still can.)
    #[serde(rename = "2")]
    Version2(FontsManifestV2),
}

impl<'de> Deserialize<'de> for FontManifestWrapper {
    fn deserialize<D>(deserializer: D) -> Result<Self, <D as Deserializer<'de>>::Error>
    where
        D: Deserializer<'de>,
    {
        // If there's no version field, assume that the version is "1".
        let mut map = serde_json::value::Map::deserialize(deserializer)?;
        if map.get("version").is_none() {
            map.insert("version".to_string(), serde_json::Value::String("1".to_string()));
        }

        // This is not a recursive call. Here, `FontManifestWrapper` is the "remote" type.
        Ok(FontManifestWrapper::deserialize(serde_json::Value::Object(map))
            .map_err(de::Error::custom)?)
    }
}

impl Serialize for FontManifestWrapper {
    fn serialize<S>(&self, serializer: S) -> Result<<S as Serializer>::Ok, <S as Serializer>::Error>
    where
        S: Serializer,
    {
        // This is not a recursive call. Here, `FontManifestWrapper` is the "remote" type.
        FontManifestWrapper::serialize(self, serializer)
    }
}

/// A collection of metadata about font families.
#[derive(Debug, Deserialize, Serialize)]
pub struct FontsManifest {
    /// List of font families.
    pub families: Vec<Family>,
}

/// Metadata about a single font family.
#[derive(Debug, Deserialize, Serialize)]
pub struct Family {
    /// Family name
    pub family: String,

    /// List of alternate names
    pub aliases: Option<Vec<String>>,

    /// List of font assets and typeface metadata belonging to this family
    pub fonts: Vec<Font>,

    /// Whether this font can serve as a fallback when other fonts are missing.
    #[serde(default = "default_fallback")]
    pub fallback: bool,

    /// The generic group of font families to which this family belongs.
    #[serde(
        alias = "fallback_group",
        default = "default_generic_family",
        with = "OptGenericFontFamily"
    )]
    pub generic_family: Option<GenericFontFamily>,
}

/// Collection of BCP-47 language IDs.
pub type LanguageSet = Vec<String>;

/// A reference to a font asset (file path) and metadata about one of the typefaces contained in the
/// file.
#[derive(Debug, Deserialize, Serialize)]
pub struct Font {
    /// Path to the font file.
    pub asset: PathBuf,

    /// Index of the typeface within the file.
    #[serde(default = "default_index")]
    pub index: u32,

    /// The typeface's slant.
    #[serde(default = "default_slant", with = "SlantDef")]
    pub slant: Slant,

    /// The typeface's weight.
    #[serde(default = "default_weight")]
    pub weight: u16,

    /// The typeface's width.
    #[serde(default = "default_width", with = "WidthDef")]
    pub width: Width,

    /// List of BCP-47 language IDs explicitly supported by the typeface.
    #[serde(
        alias = "language",
        default = "default_languages",
        deserialize_with = "deserialize_languages"
    )]
    pub languages: LanguageSet,

    /// Fuchsia Package URL at which this font file can also be found.
    #[serde(default = "default_package")]
    pub package: Option<PkgUrl>,

    /// Character set supported by the typeface.
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
    /// Tries to deserialize a v1 or v2 manifest from a JSON file.
    ///
    /// (Also performs some file path cleanup in v1 manifests.)
    pub fn load_from_file(path: &Path) -> Result<FontManifestWrapper, anyhow::Error> {
        let path = fs::canonicalize(path)?;
        let base_dir =
            path.parent().ok_or_else(|| ManifestLoadError::InvalidPath { path: path.clone() })?;

        let file = File::open(&path)
            .map_err(|e| ManifestLoadError::ReadError { path: path.clone(), cause: e })?;

        let mut wrapper: FontManifestWrapper =
            serde_json::from_reader(BufReader::new(file)).map_err(|e| {
                ManifestLoadError::ParseError { path: path.clone(), cause: Error::from(e).into() }
            })?;

        // Make sure all paths are absolute in v1.
        // (In v2, the schema for `LocalFileLocator` is specific about path types.)
        if let FontManifestWrapper::Version1(v1) = &mut wrapper {
            for family in v1.families.iter_mut() {
                for font in family.fonts.iter_mut() {
                    if font.asset.is_relative() {
                        font.asset = base_dir.join(font.asset.clone());
                    }
                }
            }
        }

        Ok(wrapper)
    }
}

/// Errors when loading manifest
#[derive(Debug, Error)]
pub enum ManifestLoadError {
    /// Invalid manifest path
    #[error("Invalid manifest path: {:?}", path)]
    InvalidPath {
        /// Manifest file path
        path: PathBuf,
    },

    /// IO error when reading the manifest
    #[error("Failed to read {:?}: {:?}", path, cause)]
    ReadError {
        /// Manifest file path
        path: PathBuf,
        /// Root cause of error
        #[source]
        cause: std::io::Error,
    },

    /// Invalid syntax in the manifest file
    #[error("Failed to parse {:?}: {:?}", path, cause)]
    ParseError {
        /// Manifest file path
        path: PathBuf,
        /// Root cause of error
        #[source]
        cause: ClonableError,
    },
}

#[cfg(test)]
mod tests {
    use {super::*, anyhow::Error, matches::assert_matches};

    #[test]
    fn test_deserialize_manifest_version_v1_implicit() -> Result<(), Error> {
        let json = r#"
        {
            "families": []
        }
        "#;

        let wrapper: FontManifestWrapper = serde_json::from_str(json)?;
        assert_matches!(wrapper, FontManifestWrapper::Version1(_));
        Ok(())
    }

    #[test]
    fn test_deserialize_manifest_version_v2() -> Result<(), Error> {
        let json = r#"
        {
            "version": "2",
            "families": []
        }
        "#;

        let wrapper: FontManifestWrapper = serde_json::from_str(json)?;
        assert_matches!(wrapper, FontManifestWrapper::Version2(_));
        Ok(())
    }
}
