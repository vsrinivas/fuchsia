// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::collection::{Font, FontCollection};
use super::font_info;
use super::manifest;
use failure::{format_err, Error, ResultExt};
use fdio;
use fidl;
use fidl::encoding::OutOfLine;
use fidl_fuchsia_fonts as fonts;
use fidl_fuchsia_mem as mem;
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use fuchsia_zircon::HandleBased;
use futures::prelude::*;
use futures::{future, Future, FutureExt};
use log::error;
use std::collections::BTreeMap;
use std::fs::File;
use std::path::{Path, PathBuf};
use std::sync::{Arc, RwLock};
use unicase::UniCase;

fn clone_buffer(buf: &mem::Buffer) -> Result<mem::Buffer, Error> {
    let vmo_rights = zx::Rights::BASIC | zx::Rights::READ | zx::Rights::MAP;
    let vmo = buf.vmo.duplicate_handle(vmo_rights).context("Failed to duplicate VMO handle.")?;
    Ok(mem::Buffer { vmo, size: buf.size })
}

struct Asset {
    path: PathBuf,
    buffer: RwLock<Option<mem::Buffer>>,
}

/// Provides cached storage of [`Asset`]s. Should be initialized by [`FontService`].
///
/// Each [`Asset`] is assigned an ID equal to its index in `assets`.
///
/// # Examples
/// ```
/// let asset_collection = AssetCollection::new();
/// // Load data into asset_collection, including "/path/to/font.ttf".
/// let path: PathBuf = PathBuf::from("/path/to/font.ttf");
/// let asset_id: u32 = asset_collection.assets_map.get(path).unwrap();
/// let asset: &Asset = &asset_collection.assets[asset_id as usize];
/// ```
struct AssetCollection {
    /// Maps [`Asset`] path to ID.
    assets_map: BTreeMap<PathBuf, u32>,

    /// [`Asset`] cache. Indices correspond to values stored in `self.assets_map`.
    ///
    /// Currently, no cache eviction strategy is implemented (I18N-15).
    assets: Vec<Asset>,
}

/// Load a `Vmo` from disk into a `Buffer`.
fn load_asset_to_vmo(path: &Path) -> Result<mem::Buffer, Error> {
    let file = File::open(path)?;
    let vmo = fdio::get_vmo_copy_from_file(&file)?;
    let size = file.metadata()?.len();
    Ok(mem::Buffer { vmo, size })
}

impl AssetCollection {
    fn new() -> AssetCollection {
        AssetCollection { assets_map: BTreeMap::new(), assets: vec![] }
    }

    /// Lazily add the [`Asset`] found at `path` to the collection and return its ID.
    /// If it already exists, just return the ID.
    ///
    /// The push onto `assets` is lazy in the sense that the pushed [`Asset`] has an empty `buffer`.
    fn add_or_get_asset_id(&mut self, path: &Path) -> u32 {
        if let Some(id) = self.assets_map.get(path) {
            return *id;
        }

        let id = self.assets.len() as u32;
        self.assets.push(Asset { path: path.to_path_buf(), buffer: RwLock::new(None) });
        self.assets_map.insert(path.to_path_buf(), id);
        id
    }

    /// Get a `Buffer` holding the `Vmo` for the [`Asset`] corresponding to `id`, using the
    /// cache if possible. On a cache miss, the [`Asset`] is cached unconditionally.
    fn get_asset(&self, id: u32) -> Result<mem::Buffer, Error> {
        assert!(id < self.assets.len() as u32);

        let asset = &self.assets[id as usize];

        if let Some(cached) = asset.buffer.read().unwrap().as_ref() {
            return clone_buffer(cached);
        }

        let buf = load_asset_to_vmo(&asset.path)
            .with_context(|_| format!("Failed to load {}", asset.path.to_string_lossy()))?;
        let buf_clone = clone_buffer(&buf)?;
        *asset.buffer.write().unwrap() = Some(buf);
        Ok(buf_clone)
    }

    /// Clear the cache by deallocating all [`Asset`]'s `buffer`s.
    /// Does not reduce the size of `assets`.
    fn reset_cache(&mut self) {
        for asset in self.assets.iter_mut() {
            *asset.buffer.write().unwrap() = None;
        }
    }
}

