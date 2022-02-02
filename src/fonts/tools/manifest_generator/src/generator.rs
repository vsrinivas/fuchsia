// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        font_catalog::TypefaceInAssetIndex, font_db::FontDb, FontCatalog, FontPackageListing,
        FontSets, ProductConfig,
    },
    anyhow::Error,
    font_info::FontInfoLoader,
    itertools::Itertools,
    manifest::{v2, FontManifestWrapper},
    std::{collections::BTreeMap, fmt, path::Path},
    thiserror::Error,
};

/// Builds a `FontDb` and then generates a manifest from all of the font metadata that has been
/// loaded.
pub fn generate_manifest(
    font_catalog: FontCatalog,
    font_pkgs: FontPackageListing,
    font_sets: FontSets,
    product_config: ProductConfig,
    font_info_loader: impl FontInfoLoader,
    font_dir: impl AsRef<Path>,
    verbose: bool,
) -> Result<FontManifestWrapper, Error> {
    let service_settings = product_config.settings.clone();

    let db = FontDb::new(
        font_catalog,
        font_pkgs,
        font_sets,
        product_config,
        font_info_loader,
        font_dir,
    )?;

    let mut errors: Vec<GeneratorError> = vec![];
    // For detecting name collisions
    let mut postscript_name_to_typeface = BTreeMap::new();
    let mut full_name_to_typeface = BTreeMap::new();

    let manifest = v2::FontsManifest {
        families: db
            .iter_families()
            .map(|catalog_family| v2::Family {
                name: catalog_family.name.clone(),
                aliases: catalog_family
                    .aliases
                    .iter()
                    .cloned()
                    .map(|string_or_alias_set| string_or_alias_set.into())
                    .collect(),
                generic_family: catalog_family.generic_family,
                assets: db
                    .iter_assets(catalog_family)
                    .map(|catalog_asset| v2::Asset {
                        file_name: catalog_asset.file_name.clone(),
                        location: db.get_asset_location(catalog_asset),
                        typefaces: catalog_asset
                            .typefaces
                            .values()
                            .map(|catalog_typeface| {
                                let typeface_metadata = db.get_typeface_metadata(
                                    catalog_asset,
                                    TypefaceInAssetIndex(catalog_typeface.index),
                                );

                                let file_name = catalog_asset.file_name.clone();
                                let face_index = catalog_typeface.index;
                                let postscript_name = typeface_metadata.postscript_name.clone();
                                let full_name = typeface_metadata.full_name.clone();

                                let manifest_typeface = v2::Typeface {
                                    index: catalog_typeface.index,
                                    languages: catalog_typeface.languages.clone(),
                                    style: catalog_typeface.style.clone(),
                                    code_points: typeface_metadata.code_points.clone(),
                                    postscript_name: Some(postscript_name.clone()),
                                    full_name: full_name.clone(),
                                };

                                let face_key = (file_name.clone(), face_index);
                                if let Some((prev_file_name, prev_face_index)) =
                                    postscript_name_to_typeface
                                        .insert(postscript_name.clone(), face_key.clone())
                                {
                                    errors.push(GeneratorError::DuplicatePostscriptName {
                                        postscript_name,
                                        file_name_1: prev_file_name,
                                        index_1: prev_face_index,
                                        file_name_2: file_name.clone(),
                                        index_2: face_index,
                                    })
                                }

                                // full_name is optional, but, if present, must be unique
                                if let Some(full_name) = full_name {
                                    if let Some((prev_file_name, prev_face_index)) =
                                        full_name_to_typeface
                                            .insert(full_name.clone(), face_key.clone())
                                    {
                                        errors.push(GeneratorError::DuplicateFullName {
                                            full_name,
                                            file_name_1: prev_file_name,
                                            index_1: prev_face_index,
                                            file_name_2: file_name,
                                            index_2: face_index,
                                        })
                                    }
                                }

                                manifest_typeface
                            })
                            .collect(),
                    })
                    .collect(),
            })
            .collect(),
        fallback_chain: db.iter_fallback_chain().collect(),
        settings: v2::Settings { cache_size_bytes: service_settings.cache_size_bytes },
    };

    if verbose {
        let non_fallback_typefaces: Vec<v2::TypefaceId> =
            db.iter_non_fallback_typefaces().sorted().collect();
        eprintln!("Non-fallback typefaces:\n{:#?}", &non_fallback_typefaces);
    }

    if errors.is_empty() {
        Ok(FontManifestWrapper::Version2(manifest))
    } else {
        Err(GeneratorErrors(errors.into()))?
    }
}

