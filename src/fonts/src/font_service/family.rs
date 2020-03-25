// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{
        inspect::zero_pad,
        typeface::{
            Collection as TypefaceCollection, Typeface, TypefaceCollectionBuilder, TypefaceId,
            TypefaceInfoAndCharSet, TypefaceInspectData,
        },
        AssetId,
    },
    fidl_fuchsia_fonts::GenericFontFamily,
    fuchsia_inspect as finspect,
    heck::KebabCase,
    itertools::Itertools,
    manifest::{serde_ext::StyleOptions, v2},
    std::{collections::BTreeMap, sync::Arc},
    unicase::UniCase,
};

#[derive(Debug, Eq, Hash, PartialEq)]
pub enum FamilyOrAlias {
    Family(FontFamily),
    /// The definition of a `Family`'s alias. This is a representation of the alias's _target_
    /// (value). The alias's name (key) is stored outside of this enum.
    ///
    /// * `UniCase<String>` - The _canonical_ family name.
    /// * `Option<Arc<TypefaceQueryOverrides>>` - Optional query overrides applied when a client
    ///    requests the associated alias.
    Alias(UniCase<String>, Option<Arc<TypefaceQueryOverrides>>),
}

/// Builder for [FamilyOrAlias].
#[derive(Debug)]
pub enum FamilyOrAliasBuilder {
    Family(FontFamilyBuilder),
    Alias(UniCase<String>, Option<Arc<TypefaceQueryOverrides>>),
}

impl FamilyOrAliasBuilder {
    /// Finalizes and builds a `FamilyOrAlias`.
    pub fn build(self) -> FamilyOrAlias {
        match self {
            FamilyOrAliasBuilder::Family(family_builder) => {
                FamilyOrAlias::Family(family_builder.build())
            }
            FamilyOrAliasBuilder::Alias(name, overrides) => FamilyOrAlias::Alias(name, overrides),
        }
    }

    /// Generates a full set of aliases from a given manifest v2 `Family`, including alias variants
    /// without spaces.
    pub fn aliases_from_family(
        manifest_family: &v2::Family,
    ) -> BTreeMap<UniCase<String>, FamilyOrAliasBuilder> {
        let mut output: BTreeMap<UniCase<String>, FamilyOrAliasBuilder> = BTreeMap::new();
        let family_name = UniCase::new(manifest_family.name.clone());
        let family_name_without_spaces = UniCase::new(manifest_family.name.replace(" ", ""));
        if family_name_without_spaces != family_name {
            output.insert(
                family_name_without_spaces,
                FamilyOrAliasBuilder::Alias(family_name.clone(), None),
            );
        }

        for alias_set in &manifest_family.aliases {
            let overrides = if alias_set.has_overrides() {
                Some(Arc::new(TypefaceQueryOverrides {
                    style: alias_set.style_overrides().clone(),
                    languages: alias_set.language_overrides().cloned().collect_vec(),
                }))
            } else {
                None
            };

            for alias_name in alias_set.names() {
                let alias_name = UniCase::new(alias_name.clone());
                output.insert(
                    alias_name.clone(),
                    FamilyOrAliasBuilder::Alias(family_name.clone(), overrides.clone()),
                );

                let alias_name_without_spaces = UniCase::new((*alias_name).replace(" ", ""));
                if alias_name != alias_name_without_spaces {
                    output.insert(
                        alias_name_without_spaces,
                        FamilyOrAliasBuilder::Alias(family_name.clone(), overrides.clone()),
                    );
                }
            }
        }

        output
    }
}

/// Optional set of typeface query overrides embedded in a `FontFamilyAlias`.
///
/// For example, the if a client asks for alias "Roboto Condensed", the server knows that means
/// means the canonical family "Roboto" with style `width: condensed`. If the client asks for "Noto
/// Sans CJK JP", that's interpreted as the canonical family "Noto Sans CJK" with
/// `languages: [ "ja" ]`.
///
#[derive(Debug, Eq, PartialEq, Hash, Ord, PartialOrd)]
pub struct TypefaceQueryOverrides {
    pub style: StyleOptions,
    pub languages: Vec<String>,
}

