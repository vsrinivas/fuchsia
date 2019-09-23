// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use manifest::v2::PackageLocator;
use {
    crate::{
        constants::PKG_URL_PREFIX, font_catalog as fi, FontCatalog, FontPackageListing, FontSets,
    },
    char_set::CharSet,
    failure::{Error, Fail},
    font_info::{FontAssetSource, FontInfo, FontInfoLoader},
    fuchsia_url::pkg_url::PkgUrl,
    itertools::{Either, Itertools},
    manifest::v2,
    std::{
        collections::BTreeMap,
        path::{Path, PathBuf},
    },
};

/// Collection of font metadata used for generating a font manifest for a particular target.
///
/// Contains indices by family and asset names, which all provide access only to those fonts that
/// are included in `font_sets`. (`font_catalog` may contain other fonts that are not included in
/// the target product.)
///
/// For test coverage, please see the integration tests.
pub(crate) struct FontDb {
    font_catalog: FontCatalog,
    font_sets: FontSets,

    family_name_to_family: BTreeMap<String, usize>,
    /// The index is the asset index within a family.
    asset_name_to_asset: BTreeMap<String, usize>,
    asset_name_to_family: BTreeMap<String, usize>,
    asset_name_to_pkg_url: BTreeMap<String, PkgUrl>,
    typeface_to_char_set: BTreeMap<(String, u32), CharSet>,
}

impl FontDb {
    /// Tries to create a new instance of `FontDb`.
    pub fn new<P: AsRef<Path>>(
        font_catalog: FontCatalog,
        font_pkgs: FontPackageListing,
        font_sets: FontSets,
        font_info_loader: impl FontInfoLoader,
        font_dir: P,
    ) -> Result<FontDb, FontDbErrors> {
        let mut family_name_to_family = BTreeMap::new();
        let mut asset_name_to_asset = BTreeMap::new();
        let mut asset_name_to_family = BTreeMap::new();
        let mut asset_name_to_pkg_url = BTreeMap::new();
        let typeface_to_char_set = BTreeMap::new();

        let mut errors: Vec<FontDbError> = vec![];

        for (family_idx, family) in (&font_catalog.families).iter().enumerate() {
            let mut asset_count = 0;
            for (asset_idx, asset) in (&family.assets).iter().enumerate() {
                let asset_name = asset.file_name.clone();
                if asset.typefaces.is_empty() {
                    errors.push(FontDbError::FontCatalogNoTypeFaces { asset_name });
                    continue;
                }

                if font_sets.get_package_set(&asset.file_name).is_some() {
                    let safe_name = font_pkgs.get_safe_name(&asset_name);
                    if safe_name.is_none() {
                        errors.push(FontDbError::FontPkgsMissingEntry { asset_name });
                        continue;
                    }

                    let pkg_url = Self::make_pkg_url(safe_name.unwrap());
                    if let Err(error) = pkg_url {
                        errors.push(error);
                        continue;
                    }
                    let pkg_url = pkg_url.unwrap();

                    asset_name_to_asset.insert(asset_name.clone(), asset_idx);
                    asset_name_to_pkg_url.insert(asset_name.clone(), pkg_url);
                    asset_name_to_family.insert(asset_name.clone(), family_idx);

                    asset_count += 1;
                }
                // Skip families where no assets are included in the target product
                if asset_count > 0 {
                    family_name_to_family.insert(family.name.clone(), family_idx);
                }
            }
        }

        // typeface_to_char_set is empty at this point.
        let mut db = FontDb {
            font_catalog,
            font_sets,
            family_name_to_family,
            asset_name_to_asset,
            asset_name_to_family,
            asset_name_to_pkg_url,
            typeface_to_char_set,
        };

        let font_infos = Self::load_font_infos(&db, &font_pkgs, font_info_loader, font_dir);

        match font_infos {
            Ok(font_infos) => {
                for (request, font_info) in font_infos {
                    db.typeface_to_char_set
                        .insert((request.asset_name(), request.index), font_info.char_set);
                }
            }
            Err(mut font_info_errors) => {
                errors.append(&mut font_info_errors);
            }
        }

        if errors.is_empty() {
            Ok(db)
        } else {
            Err(FontDbErrors(errors))
        }
    }

    pub fn get_family_by_name(&self, family_name: impl AsRef<str>) -> Option<&fi::Family> {
        let family_idx = self.family_name_to_family.get(family_name.as_ref())?;
        self.font_catalog.families.get(*family_idx)
    }

    pub fn get_asset_by_name(&self, asset_name: impl AsRef<str>) -> Option<&fi::Asset> {
        let family = self.get_family_by_asset_name(asset_name.as_ref())?;
        let asset_idx = self.asset_name_to_asset.get(asset_name.as_ref())?;
        family.assets.get(*asset_idx)
    }

    pub fn get_family_by_asset_name(&self, asset_name: impl AsRef<str>) -> Option<&fi::Family> {
        let family_idx = self.asset_name_to_family.get(asset_name.as_ref())?;
        self.font_catalog.families.get(*family_idx)
    }

    /// The asset must be in the `FontDb` or this method will panic.
    pub fn get_code_points(&self, asset: &fi::Asset, index: u32) -> &CharSet {
        // Alas, no sane way to transpose between `(&str, &u32)` and `&(String, u32)`.
        let key = (asset.file_name.to_owned(), index);
        self.typeface_to_char_set.get(&key).unwrap()
    }

