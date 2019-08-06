// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::typeface::TypefaceAndLangScore,
    fidl_fuchsia_fonts::{Slant, TypefaceQuery, Width, WEIGHT_MEDIUM, WEIGHT_NORMAL},
};

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

    if let Some(query_style) = &query.style {
        // Select based on width, see CSS3 Section 5.2, Item 4.a.
        if let Some(query_width) = query_style.width {
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
                (Slant::Italic, Slant::Oblique, _) => return a,
                (Slant::Italic, _, Slant::Oblique) => return b,

                (Slant::Oblique, Slant::Italic, _) => return a,
                (Slant::Oblique, _, Slant::Italic) => return b,

                // In case upright font is requested, but we have only italic and
                // oblique then fall through to select based on weight.
                (Slant::Upright, _, _) => (),

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

                if query_weight < WEIGHT_NORMAL {
                    // If query_weight < 400, then typefaces with weights <= query_weight are
                    // preferred.
                    if b.typeface.weight <= query_weight {
                        return b;
                    } else {
                        return a;
                    }
                } else if query_weight > WEIGHT_MEDIUM {
                    // If request.weight > 500, then typefaces with weights >= query_weight are
                    // preferred.
                    if a.typeface.weight >= query_weight {
                        return a;
                    } else {
                        return b;
                    }
                } else {
                    // request.weight is 400 or 500.
                    if b.typeface.weight <= WEIGHT_MEDIUM {
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
