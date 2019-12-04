// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_fonts_ext::TypefaceRequestExt;
use {
    super::{
        matcher::select_best_match,
        typeface::{Typeface, TypefaceAndLangScore},
    },
    char_set::CharSet,
    failure::{format_err, Error},
    fidl_fuchsia_fonts::{Style2, TypefaceQuery, TypefaceRequest},
    std::sync::Arc,
};

fn unwrap_query<'a>(query: &'a Option<TypefaceQuery>) -> Result<&'a TypefaceQuery, Error> {
    query.as_ref().ok_or(format_err!("Missing query"))
}

#[derive(Debug, Eq, PartialEq, Hash)]
pub struct Collection {
    /// Some typefaces may be in more than one collection. In particular, fallback typefaces are
    /// added to the family collection and also to the fallback collection.
    pub faces: Vec<Arc<Typeface>>,
}

impl Collection {
    pub fn new() -> Collection {
        Collection { faces: vec![] }
    }

    pub fn match_request<'a, 'b>(
        &'a self,
        request: &'b TypefaceRequest,
    ) -> Result<Option<&'a Typeface>, Error> {
        let query = unwrap_query(&request.query)?;

        if request.exact_style() && is_style_defined(query) {
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

        fn is_style_defined(query: &TypefaceQuery) -> bool {
            if let Some(style) = &query.style {
                style.width.is_some() && style.weight.is_some() && style.slant.is_some()
            } else {
                false
            }
        }

        /// Returns true if the `typeface` has the same style values as the query. `query.style`
        /// must be present.
        fn does_style_match(query: &TypefaceQuery, typeface: &Typeface) -> bool {
            query.style
                == Some(Style2 {
                    width: Some(typeface.width),
                    weight: Some(typeface.weight),
                    slant: Some(typeface.slant),
                })
        }

        /// Returns true if there are no code points in the query, or if all of the code points are
        /// present in the `char_set`.
        fn do_code_points_match(char_set: &CharSet, query_code_points: &Option<Vec<u32>>) -> bool {
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
            request: &'b TypefaceRequest,
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

    pub fn get_styles<'a>(&'a self) -> impl Iterator<Item = Style2> + 'a {
        self.faces.iter().map(|f| Style2 {
            width: Some(f.width),
            slant: Some(f.slant),
            weight: Some(f.weight),
        })
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::font_service::AssetId,
        fidl_fuchsia_fonts::{
            GenericFontFamily, Slant, TypefaceRequestFlags, Width, WEIGHT_BOLD, WEIGHT_EXTRA_BOLD,
            WEIGHT_EXTRA_LIGHT, WEIGHT_LIGHT, WEIGHT_MEDIUM, WEIGHT_NORMAL, WEIGHT_SEMI_BOLD,
            WEIGHT_THIN,
        },
        fidl_fuchsia_intl::LocaleId,
        manifest::{self, v2},
    };

    fn make_fake_typeface_collection(mut faces: Vec<Typeface>) -> Collection {
        let mut result = Collection::new();
        for (i, mut typeface) in faces.drain(..).enumerate() {
            // Assign fake asset_id to each font
            typeface.asset_id = AssetId(i as u32);
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
            generic_family,
        )
        .unwrap() // Safe because char_set is not empty
    }

    fn request_typeface<'a, 'b>(
        collection: &'a Collection,
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
            cache_miss_policy: None,
        };

