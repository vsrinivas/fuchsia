// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::font_service::{inspect::zero_pad, AssetId},
    anyhow::{format_err, Error},
    char_set::CharSet,
    fidl_fuchsia_fonts::{FamilyName, GenericFontFamily, Slant, Style2, TypefaceRequest, Width},
    fidl_fuchsia_fonts_experimental::TypefaceInfo,
    fidl_fuchsia_intl::LocaleId,
    fuchsia_inspect as finspect,
    heck::KebabCase,
    itertools::Itertools,
    manifest::v2,
    std::collections::BTreeSet,
};

/// Asset ID and font index.
#[derive(Debug, Clone, Eq, PartialEq, Hash, Ord, PartialOrd)]
pub struct TypefaceId {
    /// In-memory ID of the font asset.
    pub asset_id: AssetId,
    /// Index of the typeface within the asset.
    pub index: u32,
}

/// In-memory representation of a single typeface's metadata, slightly denormalized.
#[derive(Debug, Eq, PartialEq, Hash)]
pub struct Typeface {
    /// Which asset to find the typeface in
    pub asset_id: AssetId,
    /// Index of the typeface within the asset (for multi-typeface font formats, such as TTC)
    pub font_index: u32,
    /// Style property: slant
    pub slant: Slant,
    /// Style property: weight
    pub weight: u16,
    /// Style property: width
    pub width: Width,
    /// List of BCP-47 language IDs explicitly supported by the typeface
    pub languages: BTreeSet<String>,
    /// Collection of code points contained by the typeface
    pub char_set: CharSet,
    /// A generic font family to which the typeface's family belongs.
    pub generic_family: Option<GenericFontFamily>,
}

impl Typeface {
    /// Create a new `Typeface`, copying all fields except `asset_id` and `generic_family` from
    /// `manifest_typeface`.
    pub fn new(
        asset_id: AssetId,
        manifest_typeface: v2::Typeface,
        generic_family: Option<GenericFontFamily>,
    ) -> Result<Typeface, Error> {
        if manifest_typeface.code_points.is_empty() {
            return Err(format_err!("Can't create Typeface from Font with empty CharSet."));
        }
        Ok(Typeface {
            asset_id,
            font_index: manifest_typeface.index,
            weight: manifest_typeface.style.weight,
            width: manifest_typeface.style.width,
            slant: manifest_typeface.style.slant,
            languages: manifest_typeface.languages.into_iter().collect(),
            char_set: manifest_typeface.code_points,
            generic_family,
        })
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
    pub asset_id: AssetId,
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
            asset_id: Some(info.asset_id.into()),
            font_index: Some(info.font_index),
            family: Some(info.family),
            style: Some(info.style),
            languages: Some(info.languages),
            generic_family: info.generic_family,
        }
    }
}

/// Inspect data for a `Typeface`.
#[derive(Debug)]
pub struct TypefaceInspectData {
    /// The main `Node` for the typeface.
    node: finspect::Node,
    /// Numeric asset ID.
    asset_id: finspect::UintProperty,
    /// Path or URL to the asset.
    asset_location: Option<finspect::StringProperty>,
    /// Index of the typeface within the font asset.
    font_index: finspect::UintProperty,
    /// Style properties of the typeface.
    style: finspect::Node,
    /// Languages supported by the asset, as a sequence of BCP-47 language tags joined on ", ".
    languages: finspect::StringProperty,
    /// Number of code points covered by the typeface.
    code_point_count: finspect::UintProperty,
    /// Name of the font family. This should only be filled in in contexts where the typefaces are
    /// not already grouped by family (e.g. fallback chain).
    family_name: Option<finspect::StringProperty>,
}

impl TypefaceInspectData {
    #![allow(dead_code)]

