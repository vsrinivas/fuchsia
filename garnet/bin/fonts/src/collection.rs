// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{font_info, manifest},
    failure::{format_err, Error},
    fidl_fuchsia_fonts as fonts,
    fidl_fuchsia_intl::LocaleId,
    font_info::CharSet,
    std::{collections::BTreeSet, sync::Arc},
};

pub type LanguageSet = BTreeSet<String>;

#[derive(Debug)]
pub struct Typeface {
    pub asset_id: u32,
    pub font_index: u32,
    pub slant: fonts::Slant,
    pub weight: u16,
    pub width: fonts::Width,
    pub languages: LanguageSet,
    pub char_set: CharSet,
    pub generic_family: Option<fonts::GenericFontFamily>,
}

impl Typeface {
    pub fn new(
        asset_id: u32,
        manifest_font: manifest::Font,
        char_set: CharSet,
        generic_family: Option<fonts::GenericFontFamily>,
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
}

#[derive(Debug)]
struct TypefaceAndLangScore<'a> {
    typeface: &'a Typeface,
    lang_score: usize,
}

impl<'a> TypefaceAndLangScore<'a> {
    fn new(typeface: &'a Typeface, request: &fonts::TypefaceRequest) -> TypefaceAndLangScore<'a> {
        let request_languages: Vec<LocaleId> =
            match request.query.as_ref().and_then(|query| query.languages.as_ref()) {
                Some(languages) => languages.iter().map(LocaleId::clone).collect(),
                _ => vec![],
            };
        let lang_score = get_lang_match_score(typeface, &request_languages);
        TypefaceAndLangScore { typeface, lang_score }
    }
}

/// Returns value in the range `[0, 2 * request_lang.len()]`. The language code is used for exact
/// matches; the rest is for partial matches.
///
/// TODO(kpozin): Use a standard locale matching algorithm.
fn get_lang_match_score(typeface: &Typeface, request_languages: &[LocaleId]) -> usize {
    let mut best_partial_match_pos = None;
    for i in 0..request_languages.len() {
        let lang = &request_languages[i].id;

        // Iterate over all languages in the typeface that start with `lang`.
        for typeface_lang in
            typeface.languages.range::<String, std::ops::RangeFrom<&String>>(lang..)
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

/// Selects between typefaces `a` and `b` for the `request`. Typefaces are passed in
/// `TypefaceAndLangScore` so the language match score is calculated only once for each typeface. If
/// `a` and `b` are equivalent then `a` is returned.
///
/// The style matching logic follows the CSS3 Fonts spec (see
/// [Section 5.2, Item 4](https://www.w3.org/TR/css-fonts-3/#font-style-matching) with two
/// additions:
///
///   1. Typefaces with a higher language match score are preferred. The score value is expected to
///      be pre-calculated by `get_lang_match_score()`. Note that if the request specifies a code
///      point then the typefaces are expected to be already filtered based on that code point, i.e.
///      they both contain that character, so this function doesn't need to verify it.
///   2. If the request specifies a `fallback_family`, then fonts with the same `fallback_family`
///      are preferred.
fn select_best_match<'a, 'b>(
    a: TypefaceAndLangScore<'a>,
    b: TypefaceAndLangScore<'a>,
    query: &'b fonts::TypefaceQuery,
) -> TypefaceAndLangScore<'a> {
    if a.lang_score != b.lang_score {
        if a.lang_score < b.lang_score {
            return a;
        } else {
            return b;
        }
    }

    if let Some(fallback_family) = query.fallback_family {
        if a.typeface.generic_family != b.typeface.generic_family {
            if a.typeface.generic_family == Some(fallback_family) {
                return a;
            } else if b.typeface.generic_family == Some(fallback_family) {
                return b;
            }
            // If `generic_family` of `a` and `b` doesn't match the request, then fall through to
            // compare them based on style parameters.
        }
    }

    if let Some(query_style) = &query.style {
        // Select based on width, see CSS3 Section 5.2, Item 4.a.
        if let Some(query_width) = query_style.width {
            if a.typeface.width != b.typeface.width {
                // Reorder a and b, so a has lower width.
                let (a, b) = if a.typeface.width > b.typeface.width { (b, a) } else { (a, b) };
                if query_width <= fonts::Width::Normal {
                    if b.typeface.width <= query_width {
                        return b;
                    } else {
                        return a;
                    }
                } else {
                    if a.typeface.width >= query_width {
                        return a;
                    } else {
                        return b;
                    }
                }
            }
        }

        // Select based on slant, CSS3 Section 5.2, Item 4.b.
        if let Some(query_slant) = query_style.slant {
            match (query_slant, a.typeface.slant, b.typeface.slant) {
                // If both fonts have the same slant then fall through to select based
                // on weight.
                (_, a_s, b_s) if a_s == b_s => (),

                // If we have a font that matches the request then use it.
                (r_s, a_s, _) if r_s == a_s => return a,
                (r_s, _, b_s) if r_s == b_s => return b,

                // In case italic or oblique font is requested pick italic or
                // oblique.
                (fonts::Slant::Italic, fonts::Slant::Oblique, _) => return a,
                (fonts::Slant::Italic, _, fonts::Slant::Oblique) => return b,

                (fonts::Slant::Oblique, fonts::Slant::Italic, _) => return a,
                (fonts::Slant::Oblique, _, fonts::Slant::Italic) => return b,

                // In case upright font is requested, but we have only italic and
                // oblique then fall through to select based on weight.
                (fonts::Slant::Upright, _, _) => (),

                // Patterns above cover all possible inputs, but exhaustiveness
                // checker doesn't see it.
                _ => (),
            }
        }

        // Select based on weight, CSS3 Section 5.2, Item 4.c.
        if let Some(query_weight) = query_style.weight {
            if a.typeface.weight != b.typeface.weight {
                // Reorder a and b, so a has lower weight.
                let ordered = if a.typeface.weight > b.typeface.weight { (b, a) } else { (a, b) };
                let (a, b) = ordered;

                if a.typeface.weight == query_weight {
                    return a;
                }
                if b.typeface.weight == query_weight {
                    return b;
                }

                if query_weight < fonts::WEIGHT_NORMAL {
                    // If query_weight < 400, then typefaces with weights <= query_weight are
                    // preferred.
                    if b.typeface.weight <= query_weight {
                        return b;
                    } else {
                        return a;
                    }
                } else if query_weight > fonts::WEIGHT_MEDIUM {
                    // If request.weight > 500, then typefaces with weights >= query_weight are
                    // preferred.
                    if a.typeface.weight >= query_weight {
                        return a;
                    } else {
                        return b;
                    }
                } else {
                    // request.weight is 400 or 500.
                    if b.typeface.weight <= fonts::WEIGHT_MEDIUM {
                        if (a.typeface.weight as i32 - query_weight as i32).abs()
                            < (b.typeface.weight as i32 - query_weight as i32).abs()
                        {
                            return a;
                        } else {
                            return b;
                        }
                    } else {
                        return a;
                    }
                }
            }
        }
    }

    // If a and b are equivalent then give priority according to the order in the manifest.
    a
}

#[derive(Debug)]
pub struct TypefaceCollection {
    /// Some typefaces may be in more than one collection. In particular, fallback typefaces are
    /// added to the family collection and also to the fallback collection.
    faces: Vec<Arc<Typeface>>,
}

impl TypefaceCollection {
    pub fn new() -> TypefaceCollection {
        TypefaceCollection { faces: vec![] }
    }

    pub fn match_request<'a, 'b>(
        &'a self,
        request: &'b fonts::TypefaceRequest,
    ) -> Result<Option<&'a Typeface>, Error> {
        let query = unwrap_query(&request.query)?;

        if let Some(flags) = request.flags {
            if is_style_defined(query) && flags.contains(fonts::TypefaceRequestFlags::ExactStyle) {
                return Ok(self
                    .faces
                    .iter()
                    .find(|typeface| {
                        does_style_match(&query, typeface)
                            && (query.languages.as_ref().map_or(true, |value| {
                                value.is_empty()
                                    || value
                                        .iter()
                                        .find(|lang| typeface.languages.contains(&*lang.id))
                                        .is_some()
                            }))
                            && do_code_points_match(&typeface.char_set, &query.code_points)
                    })
                    .map(|f| f as &Typeface));
            }
        }

        fn is_style_defined(query: &fonts::TypefaceQuery) -> bool {
            if let Some(style) = &query.style {
                style.width.is_some() && style.weight.is_some() && style.slant.is_some()
            } else {
                false
            }
        }

        /// Returns true if the `typeface` has the same style values as the query. `query.style`
        /// must be present.
        fn does_style_match(query: &fonts::TypefaceQuery, typeface: &Typeface) -> bool {
            query.style
                == Some(fonts::Style2 {
                    width: Some(typeface.width),
                    weight: Some(typeface.weight),
                    slant: Some(typeface.slant),
                })
        }

        /// Returns true if there are no code points in the query, or if all of the code points are
        /// present in the `char_set`.
        fn do_code_points_match(
            char_set: &font_info::CharSet,
            query_code_points: &Option<Vec<u32>>,
        ) -> bool {
            match &query_code_points {
                Some(code_points) => {
                    code_points.iter().all(|code_point| char_set.contains(*code_point))
                }
                None => true,
            }
        }

        fn fold<'a, 'b>(
            best: Option<TypefaceAndLangScore<'a>>,
            x: &'a Typeface,
            request: &'b fonts::TypefaceRequest,
        ) -> Result<Option<TypefaceAndLangScore<'a>>, Error> {
            let x = TypefaceAndLangScore::new(x, request);
            match best {
                Some(b) => Ok(Some(select_best_match(b, x, unwrap_query(&request.query)?))),
                None => Ok(Some(x)),
            }
        }

        Ok(self
            .faces
            .iter()
            .filter(|f| do_code_points_match(&f.char_set, &query.code_points))
            .try_fold(None, |best, x| fold(best, x, request))?
            .map(|a| a.typeface))
    }

