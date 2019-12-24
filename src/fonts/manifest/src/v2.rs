// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Version 2 of the Font Manifest schema.

use {
    crate::serde_ext::*,
    anyhow::{ensure, Error},
    char_set::CharSet,
    fidl_fuchsia_fonts::{GenericFontFamily, Slant, Width, WEIGHT_NORMAL},
    fuchsia_url::pkg_url::PkgUrl,
    itertools::Itertools,
    offset_string::OffsetString,
    serde::{
        de::{Deserialize, Deserializer, Error as DeError},
        ser::{Serialize, Serializer},
    },
    serde_derive::{Deserialize, Serialize},
    std::{convert::TryFrom, iter, ops::Deref, path::PathBuf},
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
    /// Canonical name of the font family.
    pub name: String,
    /// Alternate names for the font family.
    // During de/serialization, omitted `aliases` are treated as an empty array and vice-versa.
    #[serde(skip_serializing_if = "Vec::is_empty", default)]
    pub aliases: Vec<FontFamilyAliasSet>,
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

/// Represents a set of font family aliases, and optionally, typeface properties that should be
/// applied when treating those aliases as the canonical family.
///
/// For example, the font family `"Roboto"` might have one alias set of the form:
/// ```json
/// {
///   "names": [ "Roboto Condensed" ],
///   "width": "condensed"
/// }
/// ```
/// This means that when a client requests the family `"Roboto Condensed"`, the font server will
/// treat that as a request for `"Roboto"` with `Width::Condensed`.
///
/// The font family `"Noto Sans CJK"` might have aliases of the form:
/// ```json
/// [
///   {
///     "names": [ "Noto Sans CJK KR", "Noto Sans KR" ],
///     "languages": [ "ko" ]
///   },
///   {
///     "names": [ "Noto Sans CJK JP", "Noto Sans JP" ],
///     "languages: [ "ja" ]
///   }
/// ]
/// ```
///
/// When a client requests `"Noto Sans CJK JP"` or `"Noto Sans JP"`, the font server will look under
/// `"Noto Sans CJK"` for typefaces that support Japanese (`"ja"`).
///
/// Create using [`FontFamilyAliasSet::new`] if the aliases will map to typeface property overrides,
/// or [`FontFamilyAliasSet::without_overrides`] to create a plain set of aliases.
///
/// When loaded by the font server, a `FontFamilyAliasSet` is expanded over all the `names`,
/// creating an alias entry for every name, with identical `style` and `languages` values.
#[derive(Clone, Debug, Eq, PartialEq, Hash, Deserialize, Serialize)]
pub struct FontFamilyAliasSet {
    /// Alternate names for the font family.
    #[serde(
        deserialize_with = "FontFamilyAliasSet::deserialize_names",
        serialize_with = "FontFamilyAliasSet::serialize_names"
    )]
    names: Vec<UniCase<String>>,
    /// If non-empty, style overrides that will automatically be inserted into `TypefaceQuery` when
    /// a client requests a font family using `alias` as the font family name.
    #[serde(flatten)]
    style: StyleOptions,
    /// If non-empty, the languages that will automatically be inserted into `TypefaceQuery` when a
    /// requests a font family using `alias` as the font family name. Language codes should be
    /// specified in descending order of preference (i.e. more preferred languages come first).
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    languages: Vec<String>,
}

