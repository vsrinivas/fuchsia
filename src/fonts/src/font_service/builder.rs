// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{
        asset::{AssetCollection, AssetCollectionBuilder},
        family::{FamilyOrAlias, FontFamily},
        typeface::{Collection as TypefaceCollection, Typeface},
        FontService,
    },
    anyhow::{format_err, Error},
    clonable_error::ClonableError,
    font_info::FontInfoLoaderImpl,
    fuchsia_syslog::fx_vlog,
    manifest::{v2, FontManifestWrapper, FontsManifest},
    std::{
        collections::BTreeMap,
        convert::TryFrom,
        path::{Path, PathBuf},
        sync::Arc,
    },
    thiserror::Error,
    unicase::UniCase,
};

/// Builder for [`FontService`]. Allows populating the fields that remain immutable for the
/// lifetime of the service.
///
/// Create a new builder with [`new()`](FontServiceBuilder::new), then populate using
/// [`load_manifest()`](FontServiceBuilder::load_manifest), and finally construct a `FontService`
/// using [`build()`](FontServiceBuilder::build).
pub struct FontServiceBuilder {
    manifests: Vec<ManifestOrPath>,
    assets: AssetCollectionBuilder,
    /// Maps the font family name from the manifest (`families.family`) to a FamilyOrAlias.
    families: BTreeMap<UniCase<String>, FamilyOrAlias>,
    fallback_collection: TypefaceCollection,
}

impl FontServiceBuilder {
    /// Creates a new, empty builder.
    pub fn new() -> Self {
        FontServiceBuilder {
            manifests: vec![],
            assets: AssetCollectionBuilder::new(),
            families: BTreeMap::new(),
            fallback_collection: TypefaceCollection::new(),
        }
    }

    /// Add a manifest path to be parsed and processed.
    pub fn add_manifest_from_file(&mut self, manifest_path: &Path) -> &mut Self {
        self.manifests.push(ManifestOrPath::Path(manifest_path.to_path_buf()));
        self
    }

    /// Adds a parsed manifest to be processed.
    #[allow(dead_code)]
    #[cfg(test)]
    pub fn add_manifest(&mut self, manifest_wrapper: FontManifestWrapper) -> &mut Self {
        self.manifests.push(ManifestOrPath::Manifest(manifest_wrapper));
        self
    }

    /// Tries to build a [`FontService`] from the provided manifests, with some additional error checking.
    pub async fn build(mut self) -> Result<FontService, Error> {
        let manifests: Result<Vec<(FontManifestWrapper, Option<PathBuf>)>, Error> = self
            .manifests
            .drain(..)
            .map(|manifest_or_path| match manifest_or_path {
                ManifestOrPath::Manifest(manifest) => Ok((manifest, None)),
                ManifestOrPath::Path(path) => {
                    fx_vlog!(1, "Loading manifest {:?}", &path);
                    Ok((FontsManifest::load_from_file(&path)?, Some(path)))
                }
            })
            .collect();

        for (wrapper, path) in manifests? {
            match wrapper {
                FontManifestWrapper::Version1(v1) => {
                    self.add_fonts_from_manifest_v1(v1, path).await?
                }
                FontManifestWrapper::Version2(v2) => {
                    self.add_fonts_from_manifest_v2(v2, path).await?
                }
            }
        }

        if self.fallback_collection.is_empty() {
            return Err(FontServiceBuilderError::NoFallbackCollection.into());
        }

        Ok(FontService {
            assets: self.assets.build(),
            families: self.families,
            fallback_collection: self.fallback_collection,
        })
    }