        collection.match_request(&request)
    }

    fn make_fake_font_style(width: Width, slant: Slant, weight: u16) -> Typeface {
        make_fake_typeface(width, slant, weight, &[], &[], None)
    }

    fn request_style(
        collection: &Collection,
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
            make_fake_font_style(Width::ExtraCondensed, Slant::Upright, WEIGHT_SEMI_BOLD),
            make_fake_font_style(Width::Condensed, Slant::Italic, WEIGHT_THIN),
            make_fake_font_style(Width::ExtraExpanded, Slant::Oblique, WEIGHT_NORMAL),
        ]);

        // width is more important than other style parameters.
        assert_eq!(
            request_style(&collection, Width::Condensed, Slant::Italic, WEIGHT_NORMAL)?.width,
            Width::Condensed
        );

        // For width <= Normal (5) lower widths are preferred.
        assert_eq!(
            request_style(&collection, Width::ExtraCondensed, Slant::Italic, WEIGHT_NORMAL)?.width,
            Width::ExtraCondensed
        );
        assert_eq!(
            request_style(&collection, Width::SemiCondensed, Slant::Italic, WEIGHT_NORMAL)?.width,
            Width::Condensed
        );

        // For width > SemiCondensed (4) higher widths are preferred.
        assert_eq!(
            request_style(&collection, Width::SemiExpanded, Slant::Italic, WEIGHT_NORMAL)?.width,
            Width::ExtraExpanded
        );

        // Otherwise expect font with the closest width.
        assert_eq!(
            request_style(&collection, Width::UltraCondensed, Slant::Italic, WEIGHT_NORMAL)?.width,
            Width::ExtraCondensed
        );
        assert_eq!(
            request_style(&collection, Width::UltraExpanded, Slant::Italic, WEIGHT_NORMAL)?.width,
            Width::ExtraExpanded
        );

        Ok(())
    }

    #[test]
    fn test_font_matching_slant() -> Result<(), Error> {
        let collection = make_fake_typeface_collection(vec![
            make_fake_font_style(Width::Normal, Slant::Upright, WEIGHT_SEMI_BOLD),
            make_fake_font_style(Width::Normal, Slant::Italic, WEIGHT_THIN),
            make_fake_font_style(Width::Normal, Slant::Oblique, WEIGHT_NORMAL),
        ]);

        // slant is more important than weight.
        assert_eq!(
            request_style(&collection, Width::Condensed, Slant::Upright, WEIGHT_NORMAL)?.slant,
            Slant::Upright
        );
        assert_eq!(
            request_style(&collection, Width::Condensed, Slant::Italic, WEIGHT_NORMAL)?.slant,
            Slant::Italic
        );
        assert_eq!(
            request_style(&collection, Width::Condensed, Slant::Oblique, WEIGHT_NORMAL)?.slant,
            Slant::Oblique
        );

        let collection = make_fake_typeface_collection(vec![
            make_fake_font_style(Width::Normal, Slant::Upright, WEIGHT_SEMI_BOLD),
            make_fake_font_style(Width::Normal, Slant::Oblique, WEIGHT_NORMAL),
        ]);

        // Oblique is selected when Italic is requested.
        assert_eq!(
            request_style(&collection, Width::Condensed, Slant::Italic, WEIGHT_NORMAL)?.slant,
            Slant::Oblique
        );

        Ok(())
    }

    #[test]
    fn test_font_matching_weight() -> Result<(), Error> {
        let collection = make_fake_typeface_collection(vec![
            make_fake_font_style(Width::Normal, Slant::Upright, WEIGHT_BOLD),
            make_fake_font_style(Width::Normal, Slant::Upright, WEIGHT_EXTRA_LIGHT),
            make_fake_font_style(Width::Normal, Slant::Upright, WEIGHT_NORMAL),
        ]);

        // Exact match.
        assert_eq!(
            request_style(&collection, Width::Condensed, Slant::Upright, WEIGHT_EXTRA_LIGHT)?
                .weight,
            WEIGHT_EXTRA_LIGHT
        );
        assert_eq!(
            request_style(&collection, Width::Condensed, Slant::Upright, WEIGHT_NORMAL)?.weight,
            WEIGHT_NORMAL
        );
        assert_eq!(
            request_style(&collection, Width::Condensed, Slant::Upright, WEIGHT_BOLD)?.weight,
            WEIGHT_BOLD
        );

        // For weight < WEIGHT_NORMAL lower weights are preferred.
        assert_eq!(
            request_style(&collection, Width::Condensed, Slant::Upright, WEIGHT_LIGHT)?.weight,
            WEIGHT_EXTRA_LIGHT
        );

        // For weight > WEIGHT_MEDIUM higher weights are preferred.
        assert_eq!(
            request_style(&collection, Width::Condensed, Slant::Upright, WEIGHT_SEMI_BOLD)?.weight,
            WEIGHT_BOLD
        );

        // For request.weight = WEIGHT_MEDIUM the font with weight == WEIGHT_NORMAL is preferred.
        assert_eq!(
            request_style(&collection, Width::Condensed, Slant::Upright, WEIGHT_MEDIUM)?.weight,
            WEIGHT_NORMAL
        );

        // Otherwise expect font with the closest weight.
        assert_eq!(
            request_style(&collection, Width::Condensed, Slant::Upright, WEIGHT_THIN)?.weight,
            WEIGHT_EXTRA_LIGHT
        );
        assert_eq!(
            request_style(&collection, Width::Condensed, Slant::Upright, WEIGHT_EXTRA_BOLD)?.weight,
            WEIGHT_BOLD
        );

        Ok(())
    }

    fn request_style_exact(
        collection: &Collection,
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
            make_fake_font_style(Width::ExtraCondensed, Slant::Upright, WEIGHT_SEMI_BOLD),
            make_fake_font_style(Width::Condensed, Slant::Italic, WEIGHT_THIN),
            make_fake_font_style(Width::ExtraExpanded, Slant::Oblique, WEIGHT_NORMAL),
        ]);

        assert_eq!(
            request_style_exact(&collection, Width::Condensed, Slant::Italic, WEIGHT_THIN)?
                .ok_or_else(|| format_err!("Exact style not found"))?
                .asset_id,
            AssetId(1)
        );

        assert!(request_style_exact(
            &collection,
            Width::SemiCondensed,
            Slant::Italic,
            WEIGHT_THIN
        )?
        .is_none());
        assert!(request_style_exact(&collection, Width::Condensed, Slant::Upright, WEIGHT_THIN)?
            .is_none());
        assert!(request_style_exact(&collection, Width::Condensed, Slant::Italic, WEIGHT_LIGHT)?
            .is_none());

        Ok(())
    }

    fn make_fake_typeface_with_languages(languages: &[&str]) -> Typeface {
        make_fake_typeface(Width::Normal, Slant::Upright, WEIGHT_NORMAL, languages, &[], None)
    }

    fn request_lang<'a, 'b>(
        collection: &'a Collection,
        lang: &'b [&'b str],
    ) -> Result<&'a Typeface, Error> {
        request_typeface(
            collection,
            Width::Normal,
            Slant::Upright,
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
        assert_eq!(request_lang(&collection, &["a"])?.asset_id, AssetId(0));
        assert_eq!(request_lang(&collection, &["b-C"])?.asset_id, AssetId(1));
        assert_eq!(request_lang(&collection, &["b-E"])?.asset_id, AssetId(2));

        // Verify that request language order is respected.
        assert_eq!(request_lang(&collection, &["b-C", "a"])?.asset_id, AssetId(1));

        // Partial match: the first matching font is returned first.
        assert_eq!(request_lang(&collection, &["b"])?.asset_id, AssetId(1));

        // Exact match overrides preceding partial match.
        assert_eq!(request_lang(&collection, &["b", "a"])?.asset_id, AssetId(0));

        // Partial match should match a whole BCP47 segment.
        assert_eq!(request_lang(&collection, &["foo"])?.asset_id, AssetId(4));

        Ok(())
    }

    fn make_fake_typeface_with_fallback_family(fallback_family: GenericFontFamily) -> Typeface {
        make_fake_typeface(
            Width::Normal,
            Slant::Upright,
            WEIGHT_NORMAL,
            &[],
            &[],
            Some(fallback_family),
        )
    }

    fn request_fallback_family(
        collection: &Collection,
        fallback_family: GenericFontFamily,
    ) -> Result<&Typeface, Error> {
        request_typeface(
            collection,
            Width::Normal,
            Slant::Upright,
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

        assert_eq!(
            request_fallback_family(&collection, GenericFontFamily::Serif)?.asset_id,
            AssetId(0)
        );
        assert_eq!(
            request_fallback_family(&collection, GenericFontFamily::SansSerif)?.asset_id,
            AssetId(1)
        );
        assert_eq!(
            request_fallback_family(&collection, GenericFontFamily::Monospace)?.asset_id,
            AssetId(2)
        );

        // First font is returned when there is no exact match.
        assert_eq!(
            request_fallback_family(&collection, GenericFontFamily::Cursive)?.asset_id,
            AssetId(0)
        );

        Ok(())
    }
}
