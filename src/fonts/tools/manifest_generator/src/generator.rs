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
    std::path::Path,
};

/// Builds a `FontDb` and then generates a manifest from all of the font metadata that has been
/// loaded.
///
/// For test coverage, see integration tests.
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
                            .map(|catalog_typeface| v2::Typeface {
                                index: catalog_typeface.index,
                                languages: catalog_typeface.languages.clone(),
                                style: catalog_typeface.style.clone(),
                                code_points: db
                                    .get_code_points(
                                        catalog_asset,
                                        TypefaceInAssetIndex(catalog_typeface.index),
                                    )
                                    .clone(),
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

    Ok(FontManifestWrapper::Version2(manifest))
}
