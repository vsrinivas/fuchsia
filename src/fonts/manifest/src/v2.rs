// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::serde_ext::*,
    char_collection::CharCollection,
    fidl_fuchsia_fonts::{GenericFontFamily, Slant, Width, WEIGHT_NORMAL},
    fuchsia_url::pkg_url::PkgUrl,
    offset_string::OffsetString,
    serde::{
        de::{Deserialize, Deserializer, Error},
        ser::{Serialize, Serializer},
    },
    serde_derive::{Deserialize, Serialize},
    std::{convert::TryFrom, path::PathBuf},
};

/// Version 2 of the Font Manifest schema.
///
/// Less duplication than v1 schema. Intended for generation using a Rust tool, not for writing by
/// hand.
#[derive(Debug, Deserialize, Serialize, Eq, PartialEq)]
pub struct FontsManifest {
    /// List of families in the manifest.
    pub families: Vec<Family>,
}

/// Represents a font family, its metadata, and its font files.
#[derive(Debug, Deserialize, Serialize, Eq, PartialEq)]
pub struct Family {
    pub name: String,
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub aliases: Vec<String>,
    /// The generic font family that this font belongs to. If this is a specialty font (e.g. for
    /// custom icons), this should be set to `None`.
    #[serde(with = "OptGenericFontFamily")]
    pub generic_family: Option<GenericFontFamily>,
    /// Whether this family can be used as a fallback for other fonts (within the same generic
    /// family, or as a last resort).
    pub fallback: bool,
    /// Collection of font files that make up the font family.
    pub assets: Vec<Asset>,
}

/// Represents a single font file, which contains one or more [`Typeface`]s.
#[derive(Debug, Deserialize, Serialize, Eq, PartialEq)]
pub struct Asset {
    /// Asset identifier. Should be a valid file name, e.g. `"Roboto-Regular.ttf`.
    pub file_name: String,
    /// Where to find the file
    pub location: AssetLocation,
    /// List of typefaces in the file
    pub typefaces: Vec<Typeface>,
}

impl Asset {
    /// If the asset represents a local file, returns the package-relative path to the file.
    /// Otherwise, returns `None`.
    pub fn local_path(&self) -> Option<PathBuf> {
        match &self.location {
            AssetLocation::LocalFile(locator) => {
                Some(locator.directory.join(self.file_name.clone()))
            }
            _ => None,
        }
    }
}

/// Describes the location of a font asset.
#[derive(Debug, Deserialize, Serialize, Eq, PartialEq)]
pub enum AssetLocation {
    /// Indicates that the file is accessible through a file path in the font server's namespace
    /// (e.g. at `/config/data/fonts/`).
    #[serde(rename = "local")]
    LocalFile(LocalFileLocator),
    /// Indicates that the file is accessible in a separate font package (e.g.
    /// `fuchsia-pkg://fuchsia.com/font-package-roboto-regular-ttf`).
    #[serde(rename = "package")]
    Package(PackageLocator),
}

/// Describes the location of a local file asset. Used in conjunction with [`Asset::file_name`].
#[derive(Debug, Deserialize, Serialize, Eq, PartialEq)]
pub struct LocalFileLocator {
    /// Package-relative path to the file, excluding the file name
    pub directory: PathBuf,
}

/// Describes the location of a font asset that's part of a Fuchsia package. Used in conjunction
/// with [`Asset::file_name`].
#[derive(Debug, Deserialize, Serialize, Eq, PartialEq)]
pub struct PackageLocator {
    /// URL of just the package (not including the file name)
    pub package: PkgUrl,
    /// Type of package
    pub package_set: PackageSet,
}

/// Describes which set of dependencies a font package belongs to.
///
/// See https://fuchsia.dev/fuchsia-src/development/build/boards_and_products#dependency_sets.
#[derive(Debug, Deserialize, Serialize, Eq, PartialEq)]
#[serde(rename_all = "lowercase")]
pub enum PackageSet {
    /// Package is in the device's base image (the "base set" of packages)
    Base,
    /// Package is available ephemerally (the "universe set" of packages)
    Universe,
}

/// Describes a single typeface within a font file
#[derive(Debug, Deserialize, Serialize, Eq, PartialEq)]
pub struct Typeface {
    /// Index of the typeface in the file. If the file only contains a single typeface, this will
    /// always be `0`.
    #[serde(default = "Typeface::default_index")]
    pub index: u32,

    /// List of languages that the typeface supports, in BCP-47 format.
    ///
    /// Example: `["en", "zh-Hant", "sr-Cyrl"]`
    #[serde(default = "Typeface::default_languages", skip_serializing_if = "Vec::is_empty")]
    pub languages: Vec<String>,

    /// Text style of the typeface.
    #[serde(flatten)]
    pub style: Style,

    /// List of Unicode code points supported by the typeface.
    #[serde(with = "code_points_serde")]
    pub code_points: CharCollection,
}

impl Typeface {
    fn default_index() -> u32 {
        0
    }

    fn default_languages() -> Vec<String> {
        vec![]
    }
}

/// Used for de/serializing a `CharCollection`.
mod code_points_serde {
    use super::*;

    pub fn deserialize<'d, D>(deserializer: D) -> Result<CharCollection, D::Error>
    where
        D: Deserializer<'d>,
    {
        let offset_string = OffsetString::deserialize(deserializer)?;
        CharCollection::try_from(offset_string).map_err(|e| D::Error::custom(format!("{:?}", e)))
    }

    pub fn serialize<S>(code_points: &CharCollection, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        let offset_string: OffsetString = code_points.into();
        offset_string.serialize(serializer)
    }
}

/// Describes a typeface's style properties. Equivalent to [`fidl_fuchsia_fonts::Style2`], but all
/// fields are required.
#[derive(Debug, Deserialize, Serialize, Eq, PartialEq)]
pub struct Style {
    #[serde(default = "Style::default_slant", with = "SlantDef")]
    pub slant: Slant,
    #[serde(default = "Style::default_weight")]
    pub weight: u16,
    #[serde(default = "Style::default_width", with = "WidthDef")]
    pub width: Width,
}

impl Style {
    fn default_slant() -> Slant {
        Slant::Upright
    }

    fn default_weight() -> u16 {
        WEIGHT_NORMAL
    }

    fn default_width() -> Width {
        Width::Normal
    }
}
