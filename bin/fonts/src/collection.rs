// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::font_info;
use super::manifest;
use fidl_fuchsia_fonts as fonts;
use std::collections::BTreeSet;
use std::sync::Arc;

pub type LanguageSet = BTreeSet<String>;

pub struct Font {
    pub asset_id: u32,
    pub font_index: u32,
    pub slant: fonts::Slant,
    pub weight: u32,
    pub width: u32,
    pub language: LanguageSet,
    pub info: font_info::FontInfo,
    pub fallback_group: fonts::FallbackGroup,
}

impl Font {
    pub fn new(
        asset_id: u32, manifest: manifest::Font, info: font_info::FontInfo,
        fallback_group: fonts::FallbackGroup,
    ) -> Font {
        Font {
            asset_id,
            font_index: manifest.index,
            weight: manifest.weight,
            width: manifest.width,
            slant: manifest.slant,
            language: manifest
                .language
                .iter()
                .map(|x| x.to_ascii_lowercase())
                .collect(),
            info,
            fallback_group,
        }
    }
}

struct FontAndLangScore<'a> {
    font: &'a Font,
    lang_score: usize,
}

// Returns value in the range [0, 2*request_lang.len()]. First half is used
// for exact matches, the second half is for partial matches.
fn get_lang_match_score(font: &Font, request_lang: &[String]) -> usize {
    let mut best_partial_match_pos = None;
    for i in 0..request_lang.len() {
        let lang = &request_lang[i];

        // Iterate all language in the font that start with |lang|.
        for font_lang in font
            .language
            .range::<String, std::ops::RangeFrom<&String>>(lang..)
        {
            if !font_lang.starts_with(lang.as_str()) {
                break;
            }

            if font_lang.len() == lang.len() {
                // Exact match.
                return i;
            }

            // Partial match is valid only when it's followed by '-' character
            // (45 in ascii).
            if (font_lang.as_bytes()[lang.len()] == 45) & best_partial_match_pos.is_none() {
                best_partial_match_pos = Some(i);
                continue;
            }
        }
    }

    best_partial_match_pos.unwrap_or(request_lang.len()) + request_lang.len()
}

impl<'a> FontAndLangScore<'a> {
    fn new(font: &'a Font, request: &fonts::Request) -> FontAndLangScore<'a> {
        FontAndLangScore {
            font,
            lang_score: get_lang_match_score(font, &request.language),
        }
    }
}

const FONT_WIDTH_NORMAL: u32 = 5;

// Selects between fonts |a| and |b| for the |request|. Fonts are passed in
// FontAndLangScore so the language match score is calculated only once for each
// font. If |a| and |b| are equivalent then |a| is returned.
// The style matching logic follows the CSS3 Fonts spec (see Section 5.2,
// Item 4: https://www.w3.org/TR/css-fonts-3/#font-style-matching ) with 2
// additions:
//   1. Fonts with higher language match score are preferred. The score value
//      is expected to be pre-calculated by get_lang_match_score(). Note that if
//      the request specifies a character then the fonts are expected to be
//      already filtered based on that character, i.e. they both contain that
//      character, so this function doesn't need to verify it.
//   2. If the request specifies |fallback_group| then fonts with the same
//      |fallback_group| are preferred.
fn select_best_match<'a, 'b>(
    mut a: FontAndLangScore<'a>, mut b: FontAndLangScore<'a>, request: &'b fonts::Request,
) -> FontAndLangScore<'a> {
    if a.lang_score != b.lang_score {
        if a.lang_score < b.lang_score {
            return a;
        } else {
            return b;
        }
    }

    if (request.fallback_group != fonts::FallbackGroup::None)
        & (a.font.fallback_group != b.font.fallback_group)
    {
        if a.font.fallback_group == request.fallback_group {
            return a;
        } else if b.font.fallback_group == request.fallback_group {
            return b;
        }
        // If fallback_group of a and b doesn't match the request then
        // fall-through to compare them based on style parameters.
    }

