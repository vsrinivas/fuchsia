// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::serde_ext::*,
    char_set::CharSet,
    fidl_fuchsia_fonts::{GenericFontFamily, Slant, Width, WEIGHT_NORMAL},
    fuchsia_url::pkg_url::PkgUrl,
    offset_string::OffsetString,
    serde::{
        de::{Deserialize, Deserializer, Error},
        ser::{Serialize, Serializer},
    },
    serde_derive::{Deserialize, Serialize},
    std::{cmp::Ordering, convert::TryFrom, path::PathBuf},
    unicase::UniCase,
};

/// Version 2 of the Font Manifest schema.
///
/// Less duplication than v1 schema. Intended for generation using a Rust tool, not for writing by
/// hand.
#[derive(Clone, Debug, Deserialize, Serialize, Eq, PartialEq, Hash)]
pub struct FontsManifest {
    /// List of families in the manifest.
    pub families: Vec<Family>,
}

/// Represents a font family, its metadata, and its font files.
#[derive(Clone, Debug, Deserialize, Serialize, Eq, PartialEq, Hash)]
pub struct Family {
    pub name: String,
    /// Alternate names for the font family.
    // During de/serialization, omitted `aliases` are treated as an empty array and vice-versa.
    #[serde(skip_serializing_if = "Vec::is_empty", default)]
    pub aliases: Vec<FontFamilyAlias>,
    /// The generic font family that this font belongs to. If this is a specialty font (e.g. for
    /// custom icons), this should be set to `None`.
    #[serde(with = "OptGenericFontFamily", default)]
    pub generic_family: Option<GenericFontFamily>,
    /// Whether this family can be used as a fallback for other fonts (within the same generic
    /// family, or as a last resort).
    pub fallback: bool,
    /// Collection of font files that make up the font family.
    pub assets: Vec<Asset>,
}

/// Represents a font family alias and optional style properties that should be applied when
/// treating the alias as the canonical family.
///
/// For example, the font family "Roboto" might have an alias of the form:
/// ```text
/// FontFamilyAlias {
///     name: "Roboto Condensed",
///     style: StyleOptions {
///         width: Width::Condensed
///     }
/// }
/// ```
#[derive(Clone, Debug, Eq, PartialEq, Hash, Deserialize, Serialize)]
#[serde(from = "StringOrFontFamilyAliasHelper", into = "StringOrFontFamilyAliasHelper")]
pub struct FontFamilyAlias {
    alias: UniCase<String>,
    style: StyleOptions,
}

impl FontFamilyAlias {
    /// Creates a new `FontFamilyAlias` without any style overrides.
    pub fn new(name: impl AsRef<str>) -> Self {
        Self::with_style(name, StyleOptions::default())
    }

    /// Creates a new `FontFamilyAlias` with style overrides.
    pub fn with_style(name: impl AsRef<str>, style: impl Into<StyleOptions>) -> Self {
        FontFamilyAlias { alias: UniCase::new(name.as_ref().to_string()), style: style.into() }
    }

    pub fn alias(&self) -> &str {
        self.alias.as_ref()
    }

    pub fn style(&self) -> &StyleOptions {
        &self.style
    }

    pub fn has_style(&self) -> bool {
        self.style != StyleOptions::default()
    }
}

impl Ord for FontFamilyAlias {
    fn cmp(&self, other: &Self) -> Ordering {
        self.alias.cmp(&other.alias)
    }
}

