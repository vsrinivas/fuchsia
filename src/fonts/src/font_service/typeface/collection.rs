// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{
        matcher::select_best_match,
        typeface::{Typeface, TypefaceAndLangScore, TypefaceId},
    },
    anyhow::{format_err, Error},
    char_set::CharSet,
    fidl_fuchsia_fonts::{Style2, TypefaceQuery, TypefaceRequest},
    fidl_fuchsia_fonts_ext::TypefaceRequestExt,
    std::{collections::HashSet, sync::Arc},
};

fn unwrap_query<'a>(query: &'a Option<TypefaceQuery>) -> Result<&'a TypefaceQuery, Error> {
    query.as_ref().ok_or(format_err!("Missing query"))
}

/// Ordered collection of `Typeface`s. Construct using [`TypefaceCollectionBuilder`].
#[derive(Debug, Eq, PartialEq, Hash)]
pub struct Collection {
    /// Some typefaces may be in more than one collection. In particular, fallback typefaces are
    /// added to the family collection and also to the fallback collection.
    pub faces: Vec<Arc<Typeface>>,
}

impl Collection {
    pub fn match_request<'a, 'b>(
        &'a self,
        request: &'b TypefaceRequest,
    ) -> Result<Option<&'a Typeface>, Error> {
        /// Returns true if request does not require an exact style match, _or_ if the query's style
        /// values match those that are in the `typeface`.
        ///
        /// Any style properties that are omitted from the query are ignored.
        fn does_exact_style_match(request: &TypefaceRequest, typeface: &Typeface) -> bool {
            if !request.exact_style() {
                return true;
            }
            let query = request.query.as_ref().unwrap();
            match &query.style {
                None => true,
                Some(style) => {
                    (style.width.is_none() || style.width.unwrap() == typeface.width)
                        && (style.weight.is_none() || style.weight.unwrap() == typeface.weight)
                        && (style.slant.is_none() || style.slant.unwrap() == typeface.slant)
                }
            }
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

        let query = unwrap_query(&request.query)?;

        Ok(self
            .faces
            .iter()
            .filter(|f| does_exact_style_match(&request, f))
            .filter(|f| do_code_points_match(&f.char_set, &query.code_points))
            .try_fold(None, |best, x| fold(best, x, request))?
            .map(|a| a.typeface))
    }

    pub fn get_styles<'a>(&'a self) -> impl Iterator<Item = Style2> + 'a {
        self.faces.iter().map(|f| Style2 {
            width: Some(f.width),
            slant: Some(f.slant),
            weight: Some(f.weight),
        })
    }
}

/// Builder for [typeface `Collection`](crate::font_service::typeface::Collection).
#[derive(Debug)]
pub struct TypefaceCollectionBuilder {
    faces: Vec<Arc<Typeface>>,
    /// Bookkeeping for deduplication
    seen_ids: HashSet<TypefaceId>,
}

impl TypefaceCollectionBuilder {
    /// Creates a new collection builder with no typefaces.
    pub fn new() -> Self {
        TypefaceCollectionBuilder { faces: vec![], seen_ids: HashSet::new() }
    }

    /// Returns true if the collection already has a typeface with the given ID.
    pub fn has_typeface_id(&self, typeface_id: &TypefaceId) -> bool {
        self.seen_ids.contains(typeface_id)
    }

    /// If there's no existing typeface with the same typeface ID already in the builder, adds the
    /// given typeface and and returns `true`. Otherwise, returns `false`.
    pub fn add_typeface_once(&mut self, typeface: Arc<Typeface>) -> bool {
        let typeface_id = TypefaceId { asset_id: typeface.asset_id, index: typeface.font_index };
        if self.seen_ids.contains(&typeface_id) {
            false
        } else {
            self.seen_ids.insert(typeface_id);
            self.faces.push(typeface);
            true
        }
    }

    /// Returns true if there are no typefaces currently in the collection.
    pub fn is_empty(&self) -> bool {
        self.faces.is_empty()
    }

    /// Creates a `Collection` from the builder's current state. Call this method last, once you are
    /// satisfied with how the collection is configured.
    pub fn build(self) -> Collection {
        Collection { faces: self.faces }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::{super::test_util::*, *},
        crate::font_service::AssetId,
        fidl_fuchsia_fonts::{
            GenericFontFamily, Slant, TypefaceRequestFlags, Width, WEIGHT_LIGHT, WEIGHT_NORMAL,
            WEIGHT_SEMI_BOLD, WEIGHT_THIN,
        },
        manifest::v2,
    };

    fn make_fake_typeface_collection(mut faces: Vec<Typeface>) -> Collection {
        let mut builder = TypefaceCollectionBuilder::new();
        for (i, mut typeface) in faces.drain(..).enumerate() {
            // Assign fake asset_id to each font
            typeface.asset_id = AssetId(i as u32);
            builder.add_typeface_once(Arc::new(typeface));
        }

        builder.build()
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
        width: impl Into<Option<Width>>,
        slant: impl Into<Option<Slant>>,
        weight: impl Into<Option<u16>>,
        languages: impl Into<Option<&'b [&'b str]>>,
        flags: TypefaceRequestFlags,
        fallback_family: impl Into<Option<GenericFontFamily>>,
    ) -> Result<Option<&'a Typeface>, Error> {
        let request =
            make_typeface_request(width, slant, weight, languages, flags, fallback_family);
        collection.match_request(&request)
    }

    fn request_style_exact(
        collection: &Collection,
        width: impl Into<Option<Width>>,
        slant: impl Into<Option<Slant>>,
        weight: impl Into<Option<u16>>,
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
            make_fake_typeface_style(Width::ExtraCondensed, Slant::Upright, WEIGHT_SEMI_BOLD),
            make_fake_typeface_style(Width::Condensed, Slant::Italic, WEIGHT_THIN),
            make_fake_typeface_style(Width::ExtraExpanded, Slant::Oblique, WEIGHT_NORMAL),
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
            WEIGHT_THIN,
        )?
        .is_none());
        assert!(request_style_exact(&collection, Width::Condensed, Slant::Upright, WEIGHT_THIN)?
            .is_none());
        assert!(request_style_exact(&collection, Width::Condensed, Slant::Italic, WEIGHT_LIGHT)?
            .is_none());

        Ok(())
    }

    /// Exact style matches where the query includes only some of the style properties.
    #[test]
    fn test_font_matching_exact_partial() -> Result<(), Error> {
        fn exact_style_not_found() -> Error {
            format_err!("Exact style not found")
        }

        let collection = make_fake_typeface_collection(vec![
            make_fake_typeface_style(Width::ExtraCondensed, Slant::Upright, WEIGHT_SEMI_BOLD),
            make_fake_typeface_style(Width::Condensed, Slant::Italic, WEIGHT_THIN),
            make_fake_typeface_style(Width::ExtraExpanded, Slant::Oblique, WEIGHT_SEMI_BOLD),
            make_fake_typeface_style(Width::ExtraExpanded, Slant::Oblique, WEIGHT_NORMAL),
        ]);

        assert_eq!(
            request_style_exact(&collection, None, None, WEIGHT_THIN)?
                .ok_or_else(exact_style_not_found)?
                .asset_id,
            AssetId(1)
        );

        assert_eq!(
            request_style_exact(&collection, Width::ExtraExpanded, None, None)?
                .ok_or_else(exact_style_not_found)?
                .asset_id,
            AssetId(3)
        );

        assert!(
            request_style_exact(&collection, Width::SemiCondensed, Slant::Oblique, None)?.is_none()
        );

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
