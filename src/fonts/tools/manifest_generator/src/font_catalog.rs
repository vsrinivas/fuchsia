// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Deserialization for `.font_catalog.json` files.

use {
    crate::{
        merge::{TryMerge, TryMergeGroups},
        serde_ext::{self, LoadError},
    },
    failure::Error,
    fidl_fuchsia_fonts::GenericFontFamily,
    manifest::{
        serde_ext::*,
        v2::{FontFamilyAlias, Style},
    },
    rayon::prelude::*,
    serde_derive::Deserialize,
    std::{
        collections::BTreeSet,
        path::{Path, PathBuf},
    },
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
    pub aliases: BTreeSet<FontFamilyAlias>,
    #[serde(with = "OptGenericFontFamily", default)] // Default to `None`
    pub generic_family: Option<GenericFontFamily>,
    pub fallback: bool,
    pub assets: Vec<Asset>,
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

    fn try_merge_group(group: Vec<Self>) -> Result<Self, Error> {
        let name = (&group[0].name).to_string();
        let generic_family = (&group[0]).generic_family;
        let fallback = (&group[0]).fallback;
        let aliases = group.iter().flat_map(|family| family.aliases.iter()).cloned().collect();

        let assets = group.into_iter().flat_map(|family| family.assets).try_merge_groups()?;

        Ok(Family { name, aliases, generic_family, fallback, assets })
    }
}

#[derive(Clone, Debug, Eq, PartialEq, Hash, Deserialize)]
pub(crate) struct Asset {
    pub file_name: String,
    pub typefaces: Vec<Typeface>,
}