    // Select based on width, see CSS3 Section 5.2, Item 4.a.
    if a.font.width != b.font.width {
        // Reorder a and b, so a has lower width.
        if a.font.width > b.font.width {
            std::mem::swap(&mut a, &mut b)
        }
        if request.width <= FONT_WIDTH_NORMAL {
            if b.font.width <= request.width {
                return b;
            } else {
                return a;
            }
        } else {
            if a.font.width >= request.width {
                return a;
            } else {
                return b;
            }
        }
    }

    // Select based on slant, CSS3 Section 5.2, Item 4.b.
    match (request.slant, a.font.slant, b.font.slant) {
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

    // Select based on weight, CSS3 Section 5.2, Item 4.c.
    if a.font.weight != b.font.weight {
        // Reorder a and b, so a has lower weight.
        if a.font.weight > b.font.weight {
            std::mem::swap(&mut a, &mut b)
        }

        if a.font.weight == request.weight {
            return a;
        }
        if b.font.weight == request.weight {
            return b;
        }

        if request.weight < 400 {
            // If request.weight < 400, then fonts with
            // weights <= request.weight are preferred.
            if b.font.weight <= request.weight {
                return b;
            } else {
                return a;
            }
        } else if request.weight > 500 {
            // If request.weight > 500, then fonts with
            // weights >= request.weight are preferred.
            if a.font.weight >= request.weight {
                return a;
            } else {
                return b;
            }
        } else {
            // request.weight is 400 or 500.
            if b.font.weight <= 500 {
                if (a.font.weight as i32 - request.weight as i32).abs()
                    < (b.font.weight as i32 - request.weight as i32).abs()
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

    // If a and b are equivalent then give priority according to the order in
    // the manifest.
    a
}

pub struct FontCollection {
    // Some fonts may be in more than one collections. Particularly fallback fonts
    // are added to the family collection and also to the fallback collection.
    fonts: Vec<Arc<Font>>,
}

impl FontCollection {
    pub fn new() -> FontCollection {
        FontCollection { fonts: vec![] }
    }

    pub fn match_request<'a, 'b>(&'a self, request: &'b fonts::Request) -> Option<&'a Font> {
        if (request.flags & fonts::REQUEST_FLAG_EXACT_MATCH) > 0 {
            return self
                .fonts
                .iter()
                .find(|font| {
                    (font.width == request.width)
                        & (font.weight == request.weight)
                        & (font.slant == request.slant)
                        & (request.language.is_empty() || request
                            .language
                            .iter()
                            .find(|lang| font.language.contains(*lang))
                            .is_some())
                        & ((request.character == 0) | font.info.charset.contains(request.character))
                }).map(|f| f as &Font);
        }

        fn fold<'a, 'b>(
            best: Option<FontAndLangScore<'a>>, x: &'a Font, request: &'b fonts::Request,
        ) -> Option<FontAndLangScore<'a>> {
            let x = FontAndLangScore::new(x, request);
            match best {
                Some(b) => Some(select_best_match(b, x, request)),
                None => Some(x),
            }
        }

        self.fonts
            .iter()
            .filter(|f| (request.character == 0) | f.info.charset.contains(request.character))
            .fold(None, |best, x| fold(best, x, request))
            .map(|a| a.font)
    }

    pub fn is_empty(&self) -> bool {
        self.fonts.is_empty()
    }

    pub fn add_font(&mut self, font: Arc<Font>) {
        self.fonts.push(font);
    }

    pub fn get_styles<'a>(&'a self) -> impl Iterator<Item = fonts::Style> + 'a {
        self.fonts.iter().map(|f|
            fonts::Style { width: f.width, slant: f.slant, weight: f.weight}
        )
    }
}

#[cfg(test)]
mod tests {
    use super::font_info::{CharSet, FontInfo};
    use super::*;
    use fidl_fuchsia_fonts::{self as fonts, FallbackGroup, Slant::*};

    fn make_fake_font_collection(mut fonts: Vec<Font>) -> FontCollection {
        let mut result = FontCollection::new();
        for (i, mut font) in fonts.drain(..).enumerate() {
            // Assign fake asset_id to each font
            font.asset_id = i as u32;
            result.add_font(Arc::new(font));
        }

        result
    }

