// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Contains local mirrors and Serde annotations for FIDL types.
//! See https://serde.rs/remote-derive.html.

use {
    fidl_fuchsia_fonts::{GenericFontFamily, Slant, Width},
    serde_derive::{Deserialize, Serialize},
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
///     use serde_derive::{Deserialize, Serialize}
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
/// ```
///
/// Parameters:
/// - `module`: Name of the generated module, e.g. `OptFidlTypeSerde`.
/// - `remote_type`: Name of the remote type being mirrored, e.g. `SomeFidlType`.
/// - `local_type`: Name of the local type that's mirroring the remote type, e.g. `SomeFidlTypeDef`.
/// - `local_type_str`: The same
macro_rules! derive_opt {
    ($module:ident, $remote_type:ty, $local_type:ty, $local_type_str:expr) => {
        #[allow(non_snake_case, dead_code)]
        pub(crate) mod $module {
            use {
                super::*,
                serde::{Deserialize, Deserializer, Serialize, Serializer},
                serde_derive::{Deserialize, Serialize},
            };

            /// Implementation of Serde's serialize
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

            /// Implementation of Serde's deserialize
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

/// Local mirror of [`fidl_fuchsia_fonts::GenericFontFamily`], for use in JSON serialization.
///
/// Serialized values are in _kebab-case_, e.g. `"sans-serif"`.
#[derive(Serialize, Deserialize)]
#[serde(remote = "GenericFontFamily", rename_all = "kebab-case")]
pub(crate) enum GenericFontFamilyDef {
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
/// Serialize values are _lowercase_, e.g. `"italic"`.
#[derive(Serialize, Deserialize)]
#[serde(remote = "Slant", rename_all = "lowercase")]
pub(crate) enum SlantDef {
    Upright,
    Italic,
    Oblique,
}

/// Local mirror of [`fidl_fuchsia_fonts::Width`], for use in JSON serialization.
///
/// Serialized values are in _kebab-case_, e.g. `"semi-condensed"`.
#[derive(Serialize, Deserialize)]
#[serde(remote = "Width", rename_all = "kebab-case")]
pub(crate) enum WidthDef {
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