    pub fn is_empty(&self) -> bool {
        self.faces.is_empty()
    }

    pub fn add_typeface(&mut self, typeface: Arc<Typeface>) {
        self.faces.push(typeface);
    }

    pub fn get_styles<'a>(&'a self) -> impl Iterator<Item = fonts::Style2> + 'a {
        self.faces.iter().map(|f| fonts::Style2 {
            width: Some(f.width),
            slant: Some(f.slant),
            weight: Some(f.weight),
        })
    }
}

fn unwrap_query<'a>(
    query: &'a Option<fonts::TypefaceQuery>,
) -> Result<&'a fonts::TypefaceQuery, Error> {
    query.as_ref().ok_or(format_err!("Missing query"))
}

#[cfg(test)]
mod tests {
    use {
        super::{font_info::CharSet, *},
        fidl_fuchsia_fonts::{
            GenericFontFamily,
            Slant::{self, *},
            Style2, TypefaceQuery, TypefaceRequest, TypefaceRequestFlags, Width, WEIGHT_BOLD,
            WEIGHT_EXTRA_BOLD, WEIGHT_EXTRA_LIGHT, WEIGHT_LIGHT, WEIGHT_MEDIUM, WEIGHT_NORMAL,
            WEIGHT_SEMI_BOLD, WEIGHT_THIN,
        },
        fidl_fuchsia_intl::LocaleId,
    };

