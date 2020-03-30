// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Utilities for conversion from Font Manifest v1 to v2.

use {
    crate::{v2, Family as FamilyV1, Font as FontV1, FontsManifest as FontsManifestV1},
    anyhow::{format_err, Error},
    itertools::Itertools,
    std::{
        convert::{From, TryFrom},
        path::{Path, PathBuf},
    },
};

impl TryFrom<FontsManifestV1> for v2::FontsManifest {
    type Error = Error;

    /// Converts a v1 [`manifest::FontsManifest`] to a v2 [`manifest::v2::Manifest`].
    ///
    /// This is purely an in-memory conversion, and does not load character sets for local files.
    fn try_from(old: FontsManifestV1) -> Result<v2::FontsManifest, Error> {
        // In v2, whether a font is a `fallback` is specified not per font family, but in an
        // explicit ordered list.
        // We capture each v1 family's `fallback` property for later use.
        let families_and_fallbacks: Result<Vec<(v2::Family, bool)>, _> = old
            .families
            .iter()
            .map(|v1_family| {
                v2::Family::try_from(v1_family).map(|v2_family| (v2_family, v1_family.fallback))
            })
            .collect();
        let families_and_fallbacks = families_and_fallbacks?;
        // For every v1 family that's `fallback: true`, for every asset, for every typeface, add the
        // typeface to the v2 `fallback_chain`.
        let fallback_chain: Vec<v2::TypefaceId> = families_and_fallbacks
            .iter()
            .filter(|(_, is_fallback)| *is_fallback)
            .flat_map(|(family, _)| {
                family.assets.iter().flat_map(|asset| {
                    asset.typefaces.iter().map(move |typeface| v2::TypefaceId {
                        file_name: asset.file_name.clone(),
                        index: typeface.index,
                    })
                })
            })
            .collect();
        let families = families_and_fallbacks.into_iter().map(|(family, _)| family).collect();
        Ok(v2::FontsManifest { families, fallback_chain, settings: v2::Settings::default() })
    }
}

impl TryFrom<&FamilyV1> for v2::Family {
    type Error = Error;

    /// Converts a v1 [`manifest::Family`] to a [`manifest::v2::Family`].
    ///
    /// Assumes that all v1 fonts are local files.
    fn try_from(old: &FamilyV1) -> Result<v2::Family, Error> {
        let assets: Result<Vec<v2::Asset>, _> = old
            .fonts
            .iter()
            .group_by(|font| &font.asset)
            .into_iter()
            .map(|(asset_path, font_group)| group_fonts_into_assets(asset_path, font_group))
            .collect();

        // v1 manifests only allow plain aliases, without any typeface property overrides.
        let aliases = match &old.aliases {
            None => vec![],
            Some(aliases) => vec![v2::FontFamilyAliasSet::without_overrides(aliases)?],
        };

        Ok(v2::Family {
            name: old.family.clone(),
            aliases,
            generic_family: old.generic_family.clone(),
            assets: assets?,
        })
    }
}

/// Groups v1 [`manifest::Font`]s that share a single path into a v2 [`manifest::v2::Asset`] with
/// one or more [`manifest::v2::Typeface`]s.
///
/// Params:
/// - `asset_path`: The path to the font file
/// - `font_group`: Iterator for all the v1 `Font`s that share the same `asset_path`
fn group_fonts_into_assets<'a>(
    asset_path: &PathBuf,
    font_group: impl Iterator<Item = &'a FontV1>,
) -> Result<v2::Asset, Error> {
    // We unwrap PathBuf to_str conversions because we should only be reading valid Unicode-encoded
    // paths from JSON manifests.
    let file_name: String = asset_path
        .file_name()
        .ok_or_else(|| format_err!("Invalid path: {:?}", asset_path))?
        .to_str()
        .ok_or_else(|| format_err!("Invalid path: {:?}", asset_path))?
        .to_string();
    // If the file is in the package root, then the parent directory will be blank.
    let directory: PathBuf =
        asset_path.parent().map_or_else(|| PathBuf::from(""), Path::to_path_buf);
    // Assuming that all v1 fonts are local files.
    Ok(v2::Asset {
        file_name,
        location: v2::AssetLocation::LocalFile(v2::LocalFileLocator { directory }),
        typefaces: font_group.map(font_to_typeface).collect(),
    })
}

