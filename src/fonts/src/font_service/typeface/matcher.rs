// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::typeface::TypefaceAndLangScore,
    fidl_fuchsia_fonts::{
        self as fonts, Slant, Style2, TypefaceQuery, Width, WEIGHT_MEDIUM, WEIGHT_NORMAL,
    },
    lazy_static::lazy_static,
};

lazy_static! {
    /// The default style (or its individual properties) are is applied to fill any style properties
    /// that are missing from a query passed to the matcher.
    static ref DEFAULT_STYLE: Style2 = Style2 {
        slant: Some(fonts::DEFAULT_SLANT),
        weight: Some(fonts::DEFAULT_WEIGHT),
        width: Some(fonts::DEFAULT_WIDTH),
    };
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
///   3. The matcher supports any integer font weight in the range `[1, 1000]`, and not just
///      multiples of 100 as in CSS3. Therefore, if the request specifies a weight between 400
///      (normal) and 500 (medium), inclusive, then then the algorithm used is from
///      [CSS _4_, Section 5.2](https://www.w3.org/TR/css-fonts-4/#font-style-matching), Item 4.3,
///      first bullet.
pub fn select_best_match<'a, 'b>(
    a: TypefaceAndLangScore<'a>,
    b: TypefaceAndLangScore<'a>,
    query: &'b TypefaceQuery,
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

    let query_style = query.style.as_ref().unwrap_or(&*DEFAULT_STYLE);
    // Select based on width, see CSS3 Section 5.2, Item 4.a.
    let query_width = query_style.width.unwrap_or(fonts::DEFAULT_WIDTH);
    if a.typeface.width != b.typeface.width {
        // Reorder a and b, so a has lower width.
        let (a, b) = if a.typeface.width > b.typeface.width { (b, a) } else { (a, b) };
        if query_width <= Width::Normal {
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

    // Select based on slant, CSS3 Section 5.2, Item 4.b.
    let query_slant = query_style.slant.unwrap_or(fonts::DEFAULT_SLANT);
    match (query_slant, a.typeface.slant, b.typeface.slant) {
        // If both fonts have the same slant then fall through to select based
        // on weight.
        (_, a_s, b_s) if a_s == b_s => (),

        // If we have a font that exactly matches the request's slant, then use it.
        (r_s, a_s, _) if r_s == a_s => return a,
        (r_s, _, b_s) if r_s == b_s => return b,

        // If an italic or oblique font is requested, pick whichever of italic or oblique is
        // available.
        (Slant::Italic, Slant::Oblique, _) => return a,
        (Slant::Italic, _, Slant::Oblique) => return b,

        (Slant::Oblique, Slant::Italic, _) => return a,
        (Slant::Oblique, _, Slant::Italic) => return b,

        // If an upright font is requested, but we have only italic and oblique, then fall through
        // to select based on weight.
        //
        // Technically, we could strictly follow "normal faces are checked first, then oblique
        // faces, then italic faces" in this case, but "User agents are permitted to distinguish
        // between italic and oblique faces within platform font families but this is not required."
        // A better matching weight seems more important.
        (Slant::Upright, _, _) => (),

        // Patterns above cover all possible inputs, but exhaustiveness
        // checker doesn't see it.
        _ => (),
    }

    // Select based on weight, CSS3 Section 5.2, Item 4.c.
    let query_weight = query_style.weight.unwrap_or(fonts::DEFAULT_WEIGHT);
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

        if query_weight < WEIGHT_NORMAL {
            // If query_weight < 400, then typefaces with weights <= query_weight are
            // preferred.
            if b.typeface.weight <= query_weight {
                return b;
            } else {
                return a;
            }
        } else if query_weight > WEIGHT_MEDIUM {
            // If query_weight > 500, then typefaces with weights >= query_weight are
            // preferred.
            if a.typeface.weight >= query_weight {
                return a;
            } else {
                return b;
            }
        } else {
            // This case is not adequately covered in CSS3, which only deals with weights in
            // multiples of 100. Since we support units of 1, we have to resort to CSS4
            // Section 5.2, Item 4.3, first bullet.

            // If query_weight is between 400 and 500, inclusive, then the preferred intervals are
            // 1. `(query_weight, 500]`, in ascending order
            // 2. `[1, query_weight)`, in descending order
            // 3. `[501, 1000]`, in ascending order
            if b.typeface.weight <= WEIGHT_MEDIUM {
                if a.typeface.weight > query_weight {
                    // q...a...b...500
                    return a;
                } else {
                    // a...b...q...500 OR a...q...b...500
                    return b;
                }
            } else {
                return a;
            }
        }
    }

    // If a and b are equivalent then give priority according to the order in the manifest.
    a
}

#[cfg(test)]
mod tests {
    use {
        super::super::{test_util::*, Typeface},
        super::*,
        fidl_fuchsia_fonts::{
            GenericFontFamily, Slant, TypefaceRequestFlags, Width, WEIGHT_BOLD, WEIGHT_EXTRA_BOLD,
            WEIGHT_EXTRA_LIGHT, WEIGHT_LIGHT, WEIGHT_MEDIUM, WEIGHT_NORMAL, WEIGHT_SEMI_BOLD,
            WEIGHT_THIN,
        },
    };

    /// Substitute for `TypefaceAndLangScore` that owns its typeface. Convert to a
    /// `TypefaceAndLangScore` using `(&self).into()`.
    struct TypefaceAndLangScoreWrapper {
        typeface: Typeface,
        lang_score: usize,
    }

    impl From<Typeface> for TypefaceAndLangScoreWrapper {
        fn from(typeface: Typeface) -> Self {
            TypefaceAndLangScoreWrapper { typeface, lang_score: 0 }
        }
    }

    impl<'a> From<&'a Typeface> for TypefaceAndLangScore<'a> {
        fn from(typeface: &'a Typeface) -> Self {
            TypefaceAndLangScore { typeface, lang_score: 0 }
        }
    }

    impl<'a> From<&'a TypefaceAndLangScoreWrapper> for TypefaceAndLangScore<'a> {
        fn from(source: &'a TypefaceAndLangScoreWrapper) -> TypefaceAndLangScore<'a> {
            TypefaceAndLangScore { typeface: &source.typeface, lang_score: source.lang_score }
        }
    }

    fn make_fake_typeface_and_lang_score(
        width: Width,
        slant: Slant,
        weight: u16,
        lang_score: usize,
    ) -> TypefaceAndLangScoreWrapper {
        TypefaceAndLangScoreWrapper {
            typeface: make_fake_typeface_style(width, slant, weight),
            lang_score,
        }
    }

    /// Lang score is more important than everything else.
    #[test]
    fn select_best_match_lang_score_over_others() {
        let better_lang_typeface =
            make_fake_typeface_and_lang_score(Width::Condensed, Slant::Italic, WEIGHT_MEDIUM, 3);
        let worse_lang_typeface = make_fake_typeface_and_lang_score(
            Width::UltraExpanded,
            Slant::Upright,
            WEIGHT_LIGHT,
            6,
        );

        assert_eq!(
            select_best_match(
                (&better_lang_typeface).into(),
                (&worse_lang_typeface).into(),
                make_style_request(Width::UltraExpanded, Slant::Upright, WEIGHT_LIGHT, false)
                    .query
                    .as_ref()
                    .unwrap()
            )
            .typeface,
            &better_lang_typeface.typeface
        );
    }

    /// Generic family is more important than style.
    #[test]
    fn test_fallback_generic_family_over_style() {
        let serif_typeface = make_fake_typeface(
            Width::Condensed,
            Slant::Italic,
            WEIGHT_MEDIUM,
            &vec![],
            &vec![],
            GenericFontFamily::Serif,
        );

        let fantasy_typeface = make_fake_typeface(
            Width::UltraExpanded,
            Slant::Upright,
            WEIGHT_LIGHT,
            &vec![],
            &vec![],
            GenericFontFamily::Fantasy,
        );

        let request = make_typeface_request(
            Width::Condensed,
            Slant::Italic,
            WEIGHT_MEDIUM,
            None,
            TypefaceRequestFlags::empty(),
            GenericFontFamily::Fantasy,
        );

        assert_eq!(
            select_best_match(
                (&serif_typeface).into(),
                (&fantasy_typeface).into(),
                request.query.as_ref().unwrap()
            )
            .typeface,
            &fantasy_typeface
        );
    }

    /// Calls `select_best_match` for the given type faces with the given simplified style request.
    fn compare_for_request<'a>(
        a: &'a Typeface,
        b: &'a Typeface,
        width: impl Into<Option<Width>>,
        slant: impl Into<Option<Slant>>,
        weight: impl Into<Option<u16>>,
    ) -> &'a Typeface {
        let request = make_style_request(width, slant, weight, false);
        select_best_match(a.into(), b.into(), request.query.as_ref().unwrap()).typeface
    }

    /// Width is more important than other style parameters.
    #[test]
    fn select_best_match_width_over_other_style() {
        let extra_condensed_upright_semi_bold =
            make_fake_typeface_style(Width::ExtraCondensed, Slant::Upright, WEIGHT_SEMI_BOLD);
        let condensed_italic_thin =
            make_fake_typeface_style(Width::Condensed, Slant::Italic, WEIGHT_THIN);

        assert_eq!(
            compare_for_request(
                &extra_condensed_upright_semi_bold,
                &condensed_italic_thin,
                Width::Condensed,
                Slant::Upright,
                WEIGHT_SEMI_BOLD
            )
            .width,
            Width::Condensed
        );
    }

    /// Uses default width value when omitted.
    #[test]
    fn select_best_match_width_default() {
        let extra_condensed_upright_semi_bold =
            make_fake_typeface_style(Width::ExtraCondensed, Slant::Upright, WEIGHT_SEMI_BOLD);
        let normal_italic_thin =
            make_fake_typeface_style(Width::Normal, Slant::Italic, WEIGHT_THIN);
        let extra_expanded_oblique_normal =
            make_fake_typeface_style(Width::ExtraExpanded, Slant::Oblique, WEIGHT_NORMAL);

        assert_eq!(
            compare_for_request(
                &extra_condensed_upright_semi_bold,
                &normal_italic_thin,
                None,
                Slant::Italic,
                WEIGHT_NORMAL
            )
            .width,
            Width::Normal
        );
        assert_eq!(
            compare_for_request(
                &normal_italic_thin,
                &extra_expanded_oblique_normal,
                None,
                Slant::Italic,
                WEIGHT_NORMAL
            )
            .width,
            Width::Normal
        );
    }

    fn make_width_style(width: Width) -> Typeface {
        make_fake_typeface_style(width, Slant::Upright, WEIGHT_NORMAL)
    }

    /// For requested width <= Normal (5), the priority is lower widths descending, then higher
    /// widths ascending.
    #[test]
    fn select_best_match_width_normal_or_condensed_prefers_lower_widths() {
        assert_eq!(
            compare_for_request(
                &make_width_style(Width::UltraCondensed),
                &make_width_style(Width::Condensed),
                Width::ExtraCondensed,
                Slant::Upright,
                WEIGHT_NORMAL
            )
            .width,
            Width::UltraCondensed
        );

        assert_eq!(
            compare_for_request(
                &make_width_style(Width::Condensed),
                &make_width_style(Width::Normal),
                Width::SemiCondensed,
                Slant::Upright,
                WEIGHT_NORMAL
            )
            .width,
            Width::Condensed
        );

        // This makes no sense as a user, but it's what the spec apparently dictates.
        assert_eq!(
            compare_for_request(
                &make_width_style(Width::UltraCondensed),
                &make_width_style(Width::SemiExpanded),
                Width::Normal,
                Slant::Upright,
                WEIGHT_NORMAL
            )
            .width,
            Width::UltraCondensed
        );
    }

    /// For requested width >= SemiExpanded (6), the priority is higher widths ascending, then lower
    /// widths descending.
    #[test]
    fn select_best_match_width_expanded_prefers_higher_widths() {
        assert_eq!(
            compare_for_request(
                &make_width_style(Width::Normal),
                &make_width_style(Width::Expanded),
                Width::SemiExpanded,
                Slant::Upright,
                WEIGHT_NORMAL
            )
            .width,
            Width::Expanded
        );

        assert_eq!(
            compare_for_request(
                &make_width_style(Width::Expanded),
                &make_width_style(Width::UltraExpanded),
                Width::ExtraExpanded,
                Slant::Upright,
                WEIGHT_NORMAL
            )
            .width,
            Width::UltraExpanded
        );

        assert_eq!(
            compare_for_request(
                &make_width_style(Width::ExtraExpanded),
                &make_width_style(Width::UltraExpanded),
                Width::Expanded,
                Slant::Upright,
                WEIGHT_NORMAL
            )
            .width,
            Width::ExtraExpanded
        );

        // Follow the spec, not common sense.
        assert_eq!(
            compare_for_request(
                &make_width_style(Width::Normal),
                &make_width_style(Width::UltraExpanded),
                Width::SemiExpanded,
                Slant::Upright,
                WEIGHT_NORMAL
            )
            .width,
            Width::UltraExpanded
        );
    }

    /// Slant is more important than weight.
    #[test]
    fn select_best_match_slant_vs_weight() {
        let italic_thin = make_fake_typeface_style(Width::Normal, Slant::Italic, WEIGHT_THIN);
        let upright_semi_bold =
            make_fake_typeface_style(Width::Normal, Slant::Upright, WEIGHT_SEMI_BOLD);
        let oblique_normal = make_fake_typeface_style(Width::Normal, Slant::Oblique, WEIGHT_NORMAL);

        assert_eq!(
            compare_for_request(
                &italic_thin,
                &upright_semi_bold,
                Width::Condensed,
                Slant::Upright,
                WEIGHT_THIN
            )
            .slant,
            Slant::Upright
        );

        assert_eq!(
            compare_for_request(
                &upright_semi_bold,
                &oblique_normal,
                Width::Condensed,
                Slant::Upright,
                WEIGHT_NORMAL
            )
            .slant,
            Slant::Upright
        );
    }

    /// Uses default slant value when omitted.
    #[test]
    fn select_best_match_slant_default() {
        let italic_thin = make_fake_typeface_style(Width::Normal, Slant::Italic, WEIGHT_THIN);
        let upright_semi_bold =
            make_fake_typeface_style(Width::Normal, Slant::Upright, WEIGHT_SEMI_BOLD);
        let oblique_normal = make_fake_typeface_style(Width::Normal, Slant::Oblique, WEIGHT_NORMAL);

        assert_eq!(
            compare_for_request(
                &italic_thin,
                &upright_semi_bold,
                Width::Condensed,
                None,
                WEIGHT_THIN
            )
            .slant,
            Slant::Upright
        );

        assert_eq!(
            compare_for_request(
                &upright_semi_bold,
                &oblique_normal,
                Width::Condensed,
                None,
                WEIGHT_NORMAL
            )
            .slant,
            Slant::Upright
        );
    }

    fn make_slant_style(slant: Slant) -> Typeface {
        make_fake_typeface_style(Width::Normal, slant, WEIGHT_NORMAL)
    }

    #[test]
    fn select_best_match_slant_exact_match() {
        assert_eq!(
            compare_for_request(
                &make_slant_style(Slant::Upright),
                &make_slant_style(Slant::Oblique),
                Width::Normal,
                Slant::Upright,
                WEIGHT_NORMAL
            )
            .slant,
            Slant::Upright
        );

        assert_eq!(
            compare_for_request(
                &make_slant_style(Slant::Italic),
                &make_slant_style(Slant::Oblique),
                Width::Normal,
                Slant::Oblique,
                WEIGHT_NORMAL
            )
            .slant,
            Slant::Oblique
        );

        assert_eq!(
            compare_for_request(
                &make_slant_style(Slant::Italic),
                &make_slant_style(Slant::Oblique),
                Width::Normal,
                Slant::Italic,
                WEIGHT_NORMAL
            )
            .slant,
            Slant::Italic
        );
    }

    /// Oblique can be substituted for italic and vice versa.
    #[test]
    fn select_best_match_slant_substitutes() {
        assert_eq!(
            compare_for_request(
                &make_slant_style(Slant::Upright),
                &make_slant_style(Slant::Oblique),
                Width::Normal,
                Slant::Italic,
                WEIGHT_NORMAL
            )
            .slant,
            Slant::Oblique
        );

        assert_eq!(
            compare_for_request(
                &make_slant_style(Slant::Upright),
                &make_slant_style(Slant::Italic),
                Width::Normal,
                Slant::Oblique,
                WEIGHT_NORMAL
            )
            .slant,
            Slant::Italic
        );
    }

    /// Requesting upright when it's unavailable means fall through to weight.
    #[test]
    fn select_best_match_slant_no_upright() {
        assert_eq!(
            compare_for_request(
                &make_fake_typeface_style(Width::Normal, Slant::Italic, WEIGHT_THIN),
                &make_fake_typeface_style(Width::Normal, Slant::Oblique, WEIGHT_BOLD),
                Width::Normal,
                Slant::Upright,
                WEIGHT_BOLD
            )
            .weight,
            WEIGHT_BOLD
        );
    }

    fn make_weight_style(weight: u16) -> Typeface {
        make_fake_typeface_style(Width::Normal, Slant::Upright, weight)
    }

    /// Uses default weight value when omitted.
    #[test]
    fn select_best_match_weight_default() {
        assert_eq!(
            compare_for_request(
                &make_weight_style(WEIGHT_BOLD),
                &make_weight_style(WEIGHT_NORMAL),
                Width::Normal,
                Slant::Upright,
                None
            )
            .weight,
            WEIGHT_NORMAL
        );
    }

    #[test]
    fn select_best_match_weight_exact_match() {
        assert_eq!(
            compare_for_request(
                &make_weight_style(WEIGHT_BOLD),
                &make_weight_style(WEIGHT_NORMAL),
                Width::Normal,
                Slant::Upright,
                WEIGHT_BOLD
            )
            .weight,
            WEIGHT_BOLD
        );

        assert_eq!(
            compare_for_request(
                &make_weight_style(517),
                &make_weight_style(WEIGHT_THIN),
                Width::Normal,
                Slant::Upright,
                517
            )
            .weight,
            517
        );
    }

    /// For requested weight < `WEIGHT_NORMAL`, the priority is lower weights descending, then
    /// higher weights ascending.
    #[test]
    fn select_best_match_weight_thinner_prefers_thinner() {
        assert_eq!(
            compare_for_request(
                &make_weight_style(WEIGHT_THIN),
                &make_weight_style(WEIGHT_EXTRA_LIGHT),
                Width::Normal,
                Slant::Upright,
                WEIGHT_LIGHT
            )
            .weight,
            WEIGHT_EXTRA_LIGHT
        );

        assert_eq!(
            compare_for_request(
                &make_weight_style(WEIGHT_THIN),
                &make_weight_style(WEIGHT_LIGHT),
                Width::Normal,
                Slant::Upright,
                WEIGHT_EXTRA_LIGHT
            )
            .weight,
            WEIGHT_THIN
        );

        assert_eq!(
            compare_for_request(
                &make_weight_style(WEIGHT_SEMI_BOLD),
                &make_weight_style(WEIGHT_EXTRA_BOLD),
                Width::Normal,
                Slant::Upright,
                WEIGHT_LIGHT
            )
            .weight,
            WEIGHT_SEMI_BOLD
        );
    }

    /// For requested weight > `WEIGHT_MEDIUM`, the priority is higher weights ascending, then lower
    /// weights descending.
    #[test]
    fn select_best_match_weight_thicker_prefers_thicker() {
        assert_eq!(
            compare_for_request(
                &make_weight_style(WEIGHT_SEMI_BOLD),
                &make_weight_style(WEIGHT_EXTRA_BOLD),
                Width::Normal,
                Slant::Upright,
                WEIGHT_BOLD
            )
            .weight,
            WEIGHT_EXTRA_BOLD
        );

        assert_eq!(
            compare_for_request(
                &make_weight_style(WEIGHT_THIN),
                &make_weight_style(WEIGHT_NORMAL),
                Width::Normal,
                Slant::Upright,
                WEIGHT_EXTRA_BOLD
            )
            .weight,
            WEIGHT_NORMAL
        );
    }

    /// For requested weight `WEIGHT_NORMAL`, `WEIGHT_MEDIUM` is preferred, followed by lower
    /// weights.
    #[test]
    fn select_best_match_weight_normal_prefers_medium_then_thinner() {
        assert_eq!(
            compare_for_request(
                &make_weight_style(WEIGHT_MEDIUM),
                &make_weight_style(WEIGHT_LIGHT),
                Width::Normal,
                Slant::Upright,
                WEIGHT_NORMAL
            )
            .weight,
            WEIGHT_MEDIUM
        );

        assert_eq!(
            compare_for_request(
                &make_weight_style(WEIGHT_BOLD),
                &make_weight_style(WEIGHT_EXTRA_LIGHT),
                Width::Normal,
                Slant::Upright,
                WEIGHT_NORMAL
            )
            .weight,
            WEIGHT_EXTRA_LIGHT
        );
    }

    /// For requested weight `WEIGHT_MEDIUM`, `WEIGHT_NORMAL` is preferred, followed by lower
    /// weights.
    #[test]
    fn select_best_match_weight_medium_prefers_normal_then_thinner() {
        assert_eq!(
            compare_for_request(
                &make_weight_style(WEIGHT_NORMAL),
                &make_weight_style(WEIGHT_EXTRA_BOLD),
                Width::Normal,
                Slant::Upright,
                WEIGHT_MEDIUM
            )
            .weight,
            WEIGHT_NORMAL
        );

        assert_eq!(
            compare_for_request(
                &make_weight_style(WEIGHT_LIGHT),
                &make_weight_style(WEIGHT_BOLD),
                Width::Normal,
                Slant::Upright,
                WEIGHT_MEDIUM
            )
            .weight,
            WEIGHT_LIGHT
        );
    }

    /// For requested weight between `WEIGHT_NORMAL` and `WEIGHT_MEDIUM`, inclusive, weights above
    /// the requested <= 500 are preferred, then below the requested descending, then above 500
    /// ascending.
    #[test]
    fn select_best_match_weight_between_normal_and_medium() {
        assert_eq!(
            compare_for_request(
                &make_weight_style(440),
                &make_weight_style(460),
                Width::Normal,
                Slant::Upright,
                450
            )
            .weight,
            460
        );

        assert_eq!(
            compare_for_request(
                &make_weight_style(440),
                &make_weight_style(500),
                Width::Normal,
                Slant::Upright,
                450
            )
            .weight,
            500
        );

        assert_eq!(
            compare_for_request(
                &make_weight_style(440),
                &make_weight_style(501),
                Width::Normal,
                Slant::Upright,
                450
            )
            .weight,
            440
        );

        assert_eq!(
            compare_for_request(
                &make_weight_style(300),
                &make_weight_style(501),
                Width::Normal,
                Slant::Upright,
                450
            )
            .weight,
            300
        );

        assert_eq!(
            compare_for_request(
                &make_weight_style(413),
                &make_weight_style(449),
                Width::Normal,
                Slant::Upright,
                450
            )
            .weight,
            449
        );
    }
}