impl TypefaceQueryOverrides {
    pub fn has_style_overrides(&self) -> bool {
        self.style != StyleOptions::default()
    }

    pub fn has_language_overrides(&self) -> bool {
        !self.languages.is_empty()
    }
}

/// In-memory representation of metadata for a font family and its typefaces.
#[derive(Debug, Eq, PartialEq, Hash)]
pub struct FontFamily {
    /// The family's canonical name
    pub name: String,
    /// Collection of typefaces in the family
    pub faces: TypefaceCollection,
    /// Generic font family that this family belongs to, if applicable
    pub generic_family: Option<GenericFontFamily>,
}

impl FontFamily {
    /// Get owned copies of the family's typefaces as `TypefaceInfo`
    pub fn extract_faces<'a>(&'a self) -> impl Iterator<Item = TypefaceInfoAndCharSet> + 'a {
        // Convert Vec<Arc<Typeface>> to Vec<TypefaceInfo>
        self.faces
            .faces
            .iter()
            // Copy most fields from `Typeface` and use the canonical family name
            .map(move |face| TypefaceInfoAndCharSet::from_typeface(face, self.name.clone()))
    }
}

/// Builder for [FontFamily].
#[derive(Debug)]
pub struct FontFamilyBuilder {
    /// The family's canonical name
    name: String,
    /// Generic font family that this family belongs to, if applicable
    generic_family: Option<GenericFontFamily>,
    /// Collection of typefaces in the family
    faces: TypefaceCollectionBuilder,
}

impl FontFamilyBuilder {
    /// Creates a new builder without any typefaces defined.
    pub fn new(
        name: impl Into<String>,
        generic_family: Option<GenericFontFamily>,
    ) -> FontFamilyBuilder {
        FontFamilyBuilder {
            name: name.into(),
            generic_family,
            faces: TypefaceCollectionBuilder::new(),
        }
    }

    /// Returns true if the family's typeface collection already has a typeface with the given ID.
    pub fn has_typeface_id(&self, typeface_id: &TypefaceId) -> bool {
        self.faces.has_typeface_id(typeface_id)
    }

    /// If there's no existing typeface with the same typeface ID already in the builder, adds the
    /// given typeface and and returns `true`. Otherwise, returns `false`.
    pub fn add_typeface_once(&mut self, typeface: Arc<Typeface>) -> bool {
        self.faces.add_typeface_once(typeface)
    }

    /// Finalizes and builds a `FontFamily`.
    pub fn build(self) -> FontFamily {
        FontFamily {
            name: self.name,
            faces: self.faces.build(),
            generic_family: self.generic_family,
        }
    }
}

/// Inspect data for a `FontFamily`.
#[derive(Debug)]
pub struct FamilyInspectData {
    node: finspect::Node,

    aliases_node: finspect::Node,
    aliases: Vec<AliasInspectData>,

    typefaces_node: finspect::Node,
    typefaces: Vec<TypefaceInspectData>,
}

impl FamilyInspectData {
    /// Creates a new `FamilyInspectData`.
    pub fn new(
        parent: &finspect::Node,
        family: &FontFamily,
        asset_location_lookup: &impl Fn(AssetId) -> Option<String>,
    ) -> Self {
        let node = parent.create_child(&family.name);

        let aliases_node = node.create_child("aliases");
        let aliases: Vec<AliasInspectData> = Vec::new();

        let typefaces_node = node.create_child("typefaces");
        let typefaces_count = family.faces.faces.len();
        let typefaces: Vec<TypefaceInspectData> = family
            .faces
            .faces
            .iter()
            .enumerate()
            .map(|(idx, typeface)| {
                TypefaceInspectData::new(
                    &typefaces_node,
                    &zero_pad(idx, typefaces_count),
                    typeface,
                    asset_location_lookup,
                )
            })
            .collect();

        FamilyInspectData { node, aliases_node, aliases, typefaces_node, typefaces }
    }

