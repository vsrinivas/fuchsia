// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Contains local mirrors and Serde annotations for FIDL types.
//! See https://serde.rs/remote-derive.html.

#![allow(missing_docs)]

use {
    fidl_fuchsia_fonts::{GenericFontFamily, Slant, Style2 as FidlStyle, Width},
    serde::{Deserialize, Serialize},
};

/// Generates Serde serialize and deserialize methods for types of `Option<T>`, where `T` is a type
/// defined in a remote crate and is mirrored in a local type (https://serde.rs/remote-derive.html).
///
/// On its own, Serde can't handle enums that wrap remote types. This works around that limitation
/// for the very specific case of `Option<T>`.
///
/// Expands to a mod that can be used by Serde's `"with"` attribute.
///
/// Example:
/// ```
/// mod remote_crate {
///     pub enum Magic8BallResponse {
///         Yes, Maybe, No, TryAgainLater
///     }
/// }
/// ```
/// ```
/// mod local_crate {
///     use remote_crate::Magic8BallResponse;
///     use serde::{Deserialize, Serialize};
///
///     #[derive(Deserialize, Serialize)]
///     #[serde(with = "Magic8BallResponse")]
///     pub enum Magic8BallResponseDef {
///         Yes, Maybe, No, TryAgainLater
///     }
///
///     derive_opt!(
///         OptMagic8BallResponse,
///         Magic8BallResponse,
///         Magic8BallResponseDef,
///         "Magic8BallResponseDef");
///
///     #[derive(Deserialize, Serialize)]
///     pub struct Responses {
///         present: Magic8BallResponse,
///         #[serde(with = "OptMagic8BallResponse")]
///         hazy: Option<Magic8BallResponse>,
///     }
/// }
///
/// ```
///
/// Parameters:
/// - `module`: Name of the generated module, e.g. `OptFidlTypeSerde`.
/// - `remote_type`: Name of the remote type being mirrored, e.g. `SomeFidlType`.
/// - `local_type`: Name of the local type that's mirroring the remote type, e.g. `SomeFidlTypeDef`.
/// - `local_type_str`: The same as `local_type`, but wrapped in quotes.
macro_rules! derive_opt {
    ($module:ident, $remote_type:ty, $local_type:ty, $local_type_str:expr) => {
        #[allow(non_snake_case, dead_code, missing_docs, explicit_outlives_requirements)]
        pub mod $module {
            use {
                super::*,
                serde::{Deserialize, Deserializer, Serialize, Serializer},
            };

            #[doc = "Implementation of Serde's serialize"]
            pub fn serialize<S>(
                value: &Option<$remote_type>,
                serializer: S,
            ) -> Result<S::Ok, S::Error>
            where
                S: Serializer,
            {
                #[derive(Serialize)]
                struct Wrapper<'a>(#[serde(with = $local_type_str)] &'a $remote_type);
                value.as_ref().map(Wrapper).serialize(serializer)
            }

            #[doc = "Implementation of Serde's deserialize"]
            pub fn deserialize<'de, D>(deserializer: D) -> Result<Option<$remote_type>, D::Error>
            where
                D: Deserializer<'de>,
            {
                #[derive(Deserialize)]
                struct Wrapper(#[serde(with = $local_type_str)] $remote_type);

                let helper = Option::deserialize(deserializer)?;
                Ok(helper.map(|Wrapper(external)| external))
            }
        }
    };
}

/// Local mirror of [`fidl_fuchsia_fonts::Style2`], for use in JSON serialization.
///
/// We can't just use a Serde remote type for `Style2` here because there are lots of other required
/// traits that are not derived for FIDL tables.
///
/// `Ord` is derived for use with the manifest generator's `TryMergeGroups` trait. The ordering is
/// by necessity arbitrary, comparing each field in sequence, recursively comparing enum values and
/// numbers.
#[allow(missing_docs)]
#[derive(Clone, Debug, Default, Deserialize, Serialize, Eq, PartialEq, Hash, Ord, PartialOrd)]
pub struct StyleOptions {
    #[serde(default, with = "OptSlant", skip_serializing_if = "Option::is_none")]
    pub slant: Option<Slant>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub weight: Option<u16>,
    #[serde(default, with = "OptWidth", skip_serializing_if = "Option::is_none")]
    pub width: Option<Width>,
}

impl From<FidlStyle> for StyleOptions {
    fn from(fidl_style: FidlStyle) -> Self {
        StyleOptions { slant: fidl_style.slant, weight: fidl_style.weight, width: fidl_style.width }
    }
}

impl From<StyleOptions> for FidlStyle {
    fn from(style_options: StyleOptions) -> Self {
        FidlStyle {
            slant: style_options.slant,
            weight: style_options.weight,
            width: style_options.width,
        }
    }
}

/// Local mirror of [`fidl_fuchsia_fonts::GenericFontFamily`], for use in JSON serialization.
///
/// Serialized values are in _kebab-case_, e.g. `"sans-serif"`.
#[allow(missing_docs)]
#[derive(Clone, Debug, Deserialize, Serialize, Eq, PartialEq, Hash)]
#[serde(remote = "GenericFontFamily", rename_all = "kebab-case")]
pub enum GenericFontFamilyDef {
    Serif,
    SansSerif,
    Monospace,
    Cursive,
    Fantasy,
    SystemUi,
    Emoji,
    Math,
    Fangsong,
}

derive_opt!(OptGenericFontFamily, GenericFontFamily, GenericFontFamilyDef, "GenericFontFamilyDef");

/// Local mirror of [`fidl_fuchsia_fonts::Slant`], for use in JSON serialization.
///
/// Serialized values are _lowercase_, e.g. `"italic"`.
#[allow(missing_docs)]
#[derive(Clone, Debug, Deserialize, Serialize, Eq, PartialEq, Hash)]
#[serde(remote = "Slant", rename_all = "lowercase")]
pub enum SlantDef {
    Upright,
    Italic,
    Oblique,
}

derive_opt!(OptSlant, Slant, SlantDef, "SlantDef");

/// Local mirror of [`fidl_fuchsia_fonts::Width`], for use in JSON serialization.
///
/// Serialized values are in _kebab-case_, e.g. `"semi-condensed"`.
#[allow(missing_docs)]
#[derive(Clone, Debug, Deserialize, Serialize, Eq, PartialEq, Hash)]
#[serde(remote = "Width", rename_all = "kebab-case")]
pub enum WidthDef {
    UltraCondensed,
    ExtraCondensed,
    Condensed,
    SemiCondensed,
    Normal,
    SemiExpanded,
    Expanded,
    ExtraExpanded,
    UltraExpanded,
}

derive_opt!(OptWidth, Width, WidthDef, "WidthDef");
