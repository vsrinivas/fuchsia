// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        font_catalog::TypefaceInAssetIndex, font_db::FontDb, FontCatalog, FontPackageListing,
        FontSets,
    },
    anyhow::Error,
    font_info::FontInfoLoader,
    manifest::{v2, FontManifestWrapper},
    std::path::Path,
};

/// Builds a `FontDb` and then generates a manifest from all of the font metadata that has been
/// loaded.
///
/// For test coverage, see integration tests.
pub(crate) fn generate_manifest(
    font_catalog: FontCatalog,
    font_pkgs: FontPackageListing,
    font_sets: FontSets,
    font_info_loader: impl FontInfoLoader,
    font_dir: impl AsRef<Path>,
) -> Result<FontManifestWrapper, Error> {
    let db = FontDb::new(font_catalog, font_pkgs, font_sets, font_info_loader, font_dir)?;

    let manifest = v2::FontsManifest {
        families: db
            .iter_families()
            .map(|fi_family| v2::Family {
                name: fi_family.name.clone(),
                aliases: fi_family
                    .aliases
                    .iter()
                    .cloned()
                    .map(|string_or_alias_set| string_or_alias_set.into())
                    .collect(),
                generic_family: fi_family.generic_family,
                fallback: fi_family.fallback,
                assets: db
                    .iter_assets(fi_family)
                    .map(|fi_asset| v2::Asset {
                        file_name: fi_asset.file_name.clone(),
                        location: db.get_asset_location(fi_asset),
                        typefaces: fi_asset
                            .typefaces
                            .values()
                            .map(|fi_typeface| v2::Typeface {
                                index: fi_typeface.index,
                                languages: fi_typeface.languages.clone(),
                                style: fi_typeface.style.clone(),
                                code_points: db
                                    .get_code_points(
                                        fi_asset,
                                        TypefaceInAssetIndex(fi_typeface.index),
                                    )
                                    .clone(),
                            })
                            .collect(),
                    })
                    .collect(),
            })
            .collect(),
    };

    Ok(FontManifestWrapper::Version2(manifest))
}