    async fn add_fonts_from_manifest_v2(
        &mut self,
        manifest: v2::FontsManifest,
        manifest_path: Option<PathBuf>,
    ) -> Result<(), Error> {
        for mut manifest_family in manifest.families {
            // Register the family itself
            let family_name = UniCase::new(manifest_family.name.clone());
            let family = match self.families.entry(family_name.clone()).or_insert_with(|| {
                FamilyOrAlias::Family(FontFamily::new(
                    family_name.to_string(),
                    manifest_family.generic_family,
                ))
            }) {
                FamilyOrAlias::Family(f) => f,
                FamilyOrAlias::Alias(_, _) => {
                    return Err(FontServiceBuilderError::AliasFamilyConflict {
                        conflicting_name: family_name.to_string(),
                        manifest_path: manifest_path.clone(),
                    }
                    .into());
                }
            };

            // Register the family's assets.

            // We have to use `.drain()` here in order to leave `manifest_family` in a valid state
            // to be able to keep using it further down.
            for manifest_asset in manifest_family.assets.drain(..) {
                let asset_id = self.assets.add_or_get_asset_id(&manifest_asset);
                for manifest_typeface in manifest_asset.typefaces {
                    if manifest_typeface.code_points.is_empty() {
                        return Err(FontServiceBuilderError::NoCodePoints {
                            asset_name: manifest_asset.file_name.to_string(),
                            typeface_idx: manifest_typeface.index,
                            manifest_path: manifest_path.clone(),
                        }
                        .into());
                    }
                    let typeface = Arc::new(Typeface::new(
                        asset_id,
                        manifest_typeface,
                        manifest_family.generic_family,
                    )?);
                    family.faces.add_typeface(typeface.clone());
                    if manifest_family.fallback {
                        self.fallback_collection.add_typeface(typeface);
                    }
                }
            }
            // Above, we're working with `family` mutably borrowed from `self.families`. We have to
            // finish using any mutable references to `self.families` before we can create further
            // references to `self.families` below.

            // Register aliases
            let aliases = FamilyOrAlias::aliases_from_family(&manifest_family);
            for (key, value) in aliases {
                match self.families.get(&key) {
                    None => {
                        self.families.insert(key, value);
                    }
                    Some(FamilyOrAlias::Family(_)) => {
                        return Err(FontServiceBuilderError::AliasFamilyConflict {
                            conflicting_name: key.to_string(),
                            manifest_path: manifest_path.clone(),
                        }
                        .into());
                    }
                    Some(FamilyOrAlias::Alias(other_family_name, _)) => {
                        // If the alias exists then it must be for the same font family.
                        if *other_family_name != family_name {
                            return Err(FontServiceBuilderError::AmbiguousAlias {
                                alias: key.to_string(),
                                canonical_1: other_family_name.to_string(),
                                canonical_2: family_name.to_string(),
                                manifest_path: manifest_path.clone(),
                            }
                            .into());
                        }
                    }
                }
            }
        }

        Ok(())
    }

    async fn add_fonts_from_manifest_v1(
        &mut self,
        manifest_v1: FontsManifest,
        manifest_path: Option<PathBuf>,
    ) -> Result<(), Error> {
        let manifest_v2 = self.convert_manifest_v1_to_v2(manifest_v1).await.map_err(|e| {
            FontServiceBuilderError::ConversionFromV1 {
                manifest_path: manifest_path.clone(),
                cause: Error::from(e).into(),
            }
        })?;
        self.add_fonts_from_manifest_v2(manifest_v2, manifest_path).await
    }

    /// Converts data format from manifest v1 to v2 and loads character sets for any typefaces that
    /// lack them.
    async fn convert_manifest_v1_to_v2(
        &self,
        manifest_v1: FontsManifest,
    ) -> Result<v2::FontsManifest, Error> {
        let mut manifest_v2 = v2::FontsManifest::try_from(manifest_v1)?;
        let font_info_loader = FontInfoLoaderImpl::new()?;

        for manifest_family in &mut manifest_v2.families {
            for manifest_asset in &mut manifest_family.assets {
                for manifest_typeface in &mut manifest_asset.typefaces {
                    if manifest_typeface.code_points.is_empty() {
                        match &manifest_asset.location {
                            v2::AssetLocation::LocalFile(v2::LocalFileLocator { directory }) => {
                                let asset_path = directory.join(&manifest_asset.file_name);
                                let buffer = AssetCollection::load_asset_to_vmo(&asset_path)?;
                                let font_info = font_info_loader
                                    .load_font_info(buffer, manifest_typeface.index)?;
                                manifest_typeface.code_points = font_info.char_set;
                            }
                            _ => {
                                return Err(format_err!(
                                    "Impossible asset location: {:?}",
                                    &manifest_asset
                                ));
                            }
                        }
                    }
                }
            }
        }

        Ok(manifest_v2)
    }
}

#[allow(dead_code)]
enum ManifestOrPath {
    Manifest(FontManifestWrapper),
    Path(PathBuf),
}

/// Errors arising from the use of [`FontServiceBuilder`].
#[derive(Debug, Error)]
pub enum FontServiceBuilderError {
    /// A name was used as both a canonical family name and a font family alias.
    #[error(
        "Conflict in {:?}: {} cannot be both a canonical family name and an alias",
        manifest_path,
        conflicting_name
    )]
    AliasFamilyConflict { conflicting_name: String, manifest_path: Option<PathBuf> },

    /// One string was used as an alias for two different font families.
    #[error(
        "Conflict in {:?}: {} cannot be an alias for both {} and {}",
        manifest_path,
        alias,
        canonical_1,
        canonical_2
    )]
    AmbiguousAlias {
        alias: String,
        canonical_1: String,
        canonical_2: String,
        manifest_path: Option<PathBuf>,
    },

    /// Something went wrong when converting a manifest from v1 to v2.
    #[error("Conversion from manifest v1 failed in {:?}: {:?}", manifest_path, cause)]
    ConversionFromV1 {
        manifest_path: Option<PathBuf>,
        #[source]
        cause: ClonableError,
    },

    /// None of the loaded manifests contained any families designated as `fallback: true`.
    #[error("Need at least one fallback font family")]
    NoFallbackCollection,

    /// The manifest did not have defined code points for a particular typeface.
    #[error("Missing code points for \"{}\"[{}] in {:?}", asset_name, typeface_idx, manifest_path)]
    NoCodePoints { asset_name: String, typeface_idx: u32, manifest_path: Option<PathBuf> },
}