/// Error while generating the manifest (_after_ building the font database).
#[derive(Debug, Error)]
pub enum GeneratorError {
    #[error(
        "Two typefaces had the same postscript_name '{}': {}:{} and {}:{}",
        postscript_name,
        file_name_1,
        index_1,
        file_name_2,
        index_2
    )]
    DuplicatePostscriptName {
        postscript_name: String,
        file_name_1: String,
        index_1: u32,
        file_name_2: String,
        index_2: u32,
    },

    #[error(
        "Two typefaces had the same full_name '{}': {}:{} and {}:{}",
        full_name,
        file_name_1,
        index_1,
        file_name_2,
        index_2
    )]
    DuplicateFullName {
        full_name: String,
        file_name_1: String,
        index_1: u32,
        file_name_2: String,
        index_2: u32,
    },
}

/// Collection of errors from generating a manifest.
#[derive(Debug, Error)]
#[error("Errors occurred while generating manifest: {}", _0)]
pub struct GeneratorErrors(GeneratorErrorVec);

#[derive(Debug)]
pub struct GeneratorErrorVec(Vec<GeneratorError>);

impl From<Vec<GeneratorError>> for GeneratorErrorVec {
    fn from(errors: Vec<GeneratorError>) -> Self {
        GeneratorErrorVec(errors)
    }
}

impl From<GeneratorErrors> for Vec<GeneratorError> {
    fn from(source: GeneratorErrors) -> Self {
        source.0 .0
    }
}

impl fmt::Display for GeneratorErrorVec {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let descriptions = self.0.iter().map(|e| format!("{}", e)).collect_vec();
        write!(f, "{:#?}", descriptions)
    }
}

// For additional coverage, see integration tests in `//src/fonts/tools/manifest_generator/tests`.
#[cfg(test)]
mod tests {
    #![allow(deprecated)]

    use {
        super::*,
        crate::{
            font_catalog::{Asset, Family, Typeface, TypefaceInAssetIndex},
            font_pkgs::FontPackageEntry,
            font_sets::{FontSet, FontSets},
            product_config::{ProductConfig, Settings, TypefaceId},
        },
        anyhow::format_err,
        assert_matches::assert_matches,
        char_set::CharSet,
        fidl_fuchsia_fonts::{GenericFontFamily, Slant, Width},
        font_info::{FontAssetSource, FontInfo, FontInfoLoader},
        manifest::v2::Style,
        maplit::btreemap,
    };

    /// Implementation of `FontInfoLoader` whose outputs for each file name and index are explicitly
    /// specified in a user-provided map.
    #[derive(Debug)]
    struct TestFontInfoLoader {
        map: BTreeMap<(String, u32), FontInfo>,
    }

    impl TestFontInfoLoader {
        fn new(map: BTreeMap<(String, u32), FontInfo>) -> Self {
            TestFontInfoLoader { map }
        }
    }

    impl FontInfoLoader for TestFontInfoLoader {
        fn load_font_info<S, E>(&self, source: S, index: u32) -> Result<FontInfo, Error>
        where
            S: std::convert::TryInto<FontAssetSource, Error = E>,
            E: Sync + Send + Into<Error>,
        {
            let source: FontAssetSource = source.try_into().map_err(|e| e.into())?;
            let file_name = {
                if let FontAssetSource::FilePath(path) = source {
                    path.split('/').last().expect("file path").to_string()
                } else {
                    unreachable!();
                }
            };

            self.map
                .get(&(file_name, index))
                .map(|font_info| font_info.clone())
                .ok_or_else(|| format_err!("not found"))
        }
    }

