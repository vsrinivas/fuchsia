// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Deserialization for `.font_catalog.json` files.

use {
    crate::{
        merge::{MergeError, TryMerge, TryMergeGroups},
        serde_ext::{self, LoadError},
    },
    anyhow::Error,
    fidl_fuchsia_fonts::GenericFontFamily,
    itertools::Itertools,
    manifest::{
        serde_ext::*,
        v2::{FontFamilyAliasSet, Style},
    },
    rayon::prelude::*,
    serde::de::{Deserialize, Deserializer},
    serde_derive::Deserialize,
    std::{
        collections::{BTreeMap, HashSet},
        path::{Path, PathBuf},
    },
    unicase::UniCase,
};

/// Possible versions of [FontCatalog].
#[derive(Clone, Debug, PartialEq, Eq, Deserialize)]
#[serde(tag = "version")]
enum FontCatalogWrapper {
    #[serde(rename = "1")]
    Version1(FontCatalog),
}

/// A human-defined catalog of fonts that exist in a particular CIPD repo.
#[derive(Clone, Debug, PartialEq, Eq, Deserialize)]
pub(crate) struct FontCatalog {
    pub families: Vec<Family>,
}

/// Index into the `families` table.
#[derive(Copy, Clone, Debug, Eq, PartialEq, Ord, PartialOrd, Hash)]
pub(crate) struct FamilyIndex(pub usize);

/// Index into a [Family]'s `assets` field.
#[derive(Copy, Clone, Debug, Eq, PartialEq, Ord, PartialOrd, Hash)]
pub(crate) struct AssetInFamilyIndex(pub usize);

/// A [Typeface]'s index inside an [Asset]. Note that an [Asset]'s `typefaces`'s indices might not
/// start at zero and can have discontinuities.
#[derive(Copy, Clone, Debug, Eq, PartialEq, Ord, PartialOrd, Hash)]
pub(crate) struct TypefaceInAssetIndex(pub u32);

impl FontCatalog {
    /// Loads and merges multiple catalogs.
    pub fn load_from_paths<T, P>(paths: T) -> Result<Self, Error>
    where
        T: IntoIterator<Item = P>,
        P: AsRef<Path>,
    {
        let paths: Vec<PathBuf> =
            paths.into_iter().map(|path_ref| path_ref.as_ref().into()).collect();
        let catalogs: Result<Vec<Self>, _> =
            paths.par_iter().map(|path| Self::load_from_path(path)).collect();
        Self::try_merge(catalogs?)
    }

    /// Loads a single catalog.
    pub fn load_from_path<T: AsRef<Path>>(path: T) -> Result<Self, LoadError> {
        match serde_ext::load_from_path(path) {
            Ok(FontCatalogWrapper::Version1(catalog)) => Ok(catalog),
            Err(err) => Err(err),
        }
    }

    /// Tries to merge multiple catalogs into one.
    pub fn try_merge<T>(catalogs: T) -> Result<FontCatalog, Error>
    where
        T: IntoIterator<Item = Self>,
    {
        let families: Vec<Family> =
            catalogs.into_iter().flat_map(|catalog| catalog.families).try_merge_groups()?;

        Ok(FontCatalog { families })
    }
}

#[derive(Clone, Debug, Eq, PartialEq, Hash, Deserialize)]
pub(crate) struct Family {
    pub name: String,
    #[serde(default)]
    pub aliases: Vec<FontFamilyAliasSet>,
    #[serde(with = "OptGenericFontFamily", default)] // Default to `None`
    pub generic_family: Option<GenericFontFamily>,
    pub fallback: bool,
    pub assets: Vec<Asset>,
}

impl Family {
    pub fn get_asset(&self, asset_index: AssetInFamilyIndex) -> Option<&Asset> {
        self.assets.get(asset_index.0)
    }
}

/// We merge Families with the same name by combining and deduplicating their aliases and assets.
impl TryMerge for Family {
    type Key = String;

    fn key(&self) -> Self::Key {
        self.name.clone()
    }

    fn has_matching_fields(&self, other: &Self) -> bool {
        self.generic_family == other.generic_family && self.fallback == other.fallback
    }