    /// Creates a new `TypefaceInspectData`, which contains a `Node` with details.
    ///
    /// * `parent_node`: The node that will contain this node.
    /// * `node_name`: Arbitrary display name for this node that depends on the context in which the
    ///    node is displayed.
    /// * `typeface`: The typeface for which Inspect data should be generated.
    /// * `asset_location_lookup`: A closure for retrieving an asset's path or URL by asset ID.
    pub fn new(
        parent_node: &finspect::Node,
        node_name: &str,
        typeface: &Typeface,
        asset_location_lookup: &impl Fn(AssetId) -> Option<String>,
    ) -> Self {
        let node = parent_node.create_child(node_name);
        let asset_id = node.create_uint("asset_id", typeface.asset_id.0.into());
        let asset_location = (*asset_location_lookup)(typeface.asset_id)
            .map(|location| node.create_string("asset_location", &location));

        let font_index = node.create_uint("font_index", typeface.font_index.into());
        let style = {
            let style = node.create_child("style");
            style.record_string("slant", format!("{:?}", typeface.slant).to_kebab_case());
            style.record_uint("weight", typeface.weight.into());
            style.record_string("width", format!("{:?}", typeface.width).to_kebab_case());
            style
        };
        let languages = node.create_string("languages", typeface.languages.iter().join(", "));
        let code_point_count = node.create_uint("code_point_count", typeface.char_set.len() as u64);
        let family_name = None;
        TypefaceInspectData {
            node,
            asset_id,
            asset_location,
            font_index,
            style,
            languages,
            code_point_count,
            family_name,
        }
    }

    /// Creates a new `TypefaceInspectData`, which contains a `Node` with details. The node's name
    /// is a padded numeric string (because Inspect doesn't support node arrays).
    ///
    /// * `parent_node`: The node that will contain this node.
    /// * `node_index`: The index of the typeface node within the list in which it's being shown.
    /// * `node_count`: The total number of sibling typeface nodes.
    /// * `typeface`: The typeface for which Inspect data should be generated.
    /// * `asset_location_lookup`: A closure for retrieving an asset's path or URL by asset ID.
    pub fn with_numbered_node_name(
        parent_node: &finspect::Node,
        node_index: usize,
        node_count: usize,
        typeface: &Typeface,
        asset_location_lookup: &impl Fn(AssetId) -> Option<String>,
    ) -> Self {
        Self::new(parent_node, &zero_pad(node_index, node_count), typeface, asset_location_lookup)
    }

    /// Allows specifying a font family name, for use in Inspect contexts where it isn't obvious.
    pub fn with_family_name(mut self, family_name: &str) -> Self {
        let family_name = Some((&self.node).create_string("family_name", family_name));
        self.family_name = family_name;
        self
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        char_collection::char_collect,
        fidl_fuchsia_fonts::{Slant, Width, WEIGHT_NORMAL},
        finspect::assert_inspect_tree,
        maplit::btreeset,
    };

    #[test]
    fn test_typeface_new_empty_char_set_is_error() {
        let manifest_typeface = v2::Typeface {
            index: 0,
            style: v2::Style { slant: Slant::Upright, weight: WEIGHT_NORMAL, width: Width::Normal },
            languages: vec![],
            code_points: CharSet::new(vec![]),
        };

        assert!(Typeface::new(AssetId(0), manifest_typeface, None).is_err())
    }

    #[test]
    fn test_typeface_inspect_data() {
        let inspector = finspect::Inspector::new();

        let typeface = Typeface {
            asset_id: AssetId(5),
            font_index: 2,
            slant: Slant::Upright,
            weight: 300,
            width: Width::UltraCondensed,
            languages: btreeset!("es-ES".to_string(), "en-US".to_string()),
            char_set: char_collect!(0x0..=0xFF).into(),
            generic_family: Some(GenericFontFamily::Fantasy),
        };

        let inspect_data = TypefaceInspectData::with_numbered_node_name(
            inspector.root(),
            17,
            150,
            &typeface,
            &|asset_id| {
                if asset_id == AssetId(5) {
                    Some("/path/to/asset-5.ttf".to_string())
                } else {
                    None
                }
            },
        );

        assert_inspect_tree!(inspector, root: {
            "017": {
                asset_id: 5u64,
                asset_location: "/path/to/asset-5.ttf",
                font_index: 2u64,
                style: {
                    slant: "upright",
                    weight: 300u64,
                    width: "ultra-condensed",
                },
                languages: "en-US, es-ES",
                code_point_count: 256u64,
            },
        });

        let _inspect_data = inspect_data.with_family_name("Alpha");

        assert_inspect_tree!(inspector, root: {
            "017": contains {
                asset_id: 5u64,
                family_name: "Alpha",
            },
        });
    }
}