struct FontFamily {
    name: String,
    fonts: FontCollection,
    fallback_group: fonts::FallbackGroup,
}

impl FontFamily {
    fn new(name: String, fallback_group: fonts::FallbackGroup) -> FontFamily {
        FontFamily { name, fonts: FontCollection::new(), fallback_group }
    }
}

enum FamilyOrAlias {
    Family(FontFamily),
    /// Represents an alias to a `Family` whose name is the associated [`UniCase`]`<`[`String`]`>`.
    Alias(UniCase<String>),
}

pub struct FontService {
    assets: AssetCollection,
    /// Maps the font family name from the manifest (`families.family`) to a FamilyOrAlias.
    families: BTreeMap<UniCase<String>, FamilyOrAlias>,
    fallback_collection: FontCollection,
}

impl FontService {
    pub fn new() -> FontService {
        FontService {
            assets: AssetCollection::new(),
            families: BTreeMap::new(),
            fallback_collection: FontCollection::new(),
        }
    }

    /// Verify that we have a reasonable font configuration and can start.
    pub fn check_can_start(&self) -> Result<(), Error> {
        if self.fallback_collection.is_empty() {
            return Err(format_err!("Need at least one fallback font"));
        }

        Ok(())
    }

    pub fn load_manifest(&mut self, manifest_path: &Path) -> Result<(), Error> {
        let manifest = manifest::FontsManifest::load_from_file(&manifest_path)?;
        self.add_fonts_from_manifest(manifest).with_context(|_| {
            format!("Failed to load fonts from {}", manifest_path.to_string_lossy())
        })?;

        Ok(())
    }

    fn add_fonts_from_manifest(
        &mut self,
        mut manifest: manifest::FontsManifest,
    ) -> Result<(), Error> {
        let font_info_loader = font_info::FontInfoLoader::new()?;

        for mut family_manifest in manifest.families.drain(..) {
            if family_manifest.fonts.is_empty() {
                continue;
            }

            let family_name = UniCase::new(family_manifest.family.clone());

            // Get the [`FamilyOrAlias`] from `families` associated with `family_name`.
            //
            // If the key `family_name` does not exist in `families`, insert it with a
            // [`FamilyOrAlias`]`::Family` value.
            //
            // If a [`FamilyOrAlias`]`::Alias` with the same name already exists, [`Error`].
            let family = match self.families.entry(family_name.clone()).or_insert_with(|| {
                FamilyOrAlias::Family(FontFamily::new(
                    family_manifest.family.clone(),
                    family_manifest.fallback_group,
                ))
            }) {
                FamilyOrAlias::Family(f) => f,
                FamilyOrAlias::Alias(other_family) => {
                    // Different manifest files may declare fonts for the same family,
                    // but font aliases cannot conflict with main family name.
                    return Err(format_err!(
                        "Conflicting font alias: {} is already declared as an alias for {}",
                        family_name,
                        other_family
                    ));
                }
            };

            for font_manifest in family_manifest.fonts.drain(..) {
                let asset_id = self.assets.add_or_get_asset_id(font_manifest.asset.as_path());

                let buffer = self.assets.get_asset(asset_id).with_context(|_| {
                    format!("Failed to load font from {}", font_manifest.asset.to_string_lossy())
                })?;

                let info = font_info_loader
                    .load_font_info(buffer.vmo, buffer.size as usize, font_manifest.index)
                    .with_context(|_| {
                        format!(
                            "Failed to load font info from {}",
                            font_manifest.asset.to_string_lossy()
                        )
                    })?;
                let font = Arc::new(Font::new(
                    asset_id,
                    font_manifest,
                    info,
                    family_manifest.fallback_group,
                ));
                family.fonts.add_font(font.clone());
                if family_manifest.fallback {
                    self.fallback_collection.add_font(font);
                }
            }

            // Register family aliases.
            for alias in family_manifest.aliases.unwrap_or(vec![]) {
                let alias_unicase = UniCase::new(alias.clone());

                match self.families.get(&alias_unicase) {
                    None => {
                        self.families
                            .insert(alias_unicase, FamilyOrAlias::Alias(family_name.clone()));
                    }
                    Some(FamilyOrAlias::Family(_)) => {
                        return Err(format_err!(
                            "Can't add alias {} for {} because a family with that name already \
                             exists.",
                            alias,
                            family_name
                        ))
                    }
                    Some(FamilyOrAlias::Alias(other_family)) => {
                        // If the alias exists then it must be for the same font family.
                        if *other_family != family_name {
                            return Err(format_err!(
                                "Can't add alias {} for {} because it's already declared as alias \
                                 for {}.",
                                alias,
                                family_name,
                                other_family
                            ));
                        }
                    }
                }
            }
        }

        // Flush the cache. Font files will be loaded again when they are needed.
        self.assets.reset_cache();

        Ok(())
    }