    fn make_font_db_args() -> (FontCatalog, FontPackageListing, ProductConfig) {
        let font_catalog = FontCatalog {
            families: vec![Family {
                name: "Alpha Sans".to_string(),
                aliases: vec![],
                generic_family: Some(GenericFontFamily::SansSerif),
                fallback: true,
                assets: vec![
                    Asset {
                        file_name: "AlphaSans.ttc".to_string(),
                        typefaces: btreemap! {
                            TypefaceInAssetIndex(0) => Typeface {
                                index: 0,
                                languages: vec!["en".to_string()],
                                style: Style {
                                    slant: Slant::Upright,
                                    weight: 400,
                                    width: Width::Normal,
                                },
                            },
                            TypefaceInAssetIndex(2) => Typeface {
                                index: 2,
                                languages: vec!["en".to_string()],
                                style: Style {
                                    slant: Slant::Upright,
                                    weight: 700,
                                    width: Width::Normal,
                                },
                            },
                        },
                    },
                    Asset {
                        file_name: "AlphaSansCopy.ttf".to_string(),
                        typefaces: btreemap! {
                            TypefaceInAssetIndex(0) => Typeface {
                               index: 0,
                               languages: vec!["en".to_string()],
                               style: Style {
                                   slant: Slant::Upright,
                                   weight: 700,
                                   width: Width::Normal,
                               },
                           }
                        },
                    },
                ],
            }],
        };

        let font_pkgs = FontPackageListing::new(btreemap! {
            "AlphaSans.ttc".to_string() =>
                FontPackageEntry::new(
                    "AlphaSans.ttc",
                    "alphasans-ttc",
                    "alphasans/",
                ),
            "AlphaSansCopy.ttf".to_string() => {
                FontPackageEntry::new(
                    "AlphaSansCopy.ttf",
                    "alphasanscopy.ttf",
                    "alphasans/"
                )
            }
        });

        let product_config = ProductConfig {
            fallback_chain: vec![TypefaceId::new("AlphaSans.ttc", 0)],
            settings: Settings { cache_size_bytes: None },
        };

        (font_catalog, font_pkgs, product_config)
    }

    #[test]
    fn test_colliding_postscript_names() {
        use GeneratorError::*;

        let (font_catalog, font_pkgs, product_config) = make_font_db_args();
        let font_info_loader = TestFontInfoLoader::new(btreemap! {
            ("AlphaSans.ttc".to_string(), 0) => FontInfo {
                char_set: CharSet::new(vec![0x1, 0x2]),
                postscript_name: Some("AlphaSans-Regular".to_string()),
                full_name: Some("Alpha Sans".to_string())
            },
            ("AlphaSans.ttc".to_string(), 2) => FontInfo {
                char_set: CharSet::new(vec![0x1, 0x2]),
                postscript_name: Some("AlphaSans-Bold".to_string()),
                full_name: Some("Alpha Sans Bold".to_string())
            },
            ("AlphaSansCopy.ttf".to_string(), 0) => FontInfo {
                char_set: CharSet::new(vec![0x1, 0x2]),
                postscript_name: Some("AlphaSans-Bold".to_string()),
                full_name: Some("Alpha Sans Bold Copy".to_string())
            },
        });
        let font_dir = "foo/bar";
        let font_sets = FontSets::new(btreemap! {
            "AlphaSans.ttc".to_string() => FontSet::Local,
            "AlphaSansCopy.ttf".to_string() => FontSet::Local,
        });

        let result = generate_manifest(
            font_catalog,
            font_pkgs,
            font_sets,
            product_config,
            font_info_loader,
            font_dir,
            false,
        );

        assert_matches!(result, Err(_));
        let errors: Vec<GeneratorError> =
            result.unwrap_err().downcast::<GeneratorErrors>().expect("downcast").into();
        assert!(errors
            .iter()
            .find(|e| if let DuplicatePostscriptName { .. } = e { true } else { false })
            .is_some());
        assert!(errors
            .iter()
            .find(|e| if let DuplicateFullName { .. } = e { true } else { false })
            .is_none());
    }