impl FontFamilyAliasSet {
    /// Create a new `FontFamilyAliasSet` with one or more names, and with optional style and
    /// language overrides.
    ///
    /// - `names`: A list of one more alias names
    /// - `style`: Optionally, style overrides that are automatically applied to the typeface
    ///    request when one of the `names` is requested.
    /// - `languages`: Optionally, a list of language codes that is automatically applied to the
    ///   typeface request when one of the `names` is requested.
    ///   Do not sort the language codes. They are given in priority order, just as in
    ///   `TypefaceQuery.languages`.
    ///
    /// Examples:
    /// ```
    /// use manifest::v2::FontFamilyAliasSet;
    /// use manifest::serde_ext::StyleOptions;
    ///
    /// // Alias set for "Noto Sans CJK" for Traditional Chinese. Both `"Noto Sans CJK TC"` and
    /// // `"Noto Sans TC"` will serve as aliases that apply the languages `["zh-Hant", "zh-Bopo"]`
    /// // when requested.
    /// FontFamilyAliasSet::new(
    ///     vec!["Noto Sans CJK TC", "Noto Sans TC"],
    ///     StyleOptions::default(),
    ///     vec!["zh-Hant", "zh-Bopo"]);
    ///
    /// // Alias set for "Roboto Condensed". `"Roboto Condensed"` will serve as an alias that
    /// // applies the style options `width: condensed` when requested.
    /// FontFamilyAliasSet::new(
    ///     vec!["Roboto Condensed"],
    ///     StyleOptions {
    ///         width: Some(fidl_fuchsia_fonts::Width::Condensed),
    ///         ..Default::default()
    ///     },
    ///     vec![]);
    /// ```
    pub fn new(
        names: impl IntoIterator<Item = impl AsRef<str>>,
        style: impl Into<StyleOptions>,
        languages: impl IntoIterator<Item = impl AsRef<str>>,
    ) -> Result<Self, Error> {
        let set = FontFamilyAliasSet {
            names: Self::preprocess_names(names),
            style: style.into(),
            // Note: Do not sort the language codes. They are given in priority order, just as in
            // `TypefaceQuery.languages`.
            languages: languages.into_iter().map(|s| s.as_ref().to_string()).collect_vec(),
        };
        ensure!(!set.names.is_empty(), "Must contain at least one name");
        Ok(set)
    }

    /// Create a new `FontFamilyAliasSet` with one or more names, with no typeface property
    /// overrides.
    pub fn without_overrides(
        names: impl IntoIterator<Item = impl AsRef<str>>,
    ) -> Result<Self, Error> {
        Self::new(names, StyleOptions::default(), iter::empty::<String>())
    }

    /// Gets the alias names in this set.
    pub fn names(&self) -> impl Iterator<Item = &String> {
        (&self.names).iter().map(|uni| uni.deref())
    }

    /// Gets the style property overrides for this set of aliases (may be empty).
    pub fn style_overrides(&self) -> &StyleOptions {
        &self.style
    }

    /// Gets the language code overrides for this set of aliases (may be empty).
    pub fn language_overrides(&self) -> impl Iterator<Item = &String> {
        (&self.languages).iter()
    }

    /// Whether the alias set has any property overrides. If `false`, it's just a name alias.
    pub fn has_overrides(&self) -> bool {
        self.has_style_overrides() || self.has_language_overrides()
    }

    /// Whether the alias set has style overrides.
    pub fn has_style_overrides(&self) -> bool {
        self.style != StyleOptions::default()
    }

    /// Whether the alias set has language overrides.
    pub fn has_language_overrides(&self) -> bool {
        !self.languages.is_empty()
    }

    /// Helper for deserializing a vector of `UniCase` strings.
    fn deserialize_names<'de, D>(deserializer: D) -> Result<Vec<UniCase<String>>, D::Error>
    where
        D: Deserializer<'de>,
    {
        let names: Vec<String> = Vec::deserialize(deserializer)?;
        Ok(Self::preprocess_names(names))
    }

    /// Helper for serializing a vector of `UniCase` strings.
    fn serialize_names<S>(names: &Vec<UniCase<String>>, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        names.iter().map(|u| u.deref().to_string()).collect_vec().serialize(serializer)
    }

    /// Sort the names using case-insensitive sort.
    fn preprocess_names(names: impl IntoIterator<Item = impl AsRef<str>>) -> Vec<UniCase<String>> {
        names.into_iter().map(|name| UniCase::new(name.as_ref().to_string())).sorted().collect()
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

/// Describes the location of a font asset (excluding its file name).
#[derive(Clone, Debug, Deserialize, Serialize, Eq, PartialEq, Hash, Ord, PartialOrd)]
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
#[derive(Clone, Debug, Deserialize, Serialize, Eq, PartialEq, Hash, Ord, PartialOrd)]
pub struct LocalFileLocator {
    /// Package-relative path to the file, excluding the file name
    pub directory: PathBuf,
}

/// Describes the location of a font asset that's part of a Fuchsia package. Used in conjunction
/// with [`Asset::file_name`].
#[derive(Clone, Debug, Deserialize, Serialize, Eq, PartialEq, Hash, Ord, PartialOrd)]
pub struct PackageLocator {
    /// URL of just the package (not including the file name)
    pub url: PkgUrl,
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
#[allow(missing_docs)]
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