/// Convert a v1 [`manifest::Font`] to a v2 [`v2::Typeface`].
fn font_to_typeface(font: &FontV1) -> v2::Typeface {
    v2::Typeface {
        index: font.index,
        languages: font.languages.clone(),
        style: v2::Style { slant: font.slant, weight: font.weight, width: font.width },
        code_points: font.code_points.clone(),
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        char_set::CharSet,
        fidl_fuchsia_fonts::{GenericFontFamily, Slant, Width},
    };

    #[test]
    fn test_v1_to_v2() -> Result<(), Error> {
        let old = FontsManifestV1 {
            families: vec![
                FamilyV1 {
                    family: "FamilyA".to_string(),
                    aliases: Some(vec!["Family A".to_string(), "A Family".to_string()]),
                    fonts: vec![
                        FontV1 {
                            asset: PathBuf::from("path/to/FamilyA-ExtraBold-Condensed.ttf"),
                            index: 0,
                            slant: Slant::Upright,
                            weight: 800,
                            width: Width::Condensed,
                            languages: vec!["en-US".to_string()],
                            package: None,
                            code_points: CharSet::new(vec![0x1, 0x2, 0x3, 0x7, 0x8, 0x9, 0x100]),
                        },
                        FontV1 {
                            asset: PathBuf::from("path/to/FamilyA-ExtraLight.ttf"),
                            index: 0,
                            slant: Slant::Upright,
                            weight: 200,
                            width: Width::Normal,
                            languages: vec!["en-US".to_string()],
                            package: None,
                            code_points: CharSet::new(vec![
                                0x11, 0x12, 0x13, 0x17, 0x18, 0x19, 0x100,
                            ]),
                        },
                    ],
                    fallback: true,
                    generic_family: Some(GenericFontFamily::SansSerif),
                },
                FamilyV1 {
                    family: "FamilyB".to_string(),
                    aliases: Some(vec!["Family B".to_string(), "B Family".to_string()]),
                    fonts: vec![
                        FontV1 {
                            asset: PathBuf::from("FamilyB.ttc"),
                            index: 0,
                            slant: Slant::Upright,
                            weight: 800,
                            width: Width::Condensed,
                            languages: vec!["en-US".to_string()],
                            package: None,
                            code_points: CharSet::new(vec![0x1, 0x2, 0x3, 0x7, 0x8, 0x9, 0x100]),
                        },
                        FontV1 {
                            asset: PathBuf::from("FamilyB.ttc"),
                            index: 1,
                            slant: Slant::Upright,
                            weight: 200,
                            width: Width::Normal,
                            languages: vec!["zh-Hant".to_string()],
                            package: None,
                            code_points: CharSet::new(vec![
                                0x11, 0x12, 0x13, 0x17, 0x18, 0x19, 0x100,
                            ]),
                        },
                    ],
                    fallback: false,
                    generic_family: None,
                },
            ],
        };

        let expected = v2::FontsManifest {
            families: vec![
                v2::Family {
                    name: "FamilyA".to_string(),
                    aliases: vec![v2::FontFamilyAliasSet::without_overrides(vec![
                        "Family A", "A Family",
                    ])?],
                    generic_family: Some(GenericFontFamily::SansSerif),
                    assets: vec![
                        v2::Asset {
                            file_name: "FamilyA-ExtraBold-Condensed.ttf".to_string(),
                            location: v2::AssetLocation::LocalFile(v2::LocalFileLocator {
                                directory: PathBuf::from("path/to"),
                            }),
                            typefaces: vec![v2::Typeface {
                                index: 0,
                                languages: vec!["en-US".to_string()],
                                style: v2::Style {
                                    slant: Slant::Upright,
                                    weight: 800,
                                    width: Width::Condensed,
                                },
                                code_points: CharSet::new(vec![
                                    0x1, 0x2, 0x3, 0x7, 0x8, 0x9, 0x100,
                                ]),
                            }],
                        },
                        v2::Asset {
                            file_name: "FamilyA-ExtraLight.ttf".to_string(),
                            location: v2::AssetLocation::LocalFile(v2::LocalFileLocator {
                                directory: PathBuf::from("path/to"),
                            }),
                            typefaces: vec![v2::Typeface {
                                index: 0,
                                languages: vec!["en-US".to_string()],
                                style: v2::Style {
                                    slant: Slant::Upright,
                                    weight: 200,
                                    width: Width::Normal,
                                },
                                code_points: CharSet::new(vec![
                                    0x11, 0x12, 0x13, 0x17, 0x18, 0x19, 0x100,
                                ]),
                            }],
                        },
                    ],
                },
                v2::Family {
                    name: "FamilyB".to_string(),
                    aliases: vec![v2::FontFamilyAliasSet::without_overrides(vec![
                        "Family B", "B Family",
                    ])?],
                    generic_family: None,
                    assets: vec![v2::Asset {
                        file_name: "FamilyB.ttc".to_string(),
                        location: v2::AssetLocation::LocalFile(v2::LocalFileLocator {
                            directory: PathBuf::from(""),
                        }),
                        typefaces: vec![
                            v2::Typeface {
                                index: 0,
                                languages: vec!["en-US".to_string()],
                                style: v2::Style {
                                    slant: Slant::Upright,
                                    weight: 800,
                                    width: Width::Condensed,
                                },
                                code_points: CharSet::new(vec![
                                    0x1, 0x2, 0x3, 0x7, 0x8, 0x9, 0x100,
                                ]),
                            },
                            v2::Typeface {
                                index: 1,
                                languages: vec!["zh-Hant".to_string()],
                                style: v2::Style {
                                    slant: Slant::Upright,
                                    weight: 200,
                                    width: Width::Normal,
                                },
                                code_points: CharSet::new(vec![
                                    0x11, 0x12, 0x13, 0x17, 0x18, 0x19, 0x100,
                                ]),
                            },
                        ],
                    }],
                },
            ],
            fallback_chain: vec![
                v2::TypefaceId {
                    file_name: "FamilyA-ExtraBold-Condensed.ttf".to_string(),
                    index: 0,
                },
                v2::TypefaceId { file_name: "FamilyA-ExtraLight.ttf".to_string(), index: 0 },
            ],
            settings: v2::Settings::default(),
        };

        assert_eq!(v2::FontsManifest::try_from(old)?, expected);
        Ok(())
    }
}