impl PartialOrd for FontFamilyAlias {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

impl<T: AsRef<str>> From<T> for FontFamilyAlias {
    fn from(s: T) -> Self {
        FontFamilyAlias::new(s)
    }
}

/// Serialization helper for `FontFamilyAlias`.
#[derive(Deserialize, Serialize)]
struct FontFamilyAliasHelper {
    alias: String,
    #[serde(flatten)]
    style: StyleOptions,
}

/// Serialization helper for `FontFamilyAlias`. Allows aliases to be either plain strings or structs
/// with style options.
#[derive(Deserialize)]
#[serde(untagged)]
enum StringOrFontFamilyAliasHelper {
    String(String),
    // We can't directly use `FontFamilyAlias` here because that would create an infinite loop in
    // the serde deserializer.
    Alias(FontFamilyAliasHelper),
}

impl From<StringOrFontFamilyAliasHelper> for FontFamilyAlias {
    fn from(wrapper: StringOrFontFamilyAliasHelper) -> Self {
        match wrapper {
            StringOrFontFamilyAliasHelper::String(s) => FontFamilyAlias::new(s),
            StringOrFontFamilyAliasHelper::Alias(a) => {
                FontFamilyAlias::with_style(a.alias, a.style)
            }
        }
    }
}

impl From<FontFamilyAlias> for StringOrFontFamilyAliasHelper {
    fn from(source: FontFamilyAlias) -> Self {
        if source.has_style() {
            StringOrFontFamilyAliasHelper::Alias(FontFamilyAliasHelper {
                alias: source.alias.to_string(),
                style: source.style.clone(),
            })
        } else {
            StringOrFontFamilyAliasHelper::String(source.alias.to_string())
        }
    }
}

impl Serialize for StringOrFontFamilyAliasHelper {
    fn serialize<S>(&self, serializer: S) -> Result<<S as Serializer>::Ok, <S as Serializer>::Error>
    where
        S: Serializer,
    {
        match self {
            StringOrFontFamilyAliasHelper::String(s) => s.serialize(serializer),
            StringOrFontFamilyAliasHelper::Alias(a) => a.serialize(serializer),
        }
    }
}

/// Represents a single font file, which contains one or more [`Typeface`]s.
#[derive(Clone, Debug, Deserialize, Serialize, Eq, PartialEq, Hash)]
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
#[derive(Clone, Debug, Deserialize, Serialize, Eq, PartialEq, Hash)]
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
#[derive(Clone, Debug, Deserialize, Serialize, Eq, PartialEq, Hash)]
pub struct LocalFileLocator {
    /// Package-relative path to the file, excluding the file name
    pub directory: PathBuf,
}

/// Describes the location of a font asset that's part of a Fuchsia package. Used in conjunction
/// with [`Asset::file_name`].
#[derive(Clone, Debug, Deserialize, Serialize, Eq, PartialEq, Hash)]
pub struct PackageLocator {
    /// URL of just the package (not including the file name)
    pub url: PkgUrl,
    /// Type of package
    pub set: PackageSet,
}

/// Describes which set of dependencies a font package belongs to.
///
/// See https://fuchsia.dev/fuchsia-src/development/build/boards_and_products#dependency_sets.
#[derive(Clone, Debug, Deserialize, Serialize, Eq, PartialEq, Hash)]
#[serde(rename_all = "lowercase")]
pub enum PackageSet {
    /// Package is in the device's base image (the "base set" of packages)
    Base,
    /// Package is available ephemerally (the "universe set" of packages)
    Universe,
}

/// Describes a single typeface within a font file
#[derive(Clone, Debug, Deserialize, Serialize, Eq, PartialEq, Hash)]
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
    pub code_points: CharSet,
}

impl Typeface {
    fn default_index() -> u32 {
        0
    }

    fn default_languages() -> Vec<String> {
        vec![]
    }
}

/// Used for de/serializing a `CharSet`.
mod code_points_serde {
    use super::*;

    pub fn deserialize<'d, D>(deserializer: D) -> Result<CharSet, D::Error>
    where
        D: Deserializer<'d>,
    {
        let offset_string = OffsetString::deserialize(deserializer)?;
        CharSet::try_from(offset_string).map_err(|e| D::Error::custom(format!("{:?}", e)))
    }

    pub fn serialize<S>(code_points: &CharSet, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        let offset_string: OffsetString = code_points.into();
        offset_string.serialize(serializer)
    }
}

/// Describes a typeface's style properties. Equivalent to [`fidl_fuchsia_fonts::Style2`], but all
/// fields are required.
#[derive(Clone, Debug, Deserialize, Serialize, Eq, PartialEq, Hash)]
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

#[cfg(test)]
mod tests {
    use super::*;
    use failure::Error;

    #[test]
    fn test_deserialize_font_family_alias_flat_alias() -> Result<(), Error> {
        let json = r#""Beta Sans""#;
        let result: FontFamilyAlias = serde_json::from_str(json)?;
        assert_eq!(result, FontFamilyAlias::new("Beta Sans"));
        Ok(())
    }

    #[test]
    fn test_deserialize_font_family_alias_with_style() -> Result<(), Error> {
        let json = r#"
        {
            "alias": "Beta Condensed",
            "width": "condensed"
        }
        "#;
        let result: FontFamilyAlias = serde_json::from_str(json)?;
        assert_eq!(
            result,
            FontFamilyAlias::with_style(
                "Beta Condensed",
                StyleOptions { width: Some(Width::Condensed), ..Default::default() }
            )
        );
        Ok(())
    }
}