    fn make_fake_typeface_collection(mut faces: Vec<Typeface>) -> TypefaceCollection {
        let mut result = TypefaceCollection::new();
        for (i, mut typeface) in faces.drain(..).enumerate() {
            // Assign fake asset_id to each font
            typeface.asset_id = i as u32;
            result.add_typeface(Arc::new(typeface));
        }

        result
    }

    fn make_fake_typeface(
        width: Width,
        slant: Slant,
        weight: u16,
        languages: &[&str],
        char_set: &[u32],
        generic_family: Option<GenericFontFamily>,
    ) -> Typeface {
        Typeface::new(
            0,
            manifest::Font {
                asset: std::path::PathBuf::new(),
                index: 0,
                slant,
                weight,
                width,
                languages: languages.iter().map(|s| s.to_string()).collect(),
            },
            CharSet::new(char_set.to_vec()),
            generic_family,
        )
    }

    fn request_typeface<'a, 'b>(
        collection: &'a TypefaceCollection,
        width: Width,
        slant: Slant,
        weight: u16,
        languages: Option<&'b [&'b str]>,
        flags: TypefaceRequestFlags,
        fallback_family: Option<GenericFontFamily>,
    ) -> Result<Option<&'a Typeface>, Error> {
        let request = TypefaceRequest {
            query: Some(TypefaceQuery {
                family: None,
                style: Some(Style2 {
                    weight: Some(weight),
                    width: Some(width),
                    slant: Some(slant),
                }),
                code_points: None,
                languages: languages
                    .map(|l| l.iter().map(|s| LocaleId { id: s.to_string() }).collect()),
                fallback_family,
            }),
            flags: Some(flags),
        };

        collection.match_request(&request)
    }

    fn make_fake_font_style(width: Width, slant: Slant, weight: u16) -> Typeface {
        make_fake_typeface(width, slant, weight, &[], &[], None)
    }

    fn request_style(
        collection: &TypefaceCollection,
        width: Width,
        slant: Slant,
        weight: u16,
    ) -> Result<&Typeface, Error> {
        request_typeface(
            collection,
            width,
            slant,
            weight,
            None,
            TypefaceRequestFlags::empty(),
            None,
        )?
        .ok_or(format_err!("No typeface found for style"))
    }

    #[test]
    fn test_font_matching_width() -> Result<(), Error> {
        let collection = make_fake_typeface_collection(vec![
            make_fake_font_style(Width::ExtraCondensed, Upright, WEIGHT_SEMI_BOLD),
            make_fake_font_style(Width::Condensed, Italic, WEIGHT_THIN),
            make_fake_font_style(Width::ExtraExpanded, Oblique, WEIGHT_NORMAL),
        ]);

        // width is more important than other style parameters.
        assert_eq!(
            request_style(&collection, Width::Condensed, Italic, WEIGHT_NORMAL)?.width,
            Width::Condensed
        );

        // For width <= Normal (5) lower widths are preferred.
        assert_eq!(
            request_style(&collection, Width::ExtraCondensed, Italic, WEIGHT_NORMAL)?.width,
            Width::ExtraCondensed
        );
        assert_eq!(
            request_style(&collection, Width::SemiCondensed, Italic, WEIGHT_NORMAL)?.width,
            Width::Condensed
        );

        // For width > SemiCondensed (4) higher widths are preferred.
        assert_eq!(
            request_style(&collection, Width::SemiExpanded, Italic, WEIGHT_NORMAL)?.width,
            Width::ExtraExpanded
        );

        // Otherwise expect font with the closest width.
        assert_eq!(
            request_style(&collection, Width::UltraCondensed, Italic, WEIGHT_NORMAL)?.width,
            Width::ExtraCondensed
        );
        assert_eq!(
            request_style(&collection, Width::UltraExpanded, Italic, WEIGHT_NORMAL)?.width,
            Width::ExtraExpanded
        );

        Ok(())
    }

    #[test]
    fn test_font_matching_slant() -> Result<(), Error> {
        let collection = make_fake_typeface_collection(vec![
            make_fake_font_style(Width::Normal, Upright, WEIGHT_SEMI_BOLD),
            make_fake_font_style(Width::Normal, Italic, WEIGHT_THIN),
            make_fake_font_style(Width::Normal, Oblique, WEIGHT_NORMAL),
        ]);

        // slant is more important than weight.
        assert_eq!(
            request_style(&collection, Width::Condensed, Upright, WEIGHT_NORMAL)?.slant,
            Upright
        );
        assert_eq!(
            request_style(&collection, Width::Condensed, Italic, WEIGHT_NORMAL)?.slant,
            Italic
        );
        assert_eq!(
            request_style(&collection, Width::Condensed, Oblique, WEIGHT_NORMAL)?.slant,
            Oblique
        );

        let collection = make_fake_typeface_collection(vec![
            make_fake_font_style(Width::Normal, Upright, WEIGHT_SEMI_BOLD),
            make_fake_font_style(Width::Normal, Oblique, WEIGHT_NORMAL),
        ]);

        // Oblique is selected when Italic is requested.
        assert_eq!(
            request_style(&collection, Width::Condensed, Italic, WEIGHT_NORMAL)?.slant,
            Oblique
        );

        Ok(())
    }

    #[test]
    fn test_font_matching_weight() -> Result<(), Error> {
        let collection = make_fake_typeface_collection(vec![
            make_fake_font_style(Width::Normal, Upright, WEIGHT_BOLD),
            make_fake_font_style(Width::Normal, Upright, WEIGHT_EXTRA_LIGHT),
            make_fake_font_style(Width::Normal, Upright, WEIGHT_NORMAL),
        ]);

        // Exact match.
        assert_eq!(
            request_style(&collection, Width::Condensed, Upright, WEIGHT_EXTRA_LIGHT)?.weight,
            WEIGHT_EXTRA_LIGHT
        );
        assert_eq!(
            request_style(&collection, Width::Condensed, Upright, WEIGHT_NORMAL)?.weight,
            WEIGHT_NORMAL
        );
        assert_eq!(
            request_style(&collection, Width::Condensed, Upright, WEIGHT_BOLD)?.weight,
            WEIGHT_BOLD
        );

        // For weight < WEIGHT_NORMAL lower weights are preferred.
        assert_eq!(
            request_style(&collection, Width::Condensed, Upright, WEIGHT_LIGHT)?.weight,
            WEIGHT_EXTRA_LIGHT
        );

        // For weight > WEIGHT_MEDIUM higher weights are preferred.
        assert_eq!(
            request_style(&collection, Width::Condensed, Upright, WEIGHT_SEMI_BOLD)?.weight,
            WEIGHT_BOLD
        );

        // For request.weight = WEIGHT_MEDIUM the font with weight == WEIGHT_NORMAL is preferred.
        assert_eq!(
            request_style(&collection, Width::Condensed, Upright, WEIGHT_MEDIUM)?.weight,
            WEIGHT_NORMAL
        );

        // Otherwise expect font with the closest weight.
        assert_eq!(
            request_style(&collection, Width::Condensed, Upright, WEIGHT_THIN)?.weight,
            WEIGHT_EXTRA_LIGHT
        );
        assert_eq!(
            request_style(&collection, Width::Condensed, Upright, WEIGHT_EXTRA_BOLD)?.weight,
            WEIGHT_BOLD
        );

        Ok(())
    }

    fn request_style_exact(
        collection: &TypefaceCollection,
        width: Width,
        slant: Slant,
        weight: u16,
    ) -> Result<Option<&Typeface>, Error> {
        request_typeface(
            collection,
            width,
            slant,
            weight,
            None,
            TypefaceRequestFlags::ExactStyle,
            None,
        )
    }

    #[test]
    fn test_font_matching_exact() -> Result<(), Error> {
        let collection = make_fake_typeface_collection(vec![
            make_fake_font_style(Width::ExtraCondensed, Upright, WEIGHT_SEMI_BOLD),
            make_fake_font_style(Width::Condensed, Italic, WEIGHT_THIN),
            make_fake_font_style(Width::ExtraExpanded, Oblique, WEIGHT_NORMAL),
        ]);

        assert_eq!(
            request_style_exact(&collection, Width::Condensed, Italic, WEIGHT_THIN)?
                .ok_or_else(|| format_err!("Exact style not found"))?
                .asset_id,
            1
        );

        assert!(
            request_style_exact(&collection, Width::SemiCondensed, Italic, WEIGHT_THIN)?.is_none()
        );
        assert!(request_style_exact(&collection, Width::Condensed, Upright, WEIGHT_THIN)?.is_none());
        assert!(request_style_exact(&collection, Width::Condensed, Italic, WEIGHT_LIGHT)?.is_none());

        Ok(())
    }

    fn make_fake_typeface_with_languages(languages: &[&str]) -> Typeface {
        make_fake_typeface(Width::Normal, Upright, WEIGHT_NORMAL, languages, &[], None)
    }

    fn request_lang<'a, 'b>(
        collection: &'a TypefaceCollection,
        lang: &'b [&'b str],
    ) -> Result<&'a Typeface, Error> {
        request_typeface(
            collection,
            Width::Normal,
            Upright,
            WEIGHT_NORMAL,
            Some(lang),
            TypefaceRequestFlags::empty(),
            None,
        )?
        .ok_or_else(|| format_err!("No typeface found for lang"))
    }

    #[test]
    fn test_font_matching_lang() -> Result<(), Error> {
        let collection = make_fake_typeface_collection(vec![
            make_fake_typeface_with_languages(&["a"]),
            make_fake_typeface_with_languages(&["b-C"]),
            make_fake_typeface_with_languages(&["b-D", "b-E"]),
            make_fake_typeface_with_languages(&["fooo"]),
            make_fake_typeface_with_languages(&["foo-BAR"]),
        ]);

        // Exact matches.
        assert_eq!(request_lang(&collection, &["a"])?.asset_id, 0);
        assert_eq!(request_lang(&collection, &["b-C"])?.asset_id, 1);
        assert_eq!(request_lang(&collection, &["b-E"])?.asset_id, 2);

        // Verify that request language order is respected.
        assert_eq!(request_lang(&collection, &["b-C", "a"])?.asset_id, 1);

        // Partial match: the first matching font is returned first.
        assert_eq!(request_lang(&collection, &["b"])?.asset_id, 1);

        // Exact match overrides preceding partial match.
        assert_eq!(request_lang(&collection, &["b", "a"])?.asset_id, 0);

        // Partial match should match a whole BCP47 segment.
        assert_eq!(request_lang(&collection, &["foo"])?.asset_id, 4);

        Ok(())
    }

    fn make_fake_typeface_with_fallback_family(fallback_family: GenericFontFamily) -> Typeface {
        make_fake_typeface(Width::Normal, Upright, WEIGHT_NORMAL, &[], &[], Some(fallback_family))
    }

    fn request_fallback_family(
        collection: &TypefaceCollection,
        fallback_family: GenericFontFamily,
    ) -> Result<&Typeface, Error> {
        request_typeface(
            collection,
            Width::Normal,
            Upright,
            WEIGHT_NORMAL,
            None,
            TypefaceRequestFlags::empty(),
            Some(fallback_family),
        )?
        .ok_or_else(|| format_err!("No typeface found for fallback family"))
    }

    #[test]
    fn test_font_matching_fallback_group() -> Result<(), Error> {
        let collection = make_fake_typeface_collection(vec![
            make_fake_typeface_with_fallback_family(GenericFontFamily::Serif),
            make_fake_typeface_with_fallback_family(GenericFontFamily::SansSerif),
            make_fake_typeface_with_fallback_family(GenericFontFamily::Monospace),
        ]);

        assert_eq!(request_fallback_family(&collection, GenericFontFamily::Serif)?.asset_id, 0);
        assert_eq!(request_fallback_family(&collection, GenericFontFamily::SansSerif)?.asset_id, 1);
        assert_eq!(request_fallback_family(&collection, GenericFontFamily::Monospace)?.asset_id, 2);

        // First font is returned when there is no exact match.
        assert_eq!(request_fallback_family(&collection, GenericFontFamily::Cursive)?.asset_id, 0);

        Ok(())
    }
}