    /// The asset must be in the `FontDb` or this method will panic.
    pub fn get_asset_location(&self, asset: &fi::Asset) -> v2::AssetLocation {
        v2::AssetLocation::Package(PackageLocator {
            url: self.asset_name_to_pkg_url.get(&*asset.file_name).unwrap().clone(),
            set: self.font_sets.get_package_set(&*asset.file_name).unwrap().clone(),
        })
    }

    /// Iterates over all the _included_ font families in the `FontDb`.
    pub fn iter_families(&self) -> impl Iterator<Item = &'_ fi::Family> + '_ {
        self.font_catalog
            .families
            .iter()
            .filter(move |family| self.get_family_by_name(&*family.name).is_some())
    }

    /// Iterates over all the _included_ assets in the given font family. Note this is _not_ the
    /// same as iterating over `Family::assets`.
    pub fn iter_assets<'a>(
        &'a self,
        family: &'a fi::Family,
    ) -> impl Iterator<Item = &'a fi::Asset> + 'a {
        family
            .assets
            .iter()
            .filter(move |asset| self.get_asset_by_name(&*asset.file_name).is_some())
    }

    fn make_pkg_url(safe_name: impl AsRef<str>) -> Result<PkgUrl, FontDbError> {
        let pkg_url = format!("{}{}", PKG_URL_PREFIX, safe_name.as_ref());
        Ok(PkgUrl::parse(&pkg_url).map_err(|error| FontDbError::PkgUrl { error: error.into() })?)
    }

    fn load_font_infos(
        db: &FontDb,
        font_pkgs: &FontPackageListing,
        font_info_loader: impl FontInfoLoader,
        font_dir: impl AsRef<Path>,
    ) -> Result<Vec<(FontInfoRequest, FontInfo)>, Vec<FontDbError>> {
        let (requests, errors): (Vec<_>, Vec<_>) = db
            .font_sets
            .iter()
            .map(|(asset_name, _)| {
                Self::asset_to_font_info_requests(db, font_pkgs, font_dir.as_ref(), asset_name)
            })
            .flatten()
            .partition_map(|r| match r {
                Ok(data) => Either::Left(data),
                Err(err) => Either::Right(err),
            });

        if !errors.is_empty() {
            return Err(errors);
        }

        let (font_infos, errors): (Vec<_>, Vec<_>) = requests
            .into_iter()
            .map(|request| {
                let source = FontAssetSource::FilePath(request.path.to_str().unwrap().to_owned());
                let font_info = font_info_loader.load_font_info(source, request.index);
                match font_info {
                    Ok(font_info) => Ok((request, font_info)),
                    Err(error) => Err(FontDbError::FontInfo { request, error }),
                }
            })
            .partition_map(|r| match r {
                Ok(data) => Either::Left(data),
                Err(err) => Either::Right(err),
            });

        if !errors.is_empty() {
            Err(errors)
        } else {
            Ok(font_infos)
        }
    }

    fn asset_to_font_info_requests(
        db: &FontDb,
        font_pkgs: &FontPackageListing,
        font_dir: impl AsRef<Path>,
        asset_name: &str,
    ) -> Vec<Result<FontInfoRequest, FontDbError>> {
        let mut path = font_dir.as_ref().to_path_buf();

        let path_prefix = font_pkgs.get_path_prefix(asset_name);
        if path_prefix.is_none() {
            return vec![Err(FontDbError::FontPkgsMissingEntry {
                asset_name: asset_name.to_owned(),
            })];
        }

        let path_prefix = path_prefix.unwrap();
        path.push(path_prefix);
        path.push(asset_name);

        let asset = db.get_asset_by_name(asset_name).unwrap();
        asset
            .typefaces
            .iter()
            .map(move |typeface| Ok(FontInfoRequest { path: path.clone(), index: typeface.index }))
            .collect_vec()
        // We have to collect into a vector here because otherwise there's no way to return
        // a consistent `Iterator` type.
    }
}

/// Collection of errors from loading / building `FontDb`.
#[derive(Debug, Fail)]
#[fail(display = "Errors occurred while building FontDb: {:#?}", _0)]
pub(crate) struct FontDbErrors(Vec<FontDbError>);

/// An error in a single `FontDb` operation.
#[derive(Debug, Fail)]
pub(crate) enum FontDbError {
    #[fail(display = "Asset {} has no typefaces", asset_name)]
    FontCatalogNoTypeFaces { asset_name: String },
    #[fail(display = "Asset {} is not listed in *.font_pkgs.json", asset_name)]
    FontPkgsMissingEntry { asset_name: String },
    #[fail(display = "PkgUrl error: {:?}", error)]
    PkgUrl {
        #[cause]
        error: Error,
    },
    #[fail(display = "Failed to load font info for {:?}: {:?}", request, error)]
    FontInfo {
        request: FontInfoRequest,
        #[cause]
        error: Error,
    },
}

/// Metadata needed for [`FontInfoLoader::load_font_info`].
#[derive(Debug, Clone)]
pub(crate) struct FontInfoRequest {
    /// Path to the file
    path: PathBuf,
    /// Index of the font in the file
    index: u32,
}

impl FontInfoRequest {
    fn asset_name(&self) -> String {
        self.path.file_name().and_then(|os_str| os_str.to_str()).unwrap().to_owned()
    }
}
