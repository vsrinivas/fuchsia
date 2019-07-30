// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::font_service::{font_info::CharSet, manifest::Font},
    fidl_fuchsia_fonts::{FamilyName, GenericFontFamily, Slant, Style2, TypefaceRequest, Width},
    fidl_fuchsia_fonts_experimental::TypefaceInfo,
    fidl_fuchsia_intl::LocaleId,
    std::collections::BTreeSet,
};

#[derive(Debug)]
pub struct Typeface {
    pub asset_id: u32,
    pub font_index: u32,
    pub slant: Slant,
    pub weight: u16,
    pub width: Width,
    pub languages: BTreeSet<String>,
    pub char_set: CharSet,
    pub generic_family: Option<GenericFontFamily>,
}

impl Typeface {
    pub fn new(
        asset_id: u32,
        manifest_font: Font,
        char_set: CharSet,
        generic_family: Option<GenericFontFamily>,
    ) -> Typeface {
        Typeface {
            asset_id,
            font_index: manifest_font.index,
            weight: manifest_font.weight,
            width: manifest_font.width,
            slant: manifest_font.slant,
            languages: manifest_font.languages.iter().map(|x| x.to_string()).collect(),
            char_set,
            generic_family,
        }
    }

    /// Returns value in the range `[0, 2 * request_languages.len()]`. The language code is used for
    /// exact matches; the rest is for partial matches.
    ///
    /// TODO(kpozin): Use a standard locale matching algorithm.
    fn get_lang_match_score(&self, request_languages: &[LocaleId]) -> usize {
        let mut best_partial_match_pos = None;
        for i in 0..request_languages.len() {
            let lang = &request_languages[i].id;

            // Iterate over all languages in the typeface that start with `lang`.
            for typeface_lang in
                self.languages.range::<String, std::ops::RangeFrom<&String>>(lang..)
            {
                if !typeface_lang.starts_with(lang.as_str()) {
                    break;
                }

                if typeface_lang.len() == lang.len() {
                    // Exact match.
                    return i;
                }

                // Partial match is valid only when it's followed by '-' character.
                if (typeface_lang.as_bytes()[lang.len()] == '-' as u8)
                    & best_partial_match_pos.is_none()
                {
                    best_partial_match_pos = Some(i);
                    continue;
                }
            }
        }

        best_partial_match_pos.unwrap_or(request_languages.len()) + request_languages.len()
    }
}

#[derive(Debug)]
pub struct TypefaceAndLangScore<'a> {
    pub typeface: &'a Typeface,
    pub lang_score: usize,
}

impl<'a> TypefaceAndLangScore<'a> {
    pub fn new(typeface: &'a Typeface, request: &TypefaceRequest) -> TypefaceAndLangScore<'a> {
        let request_languages: Vec<LocaleId> =
            match request.query.as_ref().and_then(|query| query.languages.as_ref()) {
                Some(languages) => languages.iter().map(LocaleId::clone).collect(),
                _ => vec![],
            };
        let lang_score = typeface.get_lang_match_score(&request_languages);
        TypefaceAndLangScore { typeface, lang_score }
    }
}

pub struct TypefaceInfoAndCharSet {
    pub asset_id: u32,
    pub font_index: u32,
    pub family: FamilyName,
    pub style: Style2,
    pub languages: Vec<LocaleId>,
    pub generic_family: Option<GenericFontFamily>,
    pub char_set: CharSet,
}

impl TypefaceInfoAndCharSet {
    pub fn from_typeface(typeface: &Typeface, canonical_family: String) -> TypefaceInfoAndCharSet {
        TypefaceInfoAndCharSet {
            asset_id: typeface.asset_id,
            font_index: typeface.font_index,
            family: FamilyName { name: canonical_family },
            style: Style2 {
                slant: Some(typeface.slant),
                weight: Some(typeface.weight),
                width: Some(typeface.width),
            },
            // Convert BTreeSet<String> to Vec<LocaleId>
            languages: typeface
                .languages
                .iter()
                .map(|lang| LocaleId { id: lang.clone() })
                .collect(),
            generic_family: typeface.generic_family,
            char_set: typeface.char_set.clone(),
        }
    }
}

impl From<TypefaceInfoAndCharSet> for TypefaceInfo {
    fn from(info: TypefaceInfoAndCharSet) -> TypefaceInfo {
        TypefaceInfo {
            asset_id: Some(info.asset_id),
            font_index: Some(info.font_index),
            family: Some(info.family),
            style: Some(info.style),
            languages: Some(info.languages),
            generic_family: info.generic_family,
        }
    }
}
