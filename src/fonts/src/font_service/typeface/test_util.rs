// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

//! Utility methods for tests in `font_service::typeface`.

use {
    super::Typeface,
    crate::font_service::AssetId,
    char_set::CharSet,
    fidl_fuchsia_fonts::{
        GenericFontFamily, Slant, Style2, TypefaceQuery, TypefaceRequest, TypefaceRequestFlags,
        Width,
    },
    fidl_fuchsia_intl::LocaleId,
    manifest::{self, v2},
};

/// Creates a typeface with the given properties (with an `AssetId` of `0` and font index of `0`).
pub fn make_fake_typeface(
    width: Width,
    slant: Slant,
    weight: u16,
    languages: &[&str],
    char_set: &[u32],
    generic_family: impl Into<Option<GenericFontFamily>>,
) -> Typeface {
    // Prevent error if char_set is empty
    let char_set = if char_set.is_empty() { &[0] } else { char_set };
    Typeface::new(
        AssetId(0),
        v2::Typeface {
            index: 0,
            style: v2::Style { slant, weight, width },
            languages: languages.iter().map(|s| s.to_string()).collect(),
            code_points: CharSet::new(char_set.to_vec()),
        },
        generic_family.into(),
    )
    .unwrap() // Safe because char_set is not empty
}

/// Makes a `Typeface` where only the style is specified.
pub fn make_fake_typeface_style(width: Width, slant: Slant, weight: u16) -> Typeface {
    make_fake_typeface(width, slant, weight, &[], &[], None)
}

/// Makes a `TypefaceRequest` with the given properties (and defaults for the rest).
pub fn make_typeface_request<'a>(
    width: impl Into<Option<Width>>,
    slant: impl Into<Option<Slant>>,
    weight: impl Into<Option<u16>>,
    languages: impl Into<Option<&'a [&'a str]>>,
    flags: TypefaceRequestFlags,
    fallback_family: impl Into<Option<GenericFontFamily>>,
) -> TypefaceRequest {
    TypefaceRequest {
        query: Some(TypefaceQuery {
            family: None,
            style: Some(Style2 { weight: weight.into(), width: width.into(), slant: slant.into() }),
            code_points: None,
            languages: languages
                .into()
                .map(|l| l.iter().map(|s| LocaleId { id: s.to_string() }).collect()),
            fallback_family: fallback_family.into(),
        }),
        flags: Some(flags),
        cache_miss_policy: None,
    }
}

/// Makes a `TypefaceRequest` with the given style properties (and defaults for the rest).
pub fn make_style_request(
    width: impl Into<Option<Width>>,
    slant: impl Into<Option<Slant>>,
    weight: impl Into<Option<u16>>,
    exact_style: bool,
) -> TypefaceRequest {
    let request_flags =
        if exact_style { TypefaceRequestFlags::ExactStyle } else { TypefaceRequestFlags::empty() };
    make_typeface_request(width, slant, weight, None, request_flags, None)
}