    fn make_fake_font(
        width: u32, slant: fonts::Slant, weight: u32, lang: &[&str], charset: &[u32],
        fallback_group: FallbackGroup,
    ) -> Font {
        Font::new(
            0,
            manifest::Font {
                asset: std::path::PathBuf::new(),
                index: 0,
                slant,
                weight,
                width,
                language: lang.iter().map(|s| s.to_string()).collect(),
            },
            FontInfo {
                charset: CharSet::new(charset.to_vec()),
            },
            fallback_group,
        )
    }

    fn request_font<'a, 'b>(
        collection: &'a FontCollection, width: u32, slant: fonts::Slant, weight: u32,
        lang: &'b [&'b str], flags: u32, fallback_group: FallbackGroup,
    ) -> Option<&'a Font> {
        let request = fonts::Request {
            family: String::new(),
            weight: weight,
            width: width,
            slant: slant,
            character: 0,
            language: lang.iter().map(|s| s.to_ascii_lowercase()).collect(),
            fallback_group: fallback_group,
            flags: flags,
        };

        collection.match_request(&request)
    }

    fn make_fake_font_style(width: u32, slant: fonts::Slant, weight: u32) -> Font {
        make_fake_font(width, slant, weight, &[], &[], FallbackGroup::None)
    }

    fn request_style(
        collection: &FontCollection, width: u32, slant: fonts::Slant, weight: u32,
    ) -> &Font {
        request_font(
            collection,
            width,
            slant,
            weight,
            &[],
            0,
            FallbackGroup::None,
        ).unwrap()
    }

    #[test]
    fn test_font_matching_width() {
        let collection = make_fake_font_collection(vec![
            make_fake_font_style(2, Upright, 600),
            make_fake_font_style(3, Italic, 100),
            make_fake_font_style(8, Oblique, 400),
        ]);

        // width is more important than other style parameters.
        assert!(request_style(&collection, 3, Italic, 400).width == 3);

        // For width <= 5 lower widths are preferred.
        assert!(request_style(&collection, 2, Italic, 400).width == 2);
        assert!(request_style(&collection, 4, Italic, 400).width == 3);

        // For width > 4 higher widths are preferred.
        assert!(request_style(&collection, 6, Italic, 400).width == 8);

        // Otherwise expect font with the closest width.
        assert!(request_style(&collection, 1, Italic, 400).width == 2);
        assert!(request_style(&collection, 9, Italic, 400).width == 8);
    }

    #[test]
    fn test_font_matching_slant() {
        let collection = make_fake_font_collection(vec![
            make_fake_font_style(5, Upright, 600),
            make_fake_font_style(5, Italic, 100),
            make_fake_font_style(5, Oblique, 400),
        ]);

        // slant is more important than weight.
        assert!(request_style(&collection, 3, Upright, 400).slant == Upright);
        assert!(request_style(&collection, 3, Italic, 400).slant == Italic);
        assert!(request_style(&collection, 3, Oblique, 400).slant == Oblique);

        let collection = make_fake_font_collection(vec![
            make_fake_font_style(5, Upright, 600),
            make_fake_font_style(5, Oblique, 400),
        ]);

        // Oblique is selected when Italic is requested.
        assert!(request_style(&collection, 3, Italic, 400).slant == Oblique);
    }

    #[test]
    fn test_font_matching_weight() {
        let collection = make_fake_font_collection(vec![
            make_fake_font_style(5, Upright, 700),
            make_fake_font_style(5, Upright, 200),
            make_fake_font_style(5, Upright, 400),
        ]);

        // Exact match.
        assert!(request_style(&collection, 3, Upright, 200).weight == 200);
        assert!(request_style(&collection, 3, Upright, 400).weight == 400);
        assert!(request_style(&collection, 3, Upright, 700).weight == 700);

        // For weight < 400 lower weights are preferred.
        assert!(request_style(&collection, 3, Upright, 300).weight == 200);

        // For weight > 500 higher weights are preferred.
        assert!(request_style(&collection, 3, Upright, 600).weight == 700);

        // For request.weight = 500 the font with weight == 400 is preferred.
        assert!(request_style(&collection, 3, Upright, 500).weight == 400);

        // Otherwise expect font with the closest weight.
        assert!(request_style(&collection, 3, Upright, 100).weight == 200);
        assert!(request_style(&collection, 3, Upright, 800).weight == 700);
    }

    fn request_style_exact(
        collection: &FontCollection, width: u32, slant: fonts::Slant, weight: u32,
    ) -> Option<&Font> {
        request_font(
            collection,
            width,
            slant,
            weight,
            &[],
            fonts::REQUEST_FLAG_EXACT_MATCH,
            FallbackGroup::None,
        )
    }

    #[test]
    fn test_font_matching_exact() {
        let collection = make_fake_font_collection(vec![
            make_fake_font_style(2, Upright, 600),
            make_fake_font_style(3, Italic, 100),
            make_fake_font_style(8, Oblique, 400),
        ]);

        assert!(
            request_style_exact(&collection, 3, Italic, 100)
                .unwrap()
                .asset_id
                == 1
        );

        assert!(request_style_exact(&collection, 4, Italic, 100).is_none());
        assert!(request_style_exact(&collection, 3, Upright, 100).is_none());
        assert!(request_style_exact(&collection, 3, Italic, 300).is_none());
    }

    fn make_fake_font_lang(lang: &[&str]) -> Font {
        make_fake_font(5, Upright, 400, lang, &[], FallbackGroup::None)
    }

    fn request_lang<'a, 'b>(collection: &'a FontCollection, lang: &'b [&'b str]) -> &'a Font {
        request_font(collection, 5, Upright, 400, lang, 0, FallbackGroup::None).unwrap()
    }

    #[test]
    fn test_font_matching_lang() {
        let collection = make_fake_font_collection(vec![
            make_fake_font_lang(&["a"]),
            make_fake_font_lang(&["b-C"]),
            make_fake_font_lang(&["b-D", "b-E"]),
            make_fake_font_lang(&["fooo"]),
            make_fake_font_lang(&["foo-BAR"]),
        ]);

        // Exact matches.
        assert!(request_lang(&collection, &["a"]).asset_id == 0);
        assert!(request_lang(&collection, &["b-C"]).asset_id == 1);
        assert!(request_lang(&collection, &["b-E"]).asset_id == 2);

        // Verify that request language order is respected.
        assert!(request_lang(&collection, &["b-C", "a"]).asset_id == 1);

        // Partial match: the first matching font is returned first.
        assert!(request_lang(&collection, &["b"]).asset_id == 1);

        // Exact match overrides preceding partial match.
        assert!(request_lang(&collection, &["b", "a"]).asset_id == 0);

        // Partial match should match a whole BCP47 segment.
        assert!(request_lang(&collection, &["foo"]).asset_id == 4);
    }

    fn make_fake_font_fallback_group(fallback_group: FallbackGroup) -> Font {
        make_fake_font(5, Upright, 400, &[], &[], fallback_group)
    }

    fn request_fallback_group(collection: &FontCollection, fallback_group: FallbackGroup) -> &Font {
        request_font(collection, 5, Upright, 400, &[], 0, fallback_group).unwrap()
    }

    #[test]
    fn test_font_matching_fallback_group() {
        let collection = make_fake_font_collection(vec![
            make_fake_font_fallback_group(FallbackGroup::Serif),
            make_fake_font_fallback_group(FallbackGroup::SansSerif),
            make_fake_font_fallback_group(FallbackGroup::Monospace),
        ]);

        assert!(request_fallback_group(&collection, FallbackGroup::Serif).asset_id == 0);
        assert!(request_fallback_group(&collection, FallbackGroup::SansSerif).asset_id == 1);
        assert!(request_fallback_group(&collection, FallbackGroup::Monospace).asset_id == 2);

        // First font is returned when there is no exact match.
        assert!(request_fallback_group(&collection, FallbackGroup::Cursive).asset_id == 0);
    }
}