    fn try_merge_group(mut group: Vec<Self>) -> Result<Self, Error> {
        let name = (&group[0].name).to_string();
        let generic_family = (&group[0]).generic_family;
        let fallback = (&group[0]).fallback;

        let aliases = group
            .iter_mut()
            // Move `aliases` out
            .flat_map(|family| std::mem::replace(&mut family.aliases, Default::default()))
            .try_merge_groups()?
            .into_iter()
            .map(|alias_set: FontFamilyAliasSet| alias_set.into())
            .collect();

        let assets = group.into_iter().flat_map(|family| family.assets).try_merge_groups()?;

        Ok(Family { name, aliases, generic_family, fallback, assets })
    }
}

impl TryMerge for FontFamilyAliasSet {
    type Key = (StyleOptions, Vec<String>);

    fn key(&self) -> Self::Key {
        (self.style_overrides().clone(), self.language_overrides().cloned().collect_vec())
    }

    fn has_matching_fields(&self, _other: &Self) -> bool {
        // All of the fields we want to match are already part of the `Key`, so this is trivially
        // true.
        true
    }

    fn try_merge_group(group: Vec<Self>) -> Result<Self, Error> {
        let names = group
            .iter()
            .flat_map(|set| set.names())
            .map(|name| UniCase::new(name))
            .sorted()
            .unique()
            .collect_vec();
        FontFamilyAliasSet::new(
            names,
            group[0].style_overrides().clone(),
            group[0].language_overrides().cloned().collect_vec(),
        )
    }

    /// Ensure that every alias `name` is unique among all the `FontFamilyAliasSet`s.
    fn post_validate(groups: Vec<Self>) -> Result<Vec<Self>, MergeError<Self>> {
        let mut unique = HashSet::new();
        let first_duplicate =
            groups.iter().flat_map(|group| group.names()).find(|name| !unique.insert(name.clone()));
        match first_duplicate {
            Some(name) => {
                Err(MergeError::PostInvalid(format!("{:?} appeared more than once", name), groups))
            }
            None => Ok(groups),
        }
    }
}

#[derive(Clone, Debug, Eq, PartialEq, Hash, Deserialize)]
pub(crate) struct Asset {
    pub file_name: String,
    #[serde(deserialize_with = "Asset::deserialize_typefaces")]
    pub typefaces: BTreeMap<TypefaceInAssetIndex, Typeface>,
}

impl Asset {
    /// Deserialize an array of [`Typeface`]s into an indexed map.
    fn deserialize_typefaces<'de, D>(
        deserializer: D,
    ) -> Result<BTreeMap<TypefaceInAssetIndex, Typeface>, D::Error>
    where
        D: Deserializer<'de>,
    {
        let typefaces: Vec<Typeface> = Vec::deserialize(deserializer)?;
        let mut map = BTreeMap::new();
        for typeface in typefaces {
            map.insert(TypefaceInAssetIndex(typeface.index), typeface);
        }
        Ok(map)
    }
}

/// `Asset`s with the same `file_name`s are expected to be identical within a given [`Family`].
///
/// Notably, we do not attempt to merge lists of `Typeface`s.
///
/// (On the other hand, a single font file might contain typefaces from different font families.
/// In this case, the different `Family` structs would have `Asset`s with the same `file_name` but
/// different subsets of the `typeface` array.)
impl TryMerge for Asset {
    type Key = String;

    fn key(&self) -> Self::Key {
        self.file_name.clone()
    }

    fn try_merge_group(mut group: Vec<Self>) -> Result<Self, Error> {
        // Just take the last one.
        Ok(group.pop().unwrap())
    }
}