    /// Get font family by name.
    fn match_family(&self, family_name: &UniCase<String>) -> Option<&FontFamily> {
        let family = match self.families.get(family_name)? {
            FamilyOrAlias::Family(f) => f,
            FamilyOrAlias::Alias(a) => match self.families.get(a) {
                Some(FamilyOrAlias::Family(f)) => f,
                _ => panic!("Invalid font alias."),
            },
        };
        Some(family)
    }

    fn match_request(&self, mut request: fonts::Request) -> Option<fonts::Response> {
        request.language =
            request.language.map(|list| list.iter().map(|l| l.to_ascii_lowercase()).collect());

        let matched_family = request
            .family
            .as_ref()
            .and_then(|family| self.match_family(&UniCase::new(family.clone())));
        let mut font = matched_family.and_then(|family| family.fonts.match_request(&request));

        if font.is_none() && (request.flags & fonts::REQUEST_FLAG_NO_FALLBACK) == 0 {
            // If fallback_group is not specified by the client explicitly then copy it from
            // the matched font family.
            if request.fallback_group == fonts::FallbackGroup::None {
                if let Some(family) = matched_family {
                    request.fallback_group = family.fallback_group;
                }
            }
            font = self.fallback_collection.match_request(&request);
        }

        font.and_then(|font| match self.assets.get_asset(font.asset_id) {
            Ok(buffer) => Some(fonts::Response {
                buffer,
                buffer_id: font.asset_id,
                font_index: font.font_index,
            }),
            Err(err) => {
                error!("Failed to load font file: {}", err);
                None
            }
        })
    }

    fn get_font_info(&self, family_name: String) -> Option<fonts::FamilyInfo> {
        let family_name = UniCase::new(family_name);
        let family = self.match_family(&family_name)?;
        Some(fonts::FamilyInfo {
            name: family.name.clone(),
            styles: family.fonts.get_styles().collect(),
        })
    }

    fn handle_font_provider_request(
        &self,
        request: fonts::ProviderRequest,
    ) -> impl Future<Output = Result<(), fidl::Error>> {
        #[allow(unused_variables)]
        match request {
            fonts::ProviderRequest::GetFont { request, responder } => {
                let mut response = self.match_request(request);
                future::ready(responder.send(response.as_mut().map(OutOfLine)))
            }
            fonts::ProviderRequest::GetFamilyInfo { family, responder } => {
                let mut font_info = self.get_font_info(family);
                future::ready(responder.send(font_info.as_mut().map(OutOfLine)))
            }
            fonts::ProviderRequest::GetTypeface { request, responder } => {
                // TODO(I18N-12): Implement changes from API review
                unimplemented!();
            }
            fonts::ProviderRequest::GetFontFamilyInfo { family, responder } => {
                // TODO(I18N-12): Implement changes from API review
                unimplemented!();
            }
        }
    }
}

pub fn spawn_server(font_service: Arc<FontService>, stream: fonts::ProviderRequestStream) {
    // TODO(sergeyu): Consider making handle_font_provider_request() and
    // load_asset_to_vmo() asynchronous and using try_for_each_concurrent()
    // instead of try_for_each() here. That would be useful only if clients can
    // send more than one concurrent request.
    let stream_complete = stream
        .try_for_each(move |request| font_service.handle_font_provider_request(request))
        .map(|_| ());
    fasync::spawn(stream_complete);
}