/// `Asset`s with the same `file_name`s are expected to be identical.
///
/// Notably, we do not attempt to merge lists of `Typeface`s.
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
        fidl_fuchsia_fonts::{GenericFontFamily, Slant, Width, WEIGHT_NORMAL},
        pretty_assertions::assert_eq,
    };

    #[test]
    fn test_try_merge_assets() -> Result<(), Error> {
        let assets = vec![
            Asset {
                file_name: "FamilyA.ttf".to_string(),
                typefaces: vec![Typeface {
                    index: 0,
                    languages: vec!["en".to_string()],
                    style: Style {
                        width: Width::Condensed,
                        weight: WEIGHT_NORMAL,
                        slant: Slant::Upright,
                    },
                }],
            },
            // Duplicate
            Asset {
                file_name: "FamilyA.ttf".to_string(),
                typefaces: vec![Typeface {
                    index: 0,
                    languages: vec!["en".to_string()],
                    style: Style {
                        width: Width::Condensed,
                        weight: WEIGHT_NORMAL,
                        slant: Slant::Upright,
                    },
                }],
            },
            Asset {
                file_name: "FamilyA-1.ttf".to_string(),
                typefaces: vec![
                    Typeface {
                        index: 0,
                        languages: vec!["en".to_string()],
                        style: Style {
                            width: Width::Condensed,
                            weight: WEIGHT_NORMAL,
                            slant: Slant::Upright,
                        },
                    },
                    Typeface {
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
                typefaces: vec![
                    Typeface {
                        index: 0,
                        languages: vec!["en".to_string()],
                        style: Style {
                            width: Width::Condensed,
                            weight: WEIGHT_NORMAL,
                            slant: Slant::Upright,
                        },
                    },
                    Typeface {
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
                typefaces: vec![Typeface {
                    index: 0,
                    languages: vec!["en".to_string()],
                    style: Style {
                        width: Width::Condensed,
                        weight: WEIGHT_NORMAL,
                        slant: Slant::Upright,
                    },
                }],
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
                typefaces: vec![Typeface {
                    index: 0,
                    languages: vec!["en".to_string()],
                    style: Style {
                        width: Width::Condensed,
                        weight: WEIGHT_NORMAL,
                        slant: Slant::Upright,
                    },
                }],
            },
            // Duplicate with collision
            Asset {
                file_name: "FamilyA.ttf".to_string(),
                typefaces: vec![Typeface {
                    index: 0,
                    languages: vec!["ru".to_string()],
                    style: Style {
                        width: Width::Condensed,
                        weight: WEIGHT_NORMAL,
                        slant: Slant::Upright,
                    },
                }],
            },
            Asset {
                file_name: "FamilyA-1.ttf".to_string(),
                typefaces: vec![
                    Typeface {
                        index: 0,
                        languages: vec!["en".to_string()],
                        style: Style {
                            width: Width::Condensed,
                            weight: WEIGHT_NORMAL,
                            slant: Slant::Upright,
                        },
                    },
                    Typeface {
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
                        FontFamilyAlias::new("Family Ay"),
                        FontFamilyAlias::new("A Family"),
                    ]
                    .into_iter()
                    .collect(),
                    generic_family: Some(GenericFontFamily::Serif),
                    fallback: true,
                    assets: vec![
                        Asset {
                            file_name: "FamilyA.ttf".to_string(),
                            typefaces: vec![Typeface {
                                index: 0,
                                languages: vec!["en".to_string()],
                                style: Style {
                                    width: Width::Condensed,
                                    weight: WEIGHT_NORMAL,
                                    slant: Slant::Upright,
                                },
                            }],
                        },
                        Asset {
                            file_name: "FamilyA-1.ttf".to_string(),
                            typefaces: vec![
                                Typeface {
                                    index: 0,
                                    languages: vec!["en".to_string()],
                                    style: Style {
                                        width: Width::Condensed,
                                        weight: WEIGHT_NORMAL,
                                        slant: Slant::Upright,
                                    },
                                },
                                Typeface {
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
                            FontFamilyAlias::new("Family Ayyyy"),
                            FontFamilyAlias::new("FamilyA"),
                            FontFamilyAlias::new("a family"),
                        ]
                        .into_iter()
                        .collect(),
                        generic_family: Some(GenericFontFamily::Serif),
                        fallback: true,
                        assets: vec![
                            Asset {
                                file_name: "FamilyA.ttf".to_string(),
                                typefaces: vec![Typeface {
                                    index: 0,
                                    languages: vec!["en".to_string()],
                                    style: Style {
                                        width: Width::Condensed,
                                        weight: WEIGHT_NORMAL,
                                        slant: Slant::Upright,
                                    },
                                }],
                            },
                            Asset {
                                file_name: "FamilyA-2.ttf".to_string(),
                                typefaces: vec![Typeface {
                                    index: 0,
                                    languages: vec!["en".to_string()],
                                    style: Style {
                                        width: Width::Condensed,
                                        weight: WEIGHT_NORMAL,
                                        slant: Slant::Upright,
                                    },
                                }],
                            },
                        ],
                    },
                    Family {
                        name: "Family B".to_string(),
                        aliases: vec![
                            FontFamilyAlias::new("FamilyB"),
                            FontFamilyAlias::new("BFamily"),
                        ]
                        .into_iter()
                        .collect(),
                        generic_family: Some(GenericFontFamily::Serif),
                        fallback: true,
                        assets: vec![Asset {
                            file_name: "FamilyB.ttf".to_string(),
                            typefaces: vec![Typeface {
                                index: 0,
                                languages: vec!["en".to_string()],
                                style: Style {
                                    width: Width::Condensed,
                                    weight: WEIGHT_NORMAL,
                                    slant: Slant::Upright,
                                },
                            }],
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
                        FontFamilyAlias::new("A Family"),
                        FontFamilyAlias::new("Family Ay"),
                        FontFamilyAlias::new("Family Ayyyy"),
                        FontFamilyAlias::new("FamilyA"),
                    ]
                    .into_iter()
                    .collect(),
                    generic_family: Some(GenericFontFamily::Serif),
                    fallback: true,
                    assets: vec![
                        Asset {
                            file_name: "FamilyA-1.ttf".to_string(),
                            typefaces: vec![
                                Typeface {
                                    index: 0,
                                    languages: vec!["en".to_string()],
                                    style: Style {
                                        width: Width::Condensed,
                                        weight: WEIGHT_NORMAL,
                                        slant: Slant::Upright,
                                    },
                                },
                                Typeface {
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
                            typefaces: vec![Typeface {
                                index: 0,
                                languages: vec!["en".to_string()],
                                style: Style {
                                    width: Width::Condensed,
                                    weight: WEIGHT_NORMAL,
                                    slant: Slant::Upright,
                                },
                            }],
                        },
                        Asset {
                            file_name: "FamilyA.ttf".to_string(),
                            typefaces: vec![Typeface {
                                index: 0,
                                languages: vec!["en".to_string()],
                                style: Style {
                                    width: Width::Condensed,
                                    weight: WEIGHT_NORMAL,
                                    slant: Slant::Upright,
                                },
                            }],
                        },
                    ],
                },
                Family {
                    name: "Family B".to_string(),
                    aliases: vec![FontFamilyAlias::new("BFamily"), FontFamilyAlias::new("FamilyB")]
                        .into_iter()
                        .collect(),
                    generic_family: Some(GenericFontFamily::Serif),
                    fallback: true,
                    assets: vec![Asset {
                        file_name: "FamilyB.ttf".to_string(),
                        typefaces: vec![Typeface {
                            index: 0,
                            languages: vec!["en".to_string()],
                            style: Style {
                                width: Width::Condensed,
                                weight: WEIGHT_NORMAL,
                                slant: Slant::Upright,
                            },
                        }],
                    }],
                },
            ],
        };

        let actual = FontCatalog::try_merge(catalogs)?;
        assert_eq!(actual, expected);

        Ok(())
    }
}
