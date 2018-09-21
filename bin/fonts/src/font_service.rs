// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::font_info;
use super::manifest::FontsManifest;
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
use std::collections::{BTreeMap, BTreeSet};
use std::fs::File;
use std::path::{Path, PathBuf};
use std::sync::{Arc, RwLock};

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

    fn add_or_get_asset_id(&mut self, path: PathBuf) -> u32 {
        if let Some(id) = self.assets_map.get(&path) {
            return *id;
        }

        let id = self.assets.len() as u32;
        self.assets.push(Asset {
            path: path.clone(),
            buffer: RwLock::new(None),
        });
        self.assets_map.insert(path, id);
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

pub type LanguageSet = BTreeSet<String>;

struct Font {
    asset_id: u32,
    font_index: u32,
    slant: fonts::Slant,
    weight: u32,
    _width: u32,
    language: LanguageSet,
    info: font_info::FontInfo,
}

struct FontFamily {
    _name: String,
    fonts: Vec<Font>,
}

fn abs_diff(a: u32, b: u32) -> u32 {
    a.max(b) - a.min(b)
}

// TODO(US-409): Implement a better font-matching algorithm which:
//   - follows CSS3 font style matching rules,
//   - prioritizes fonts according to the language order in the request,
//   - allows partial language match
fn compute_font_request_score(font: &Font, request: &fonts::Request) -> u32 {
    let mut score = 0;

    let char_matches = request.character != 0 && font.info.charset.contains(request.character);
    score += 4000 * (!char_matches) as u32;

    let language_matches = request
        .language
        .iter()
        .find(|lang| font.language.contains(*lang))
        .is_some();
    score += 2000 * (!language_matches) as u32;

    score += (font.slant != request.slant) as u32 * 1000;
    score += abs_diff(request.weight, font.weight);

    score
}

impl FontFamily {
    fn find_best_match<'a>(&'a self, request: &fonts::Request) -> &'a Font {
        self.fonts
            .iter()
            .min_by_key(|f| compute_font_request_score(f, request))
            .unwrap()
    }
}

struct FontCollection {
    fallback_family: String,
    assets: AssetCollection,
    families: BTreeMap<String, FontFamily>,
}

impl FontCollection {
    fn new() -> FontCollection {
        FontCollection {
            fallback_family: String::new(),
            families: BTreeMap::new(),
            assets: AssetCollection::new(),
        }
    }

    fn add_from_manifest(&mut self, mut manifest: FontsManifest) -> Result<(), Error> {
        for mut family_manifest in manifest.families.drain(..) {
            if family_manifest.fonts.is_empty() {
                continue;
            }

            let family = self
                .families
                .entry(family_manifest.family.clone())
                .or_insert_with(|| FontFamily {
                    _name: family_manifest.family.clone(),
                    fonts: vec![],
                });

            let font_info_loader = font_info::FontInfoLoader::new()?;

            for font in family_manifest.fonts.drain(..) {
                let asset_id = self.assets.add_or_get_asset_id(font.asset.clone());

                let buffer = self.assets.get_asset(asset_id).with_context(|_| {
                    format!("Failed to load font from {}", font.asset.to_string_lossy())
                })?;

                // Unsafe because in VMOs may be resized while being mapped and
                // load_font_info_from_vmo() doesn't handle this case.
                // fidl::get_vmo_copy_from_file() allocates a new VMO or gets it
                // from the file system. pkgfs won't resize the VMO, so this can
                // be unsafe only if font_server is started with a custom
                // manifest file that refers to files that are not on pkgs.
                let info = unsafe {
                    font_info_loader
                        .load_font_info_from_vmo(&buffer.vmo, buffer.size as usize, font.index)
                        .with_context(|_| {
                            format!(
                                "Failed to load font info from {}",
                                font.asset.to_string_lossy()
                            )
                        })?
                };
                family.fonts.push(Font {
                    asset_id,
                    font_index: font.index,
                    weight: font.weight,
                    _width: font.width,
                    slant: font.slant,
                    language: font.language.iter().map(|x| x.clone()).collect(),
                    info,
                });
            }
        }

        if let Some(fallback) = manifest.fallback {
            if !self.families.contains_key(&fallback) {
                return Err(format_err!(
                    "Font manifest contains invalid fallback family {}",
                    fallback
                ));
            }
            self.fallback_family = fallback;
        }

        // Flush the cache. Font files will be loaded again when they are needed.
        self.assets.reset_cache();

        Ok(())
    }

    fn get_fallback_family<'a>(&'a self) -> &'a FontFamily {
        // fallback_family is expected to be always valid.
        self.families.get(&self.fallback_family).unwrap()
    }

    fn find_best_match(&self, request: &fonts::Request) -> Result<fonts::Response, Error> {
        let family = self
            .families
            .get(&request.family)
            .unwrap_or_else(|| self.get_fallback_family());
        let font = family.find_best_match(request);

        Ok(fonts::Response {
            buffer: self.assets.get_asset(font.asset_id)?,
            buffer_id: font.asset_id,
            font_index: font.font_index,
        })
    }
}

pub struct FontService {
    font_collection: FontCollection,
}

impl FontService {
    pub fn new(manifests: &[PathBuf]) -> Result<FontService, Error> {
        let mut font_collection = FontCollection::new();
        for manifest_path in manifests {
            let manifest = FontsManifest::load_from_file(&manifest_path)
                .with_context(|_| format!("Failed to load {}", manifest_path.to_string_lossy()))?;
            font_collection
                .add_from_manifest(manifest)
                .with_context(|_| {
                    format!(
                        "Failed to load fonts from {}",
                        manifest_path.to_string_lossy()
                    )
                })?;
        }

        if font_collection.fallback_family == "" {
            return Err(format_err!(
                "Font manifest didn't contain a valid fallback family."
            ));
        }

        Ok(FontService { font_collection })
    }

    fn handle_font_provider_request(
        &self, request: fonts::ProviderRequest,
    ) -> impl Future<Output = Result<(), fidl::Error>> {
        match request {
            fonts::ProviderRequest::GetFont { request, responder } => {
                // TODO(sergeyu): Currently the service returns an empty response when
                // it fails to load a font. This matches behavior of the old
                // FontProvider implementation, but it isn't the right thing to do.
                let mut response = self.font_collection.find_best_match(&request).ok();
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