    /// Adds a new font family alias, optionally with query overrides, to the existing Inspect data.
    pub fn add_alias(&mut self, alias: &str, overrides: &Option<Arc<TypefaceQueryOverrides>>) {
        let alias_data = AliasInspectData::new(&self.aliases_node, alias, overrides);
        self.aliases.push(alias_data);
    }
}

/// Inspect data for a single font family alias.
#[derive(Debug)]
pub struct AliasInspectData {
    node: finspect::Node,
    style_overrides: Option<finspect::Node>,
    language_overrides: Option<finspect::StringProperty>,
}

impl AliasInspectData {
    /// Creates a new `AliasInspectData`.
    fn new(
        parent_inspect_node: &finspect::Node,
        alias: &str,
        overrides: &Option<Arc<TypefaceQueryOverrides>>,
    ) -> Self {
        let node = parent_inspect_node.create_child(alias);

        let style_overrides = overrides.as_ref().and_then(|overrides| {
            if overrides.has_style_overrides() {
                let style = node.create_child("style_overrides");
                if let Some(slant) = overrides.style.slant {
                    style.record_string("slant", format!("{:?}", slant).to_kebab_case());
                }
                if let Some(weight) = overrides.style.weight {
                    style.record_uint("weight", weight.into());
                }
                if let Some(width) = overrides.style.width {
                    style.record_string("width", format!("{:?}", width).to_kebab_case());
                }
                Some(style)
            } else {
                None
            }
        });

        let language_overrides = overrides.as_ref().and_then(|overrides| {
            if overrides.has_language_overrides() {
                Some(
                    node.create_string("language_overrides", overrides.languages.iter().join(", ")),
                )
            } else {
                None
            }
        });

        AliasInspectData { node, style_overrides, language_overrides }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        anyhow::Error,
        char_collection::char_collect,
        fidl_fuchsia_fonts::{Slant, Width},
        finspect::assert_inspect_tree,
        maplit::{btreemap, btreeset},
        pretty_assertions::assert_eq,
    };

    #[test]
    fn test_aliases_from_family() -> Result<(), Error> {
        let family = v2::Family {
            name: "Family A".to_string(),
            aliases: vec![
                v2::FontFamilyAliasSet::without_overrides(vec!["A Family", "Family Ay"])?,
                v2::FontFamilyAliasSet::new(
                    vec!["Family A Condensed", "Family A Squished"],
                    StyleOptions { width: Some(Width::Condensed), ..Default::default() },
                    std::iter::empty::<String>(),
                )?,
            ],
            generic_family: None,
            assets: vec![],
        };

        let expected = btreemap! {
            UniCase::new("FamilyA".to_string()) =>
                FamilyOrAlias::Alias(UniCase::new("Family A".to_string()), None),
            UniCase::new("A Family".to_string()) =>
                FamilyOrAlias::Alias(UniCase::new("Family A".to_string()), None),
            UniCase::new("AFamily".to_string()) =>
                FamilyOrAlias::Alias(UniCase::new("Family A".to_string()), None),
            UniCase::new("Family A Condensed".to_string()) =>
                FamilyOrAlias::Alias(
                    UniCase::new("Family A".to_string()),
                    Some(Arc::new(
                            TypefaceQueryOverrides {
                                style: StyleOptions {
                                    width: Some(Width::Condensed),
                                    ..Default::default()
                                },
                                languages: vec![]
                            }
                        ))
                ),
            UniCase::new("FamilyACondensed".to_string()) =>
                FamilyOrAlias::Alias(
                    UniCase::new("Family A".to_string()),
                    Some(Arc::new(
                            TypefaceQueryOverrides {
                                style: StyleOptions {
                                    width: Some(Width::Condensed),
                                    ..Default::default()
                                },
                                languages: vec![]
                            }
                        ))
                ),
            UniCase::new("Family A Squished".to_string()) =>
                FamilyOrAlias::Alias(
                    UniCase::new("Family A".to_string()),
                    Some(Arc::new(
                            TypefaceQueryOverrides {
                                style: StyleOptions {
                                    width: Some(Width::Condensed),
                                    ..Default::default()
                                },
                                languages: vec![]
                            }
                        ))
                ),
            UniCase::new("FamilyASquished".to_string()) =>
                FamilyOrAlias::Alias(
                    UniCase::new("Family A".to_string()),
                    Some(Arc::new(
                            TypefaceQueryOverrides {
                                style: StyleOptions {
                                    width: Some(Width::Condensed),
                                    ..Default::default()
                                },
                                languages: vec![]
                            }
                        ))
                ),
            UniCase::new("Family Ay".to_string()) =>
                FamilyOrAlias::Alias(UniCase::new("Family A".to_string()), None),
            UniCase::new("FamilyAy".to_string()) =>
                FamilyOrAlias::Alias(UniCase::new("Family A".to_string()), None),
        };

        let actual: BTreeMap<UniCase<String>, FamilyOrAlias> =
            FamilyOrAliasBuilder::aliases_from_family(&family)
                .into_iter()
                .map(|(key, val)| (key, val.build()))
                .collect();

        assert_eq!(expected, actual);

        Ok(())
    }

