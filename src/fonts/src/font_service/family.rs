// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::typeface::{Collection as TypefaceCollection, TypefaceInfoAndCharSet},
    fidl_fuchsia_fonts::GenericFontFamily,
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
    /// *`Arc<TypefaceQueryOverrides` - Optional query overrides applied when a client requests the
    /// associated alias.
    Alias(UniCase<String>, Option<Arc<TypefaceQueryOverrides>>),
}

impl FamilyOrAlias {
    /// Generates a full set of aliases from a given manifest v2 `Family`, including alias variants
    /// without spaces.
    pub fn aliases_from_family(
        manifest_family: &v2::Family,
    ) -> BTreeMap<UniCase<String>, FamilyOrAlias> {
        let mut output: BTreeMap<UniCase<String>, FamilyOrAlias> = BTreeMap::new();
        let family_name = UniCase::new(manifest_family.name.clone());
        let family_name_without_spaces = UniCase::new(manifest_family.name.replace(" ", ""));
        if family_name_without_spaces != family_name {
            output.insert(
                family_name_without_spaces,
                FamilyOrAlias::Alias(family_name.clone(), None),
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
                    FamilyOrAlias::Alias(family_name.clone(), overrides.clone()),
                );

                let alias_name_without_spaces = UniCase::new((*alias_name).replace(" ", ""));
                if alias_name != alias_name_without_spaces {
                    output.insert(
                        alias_name_without_spaces,
                        FamilyOrAlias::Alias(family_name.clone(), overrides.clone()),
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
    pub fn new(name: String, generic_family: Option<GenericFontFamily>) -> FontFamily {
        FontFamily { name, faces: TypefaceCollection::new(), generic_family }
    }

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

#[cfg(test)]
mod tests {
    use {
        super::*, anyhow::Error, fidl_fuchsia_fonts::Width, maplit::btreemap,
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
            fallback: false,
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

        let actual = FamilyOrAlias::aliases_from_family(&family);

        assert_eq!(expected, actual);

        Ok(())
    }
}
