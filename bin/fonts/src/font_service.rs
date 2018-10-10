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
use fidl::endpoints::RequestStream;
use fidl_fuchsia_fonts as fonts;
use fidl_fuchsia_mem as mem;
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use fuchsia_zircon::HandleBased;
use futures::prelude::*;
use futures::{future, Future, FutureExt};
use std::collections::BTreeMap;
use std::fs::File;
use std::path::{Path, PathBuf};
use std::sync::{Arc, RwLock};
use unicase::UniCase;

fn clone_buffer(buf: &mem::Buffer) -> Result<mem::Buffer, Error> {
    let vmo_rights = zx::Rights::BASIC | zx::Rights::READ | zx::Rights::MAP;
    let vmo = buf.vmo.duplicate_handle(vmo_rights)?;
    Ok(mem::Buffer {
        vmo,
        size: buf.size,
    })
}

struct Asset {
    path: PathBuf,
    buffer: RwLock<Option<mem::Buffer>>,
}

struct AssetCollection {
    assets_map: BTreeMap<PathBuf, u32>,
    assets: Vec<Asset>,
}

fn load_asset_to_vmo(path: &Path) -> Result<mem::Buffer, Error> {
    let file = File::open(path)?;
    let vmo = fdio::get_vmo_copy_from_file(&file)?;
    let size = file.metadata()?.len();
    Ok(mem::Buffer { vmo, size })
}

impl AssetCollection {
    fn new() -> AssetCollection {
        AssetCollection {
            assets_map: BTreeMap::new(),
            assets: vec![],
        }
    }

    fn add_or_get_asset_id(&mut self, path: &Path) -> u32 {
        if let Some(id) = self.assets_map.get(path) {
            return *id;
        }

        let id = self.assets.len() as u32;
        self.assets.push(Asset {
            path: path.to_path_buf(),
            buffer: RwLock::new(None),
        });
        self.assets_map.insert(path.to_path_buf(), id);
        id
    }

    fn get_asset(&self, id: u32) -> Result<mem::Buffer, Error> {
        assert!(id < self.assets.len() as u32);

        let asset = &self.assets[id as usize];

        if let Some(cached) = asset.buffer.read().unwrap().as_ref() {
            return clone_buffer(cached);
        }

        let buf = load_asset_to_vmo(&asset.path)?;
        let buf_clone = clone_buffer(&buf)?;
        *asset.buffer.write().unwrap() = Some(buf);
        Ok(buf_clone)
    }

    fn reset_cache(&mut self) {
        for asset in self.assets.iter_mut() {
            *asset.buffer.write().unwrap() = None;
        }
    }
}

struct FontFamily {
    fonts: FontCollection,
    fallback_group: fonts::FallbackGroup,
}

impl FontFamily {
    fn new(fallback_group: fonts::FallbackGroup) -> FontFamily {
        FontFamily {
            fonts: FontCollection::new(),
            fallback_group,
        }
    }
}

pub enum FamilyOrAlias {
    Family(FontFamily),
    Alias(UniCase<String>),
}

pub struct FontService {
    assets: AssetCollection,
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

    // Verifies that we have a reasonable font configuration and can start.
    pub fn check_can_start(&self) -> Result<(), Error> {
        if self.fallback_collection.is_empty() {
            return Err(format_err!("Need at least one fallback font"));
        }

        Ok(())
    }

    pub fn load_manifest(&mut self, manifest_path: &Path) -> Result<(), Error> {
        let manifest = manifest::FontsManifest::load_from_file(&manifest_path)?;
        self.add_fonts_from_manifest(manifest).with_context(|_| {
            format!(
                "Failed to load fonts from {}",
                manifest_path.to_string_lossy()
            )
        })?;

        Ok(())
    }

    fn add_fonts_from_manifest(
        &mut self, mut manifest: manifest::FontsManifest,
    ) -> Result<(), Error> {
        let font_info_loader = font_info::FontInfoLoader::new()?;

        for mut family_manifest in manifest.families.drain(..) {
            if family_manifest.fonts.is_empty() {
                continue;
            }

            let family_name = UniCase::new(family_manifest.family.clone());

            let family = match self.families.entry(family_name.clone()).or_insert_with(|| {
                FamilyOrAlias::Family(FontFamily::new(family_manifest.fallback_group))
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
                let asset_id = self
                    .assets
                    .add_or_get_asset_id(font_manifest.asset.as_path());

                let buffer = self.assets.get_asset(asset_id).with_context(|_| {
                    format!(
                        "Failed to load font from {}",
                        font_manifest.asset.to_string_lossy()
                    )
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
                    Some(FamilyOrAlias::Family(f)) => {
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

    fn match_request(&self, mut request: fonts::Request) -> Result<fonts::Response, Error> {
        for lang in request.language.iter_mut() {
            *lang = lang.to_ascii_lowercase();
        }

        let family_name = UniCase::new(request.family.clone());
        let matched_family = self.families.get(&family_name).map(|f| match f {
            FamilyOrAlias::Family(f) => f,
            FamilyOrAlias::Alias(a) => match self.families.get(a) {
                Some(FamilyOrAlias::Family(f)) => f,
                _ => panic!("Invalid font alias."),
            },
        });

        let mut font = None;

        if let Some(family) = matched_family {
            font = family.fonts.match_request(&request)
        }

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

        let response = match font {
            Some(f) => fonts::Response {
                buffer: self.assets.get_asset(f.asset_id)?,
                buffer_id: f.asset_id,
                font_index: f.font_index,
            },
            None => fonts::Response {
                buffer: mem::Buffer {
                    vmo: zx::Vmo::from_handle(zx::Handle::invalid()),
                    size: 0,
                },
                buffer_id: 0,
                font_index: 0,
            },
        };

        Ok(response)
    }

    fn handle_font_provider_request(
        &self, request: fonts::ProviderRequest,
    ) -> impl Future<Output = Result<(), fidl::Error>> {
        match request {
            fonts::ProviderRequest::GetFont { request, responder } => {
                // TODO(sergeyu): Currently the service returns an empty response when
                // it fails to load a font. This matches behavior of the old
                // FontProvider implementation, but it isn't the right thing to do.
                let mut response = self.match_request(request).ok();
                future::ready(responder.send(response.as_mut().map(OutOfLine)))
            }
        }
    }
}

pub fn spawn_server(font_service: Arc<FontService>, chan: fasync::Channel) {
    // TODO(sergeyu): Consider making handle_font_provider_request() and
    // load_asset_to_vmo() asynchronous and using try_for_each_concurrent()
    // instead of try_for_each() here. That would be useful only if clients can
    // send more than one concurrent request.
    let stream_complete = fonts::ProviderRequestStream::from_channel(chan)
        .try_for_each(move |request| font_service.handle_font_provider_request(request))
        .map(|_| ());
    fasync::spawn(stream_complete);
}