    #[test]
    fn test_alias_inspect_data() {
        let alias = "Alif";
        let overrides = Some(Arc::new(TypefaceQueryOverrides {
            style: StyleOptions {
                slant: None,
                weight: Some(600),
                width: Some(Width::ExtraExpanded),
            },
            languages: vec!["ar-EG".to_string(), "ar-MA".to_string()],
        }));

        let inspector = finspect::Inspector::new();
        let _inspect_data = AliasInspectData::new(inspector.root(), alias, &overrides);
        assert_inspect_tree!(inspector, root: {
            Alif: {
                style_overrides: {
                    weight: 600u64,
                    width: "extra-expanded"
                },
                language_overrides: "ar-EG, ar-MA"
            }
        });
    }

    #[test]
    fn test_family_inspect_data() {
        let mut family = FontFamilyBuilder::new("Alpha", Some(GenericFontFamily::Cursive));
        family.add_typeface_once(Arc::new(Typeface {
            asset_id: AssetId(0),
            font_index: 0,
            slant: Slant::Upright,
            weight: 400,
            width: Width::Normal,
            languages: btreeset!("ar-EG".to_string(), "ar-MA".to_string()),
            char_set: char_collect!(0x0..=0xEE).into(),
            generic_family: Some(GenericFontFamily::Cursive),
        }));
        family.add_typeface_once(Arc::new(Typeface {
            asset_id: AssetId(1),
            font_index: 0,
            slant: Slant::Upright,
            weight: 400,
            width: Width::UltraCondensed,
            languages: btreeset!("und-Latn".to_string()),
            char_set: char_collect!(0x0..=0xEE).into(),
            generic_family: Some(GenericFontFamily::Cursive),
        }));
        let family = family.build();

        let asset_location_lookup = |asset_id| match asset_id {
            AssetId(0) => Some("/path/to/asset-0.ttf".to_string()),
            AssetId(1) => Some("/path/to/asset-1.ttf".to_string()),
            _ => unreachable!(),
        };

        let inspector = finspect::Inspector::new();
        let mut inspect_data =
            FamilyInspectData::new(inspector.root(), &family, &asset_location_lookup);
        inspect_data.add_alias(
            "Alif",
            &Some(Arc::new(TypefaceQueryOverrides {
                style: StyleOptions::default(),
                languages: vec!["ar-EG".to_string(), "ar-JO".to_string()],
            })),
        );
        inspect_data.add_alias("Ay", &None);

        // The contents of `AliasInspectData` and `TypefaceInspectData` are tested in their
        // respective unit tests. This test just asserts the overall structure.
        assert_inspect_tree!(inspector, root: {
            Alpha: {
                aliases: {
                    Ay: contains {},
                    Alif: contains {},
                },
                typefaces: {
                    "0": contains {
                        asset_location: "/path/to/asset-0.ttf",
                    },
                    "1": contains {
                        asset_location: "/path/to/asset-1.ttf",
                    },
                }
            }
        });
    }
}