#[derive(Clone, Debug, Eq, PartialEq, Hash, Deserialize)]
pub(crate) struct Typeface {
    #[serde(default)]
    pub index: u32,
    #[serde(default)]
    pub languages: Vec<String>,
    #[serde(flatten)]
    pub style: Style,
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl_fuchsia_fonts::{GenericFontFamily, Slant, Style2 as FidlStyle, Width, WEIGHT_NORMAL},
        maplit::btreemap,
        pretty_assertions::assert_eq,
        std::iter,
    };

    #[test]
    fn test_try_merge_aliases() -> Result<(), Error> {
        let aliases: Vec<FontFamilyAliasSet> = vec![
            FontFamilyAliasSet::without_overrides(vec!["Abc"])?,
            FontFamilyAliasSet::without_overrides(vec!["Def"])?,
            // Duplicate
            FontFamilyAliasSet::without_overrides(vec!["Abc"])?,
            FontFamilyAliasSet::new(
                vec!["Abc Condensed", "Abc Squished"],
                FidlStyle { slant: None, weight: None, width: Some(Width::Condensed) },
                iter::empty::<String>(),
            )?,
            // Duplicate
            FontFamilyAliasSet::new(
                vec!["Abc Condensed", "Condensed Abc"],
                FidlStyle { slant: None, weight: None, width: Some(Width::Condensed) },
                iter::empty::<String>(),
            )?,
        ];

        let expected = vec![
            FontFamilyAliasSet::without_overrides(vec!["Abc", "Def"])?,
            FontFamilyAliasSet::new(
                vec!["Abc Condensed", "Abc Squished", "Condensed Abc"],
                StyleOptions { width: Some(Width::Condensed), ..Default::default() },
                iter::empty::<String>(),
            )?,
        ];

        let actual = aliases.into_iter().try_merge_groups()?;
        assert_eq!(actual, expected);

        Ok(())
    }

    #[test]
    fn test_try_merge_aliases_collision() -> Result<(), Error> {
        let aliases: Vec<FontFamilyAliasSet> = vec![
            FontFamilyAliasSet::without_overrides(vec!["Abc"])?,
            FontFamilyAliasSet::without_overrides(vec!["Def"])?,
            FontFamilyAliasSet::new(
                vec!["Def"],
                FidlStyle { slant: None, weight: None, width: None },
                vec!["en", "es"],
            )?,
        ];

        let actual = aliases.into_iter().try_merge_groups();
        println!("{:#?}", actual);
        assert!(actual.is_err());
        Ok(())
    }

    #[test]
    fn test_try_merge_assets() -> Result<(), Error> {
        let assets = vec![
            Asset {
                file_name: "FamilyA.ttf".to_string(),
                typefaces: btreemap![
                    TypefaceInAssetIndex(0) => Typeface {
                        index: 0,
                        languages: vec!["en".to_string()],
                        style: Style {
                            width: Width::Condensed,
                            weight: WEIGHT_NORMAL,
                            slant: Slant::Upright,
                        },
                    }
                ],
            },
            // Duplicate
            Asset {
                file_name: "FamilyA.ttf".to_string(),
                typefaces: btreemap![
                    TypefaceInAssetIndex(0) => Typeface {
                        index: 0,
                        languages: vec!["en".to_string()],
                        style: Style {
                            width: Width::Condensed,
                            weight: WEIGHT_NORMAL,
                            slant: Slant::Upright,
                        },
                    }
                ],
            },
            Asset {
                file_name: "FamilyA-1.ttf".to_string(),
                typefaces: btreemap![
                    TypefaceInAssetIndex(0) => Typeface {
                        index: 0,
                        languages: vec!["en".to_string()],
                        style: Style {
                            width: Width::Condensed,
                            weight: WEIGHT_NORMAL,
                            slant: Slant::Upright,
                        },
                    },
                    TypefaceInAssetIndex(1) => Typeface {
                        index: 1,
                        languages: vec!["he".to_string()],
                        style: Style {
                            width: Width::Condensed,
                            weight: WEIGHT_NORMAL,
                            slant: Slant::Upright,
                        },
                    },
                ],
            },
        ];

        let expected = vec![
            Asset {
                file_name: "FamilyA-1.ttf".to_string(),
                typefaces: btreemap![
                    TypefaceInAssetIndex(0) => Typeface {
                        index: 0,
                        languages: vec!["en".to_string()],
                        style: Style {
                            width: Width::Condensed,
                            weight: WEIGHT_NORMAL,
                            slant: Slant::Upright,
                        },
                    },
                    TypefaceInAssetIndex(1) => Typeface {
                        index: 1,
                        languages: vec!["he".to_string()],
                        style: Style {
                            width: Width::Condensed,
                            weight: WEIGHT_NORMAL,
                            slant: Slant::Upright,
                        },
                    },
                ],
            },
            Asset {
                file_name: "FamilyA.ttf".to_string(),
                typefaces: btreemap![
                    TypefaceInAssetIndex(0) => Typeface {
                        index: 0,
                        languages: vec!["en".to_string()],
                        style: Style {
                            width: Width::Condensed,
                            weight: WEIGHT_NORMAL,
                            slant: Slant::Upright,
                        },
                    }
                ],
            },
        ];

        let actual = assets.into_iter().try_merge_groups()?;

        assert_eq!(actual, expected);

        Ok(())
    }

    #[test]
    fn test_try_merge_assets_collision() -> Result<(), Error> {
        let assets = vec![
            Asset {
                file_name: "FamilyA.ttf".to_string(),
                typefaces: btreemap![
                    TypefaceInAssetIndex(0) => Typeface {
                        index: 0,
                        languages: vec!["en".to_string()],
                        style: Style {
                            width: Width::Condensed,
                            weight: WEIGHT_NORMAL,
                            slant: Slant::Upright,
                        },
                    }
                ],
            },
            // Duplicate with collision
            Asset {
                file_name: "FamilyA.ttf".to_string(),
                typefaces: btreemap![
                    TypefaceInAssetIndex(0) => Typeface {
                        index: 0,
                        languages: vec!["ru".to_string()],
                        style: Style {
                            width: Width::Condensed,
                            weight: WEIGHT_NORMAL,
                            slant: Slant::Upright,
                        },
                    }
                ],
            },
            Asset {
                file_name: "FamilyA-1.ttf".to_string(),
                typefaces: btreemap![
                    TypefaceInAssetIndex(0) => Typeface {
                        index: 0,
                        languages: vec!["en".to_string()],
                        style: Style {
                            width: Width::Condensed,
                            weight: WEIGHT_NORMAL,
                            slant: Slant::Upright,
                        },
                    },
                    TypefaceInAssetIndex(1) => Typeface {
                        index: 1,
                        languages: vec!["he".to_string()],
                        style: Style {
                            width: Width::Condensed,
                            weight: WEIGHT_NORMAL,
                            slant: Slant::Upright,
                        },
                    },
                ],
            },
        ];

        let actual = assets.into_iter().try_merge_groups();
        assert!(actual.is_err());

        Ok(())
    }

    #[test]
    fn test_merge_catalogs() -> Result<(), Error> {
        let catalogs = vec![
            FontCatalog {
                families: vec![Family {
                    name: "Family A".to_string(),
                    aliases: vec![
                        FontFamilyAliasSet::without_overrides(vec!["Family Ay", "A Family"])?,
                        FontFamilyAliasSet::new(
                            vec!["Family A Condensed"],
                            StyleOptions { width: Some(Width::Condensed), ..Default::default() },
                            iter::empty::<String>(),
                        )?,
                    ]
                    .into_iter()
                    .collect(),
                    generic_family: Some(GenericFontFamily::Serif),
                    fallback: true,
                    assets: vec![
                        Asset {
                            file_name: "FamilyA.ttf".to_string(),
                            typefaces: btreemap![
                                TypefaceInAssetIndex(0) => Typeface {
                                    index: 0,
                                    languages: vec!["en".to_string()],
                                    style: Style {
                                        width: Width::Condensed,
                                        weight: WEIGHT_NORMAL,
                                        slant: Slant::Upright,
                                    },
                                }
                            ],
                        },
                        Asset {
                            file_name: "FamilyA-1.ttf".to_string(),
                            typefaces: btreemap![
                                TypefaceInAssetIndex(0) => Typeface {
                                    index: 0,
                                    languages: vec!["en".to_string()],
                                    style: Style {
                                        width: Width::Condensed,
                                        weight: WEIGHT_NORMAL,
                                        slant: Slant::Upright,
                                    },
                                },
                                TypefaceInAssetIndex(1) => Typeface {
                                    index: 1,
                                    languages: vec!["he".to_string()],
                                    style: Style {
                                        width: Width::Condensed,
                                        weight: WEIGHT_NORMAL,
                                        slant: Slant::Upright,
                                    },
                                },
                            ],
                        },
                    ],
                }],
            },
            FontCatalog {
                families: vec![
                    Family {
                        name: "Family A".to_string(),
                        aliases: vec![
                            FontFamilyAliasSet::without_overrides(vec![
                                "Family Ayyyy",
                                "FamilyA",
                                "a family",
                            ])?,
                            FontFamilyAliasSet::new(
                                vec!["Family A Squished"],
                                StyleOptions {
                                    width: Some(Width::Condensed),
                                    ..Default::default()
                                },
                                iter::empty::<String>(),
                            )?,
                        ]
                        .into_iter()
                        .collect(),
                        generic_family: Some(GenericFontFamily::Serif),
                        fallback: true,
                        assets: vec![
                            Asset {
                                file_name: "FamilyA.ttf".to_string(),
                                typefaces: btreemap![
                                    TypefaceInAssetIndex(0) =>Typeface {
                                        index: 0,
                                        languages: vec!["en".to_string()],
                                        style: Style {
                                            width: Width::Condensed,
                                            weight: WEIGHT_NORMAL,
                                            slant: Slant::Upright,
                                        },
                                    }
                                ],
                            },
                            Asset {
                                file_name: "FamilyA-2.ttf".to_string(),
                                typefaces: btreemap![
                                    TypefaceInAssetIndex(0) => Typeface {
                                        index: 0,
                                        languages: vec!["en".to_string()],
                                        style: Style {
                                            width: Width::Condensed,
                                            weight: WEIGHT_NORMAL,
                                            slant: Slant::Upright,
                                        },
                                    }
                                ],
                            },
                        ],
                    },
                    Family {
                        name: "Family B".to_string(),
                        aliases: vec![FontFamilyAliasSet::without_overrides(vec![
                            "FamilyB", "BFamily",
                        ])?]
                        .into_iter()
                        .collect(),
                        generic_family: Some(GenericFontFamily::Serif),
                        fallback: true,
                        assets: vec![Asset {
                            file_name: "FamilyB.ttf".to_string(),
                            typefaces: btreemap![
                                TypefaceInAssetIndex(0) =>Typeface {
                                    index: 0,
                                    languages: vec!["en".to_string()],
                                    style: Style {
                                        width: Width::Condensed,
                                        weight: WEIGHT_NORMAL,
                                        slant: Slant::Upright,
                                    },
                                }
                            ],
                        }],
                    },
                ],
            },
        ];

        let expected = FontCatalog {
            families: vec![
                Family {
                    name: "Family A".to_string(),
                    aliases: vec![
                        FontFamilyAliasSet::without_overrides(vec![
                            "A Family",
                            "Family Ay",
                            "Family Ayyyy",
                            "FamilyA",
                        ])?,
                        FontFamilyAliasSet::new(
                            vec!["Family A Condensed", "Family A Squished"],
                            StyleOptions { width: Some(Width::Condensed), ..Default::default() },
                            iter::empty::<String>(),
                        )?,
                    ]
                    .into_iter()
                    .collect(),
                    generic_family: Some(GenericFontFamily::Serif),
                    fallback: true,
                    assets: vec![
                        Asset {
                            file_name: "FamilyA-1.ttf".to_string(),
                            typefaces: btreemap![
                            TypefaceInAssetIndex(0) => Typeface {
                                    index: 0,
                                    languages: vec!["en".to_string()],
                                    style: Style {
                                        width: Width::Condensed,
                                        weight: WEIGHT_NORMAL,
                                        slant: Slant::Upright,
                                    },
                                },
                            TypefaceInAssetIndex(1) => Typeface {
                                    index: 1,
                                    languages: vec!["he".to_string()],
                                    style: Style {
                                        width: Width::Condensed,
                                        weight: WEIGHT_NORMAL,
                                        slant: Slant::Upright,
                                    },
                                },
                            ],
                        },
                        Asset {
                            file_name: "FamilyA-2.ttf".to_string(),
                            typefaces: btreemap![
                                TypefaceInAssetIndex(0) => Typeface {
                                    index: 0,
                                    languages: vec!["en".to_string()],
                                    style: Style {
                                        width: Width::Condensed,
                                        weight: WEIGHT_NORMAL,
                                        slant: Slant::Upright,
                                    },
                                }
                            ],
                        },
                        Asset {
                            file_name: "FamilyA.ttf".to_string(),
                            typefaces: btreemap![
                                TypefaceInAssetIndex(0) =>Typeface {
                                    index: 0,
                                    languages: vec!["en".to_string()],
                                    style: Style {
                                        width: Width::Condensed,
                                        weight: WEIGHT_NORMAL,
                                        slant: Slant::Upright,
                                    },
                                }
                            ],
                        },
                    ],
                },
                Family {
                    name: "Family B".to_string(),
                    aliases: vec![FontFamilyAliasSet::new(
                        vec!["BFamily", "FamilyB"],
                        StyleOptions::default(),
                        iter::empty::<String>(),
                    )?
                    .into()]
                    .into_iter()
                    .collect(),
                    generic_family: Some(GenericFontFamily::Serif),
                    fallback: true,
                    assets: vec![Asset {
                        file_name: "FamilyB.ttf".to_string(),
                        typefaces: btreemap![
                            TypefaceInAssetIndex(0) => Typeface {
                                index: 0,
                                languages: vec!["en".to_string()],
                                style: Style {
                                    width: Width::Condensed,
                                    weight: WEIGHT_NORMAL,
                                    slant: Slant::Upright,
                                },
                            }
                        ],
                    }],
                },
            ],
        };

        let actual = FontCatalog::try_merge(catalogs)?;
        assert_eq!(actual, expected);

        Ok(())
    }
}