    /// This is the same as the above test, but "AlphaSansCopy.ttf" is not included in the manifest,
    /// so there should be no collision.
    #[test]
    fn test_no_collision_unless_font_is_included_in_font_set() {
        let (font_catalog, font_pkgs, product_config) = make_font_db_args();
        let font_info_loader = TestFontInfoLoader::new(btreemap! {
            ("AlphaSans.ttc".to_string(), 0) => FontInfo {
                char_set: CharSet::new(vec![0x1, 0x2]),
                postscript_name: Some("AlphaSans-Regular".to_string()),
                full_name: Some("Alpha Sans".to_string())
            },
            ("AlphaSans.ttc".to_string(), 2) => FontInfo {
                char_set: CharSet::new(vec![0x1, 0x2]),
                postscript_name: Some("AlphaSans-Bold".to_string()),
                full_name: Some("Alpha Sans Bold".to_string())
            },
            ("AlphaSansCopy.ttf".to_string(), 0) => FontInfo {
                char_set: CharSet::new(vec![0x1, 0x2]),
                postscript_name: Some("AlphaSans-Bold".to_string()),
                full_name: Some("Alpha Sans Bold Copy".to_string())
            },
        });
        let font_dir = "foo/bar";
        let font_sets = FontSets::new(btreemap! {
            "AlphaSans.ttc".to_string() => FontSet::Local,
        });

        let result = generate_manifest(
            font_catalog,
            font_pkgs,
            font_sets,
            product_config,
            font_info_loader,
            font_dir,
            false,
        );

        assert_matches!(result, Ok(_));
    }

    #[test]
    fn test_colliding_full_names() {
        use GeneratorError::*;

        let (font_catalog, font_pkgs, product_config) = make_font_db_args();
        let font_info_loader = TestFontInfoLoader::new(btreemap! {
            ("AlphaSans.ttc".to_string(), 0) => FontInfo {
                char_set: CharSet::new(vec![0x1, 0x2]),
                postscript_name: Some("AlphaSans-Regular".to_string()),
                full_name: Some("Alpha Sans".to_string())
            },
            ("AlphaSans.ttc".to_string(), 2) => FontInfo {
                char_set: CharSet::new(vec![0x1, 0x2]),
                postscript_name: Some("AlphaSans-Bold".to_string()),
                full_name: Some("Alpha Sans Bold".to_string())
            },
            ("AlphaSansCopy.ttf".to_string(), 0) => FontInfo {
                char_set: CharSet::new(vec![0x1, 0x2]),
                postscript_name: Some("AlphaSans-BoldCopy".to_string()),
                full_name: Some("Alpha Sans Bold".to_string())
            },
        });
        let font_dir = "foo/bar";
        let font_sets = FontSets::new(btreemap! {
            "AlphaSans.ttc".to_string() => FontSet::Local,
            "AlphaSansCopy.ttf".to_string() => FontSet::Local,
        });

        let result = generate_manifest(
            font_catalog,
            font_pkgs,
            font_sets,
            product_config,
            font_info_loader,
            font_dir,
            false,
        );

        assert_matches!(result, Err(_));
        let errors: Vec<GeneratorError> =
            result.unwrap_err().downcast::<GeneratorErrors>().expect("downcast").into();
        assert!(errors
            .iter()
            .find(|e| if let DuplicatePostscriptName { .. } = e { true } else { false })
            .is_none());
        assert!(errors
            .iter()
            .find(|e| if let DuplicateFullName { .. } = e { true } else { false })
            .is_some());
    }

    #[test]
    fn test_no_full_names_no_problem() {
        let (font_catalog, font_pkgs, product_config) = make_font_db_args();
        let font_info_loader = TestFontInfoLoader::new(btreemap! {
            ("AlphaSans.ttc".to_string(), 0) => FontInfo {
                char_set: CharSet::new(vec![0x1, 0x2]),
                postscript_name: Some("AlphaSans-Regular".to_string()),
                full_name: None,
            },
            ("AlphaSans.ttc".to_string(), 2) => FontInfo {
                char_set: CharSet::new(vec![0x1, 0x2]),
                postscript_name: Some("AlphaSans-Bold".to_string()),
                full_name: None,
            },
            ("AlphaSansCopy.ttf".to_string(), 0) => FontInfo {
                char_set: CharSet::new(vec![0x1, 0x2]),
                postscript_name: Some("AlphaSans-BoldCopy".to_string()),
                full_name: None,
            },
        });
        let font_dir = "foo/bar";
        let font_sets = FontSets::new(btreemap! {
            "AlphaSans.ttc".to_string() => FontSet::Local,
            "AlphaSansCopy.ttf".to_string() => FontSet::Local,
        });

        let result = generate_manifest(
            font_catalog,
            font_pkgs,
            font_sets,
            product_config,
            font_info_loader,
            font_dir,
            false,
        );

        assert_matches!(result, Ok(_));
    }
}
