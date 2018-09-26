// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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

pub type LanguageSet = BTreeSet<String>;

struct Font {
    asset_id: u32,
    font_index: u32,
    slant: fonts::Slant,
    weight: u32,
    _width: u32,
    language: LanguageSet,
    info: font_info::FontInfo,
    fallback_group: fonts::FallbackGroup,
}

impl Font {
    fn new(
        asset_id: u32, manifest: manifest::Font, info: font_info::FontInfo,
        fallback_group: fonts::FallbackGroup,
    ) -> Font {
        Font {
            asset_id,
            font_index: manifest.index,
            weight: manifest.weight,
            _width: manifest.width,
            slant: manifest.slant,
            language: manifest.language.iter().map(|x| x.clone()).collect(),
            info,
            fallback_group,
        }
    }
}

struct FontCollection {
    // Some fonts may be in more than one collections. Particularly fallback fonts
    // are added to the family collection and also to the fallback collection.
    fonts: Vec<Arc<Font>>,
}

impl FontCollection {
    fn new() -> FontCollection {
        FontCollection { fonts: vec![] }
    }

    fn match_request<'a>(&'a self, request: &fonts::Request) -> Option<&'a Font> {
        self.fonts
            .iter()
            .filter(|f| (request.character == 0) | f.info.charset.contains(request.character))
            .min_by_key(|f| compute_font_request_score(f, request))
            .map(|a| a.as_ref())
    }

    fn is_empty(&self) -> bool {
        self.fonts.is_empty()
    }

    fn add_font(&mut self, font: Arc<Font>) {
        self.fonts.push(font);
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
            fallback_group: fallback_group,
        }
    }
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

    if (request.fallback_group != fonts::FallbackGroup::None)
        & (request.fallback_group != font.fallback_group)
    {
        score += 4000;
    }

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

pub struct FontService {
    assets: AssetCollection,
    families: BTreeMap<String, FontFamily>,
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
        for mut family_manifest in manifest.families.drain(..) {
            if family_manifest.fonts.is_empty() {
                continue;
            }

            let family = self
                .families
                .entry(family_manifest.family.clone())
                .or_insert_with(|| FontFamily::new(family_manifest.fallback_group));

            let font_info_loader = font_info::FontInfoLoader::new()?;

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

                // Unsafe because in VMOs may be resized while being mapped and
                // load_font_info_from_vmo() doesn't handle this case.
                // fidl::get_vmo_copy_from_file() allocates a new VMO or gets it
                // from the file system. pkgfs won't resize the VMO, so this can
                // be unsafe only if font_server is started with a custom
                // manifest file that refers to files that are not on pkgs.
                let info = unsafe {
                    font_info_loader
                        .load_font_info_from_vmo(
                            &buffer.vmo,
                            buffer.size as usize,
                            font_manifest.index,
                        ).with_context(|_| {
                            format!(
                                "Failed to load font info from {}",
                                font_manifest.asset.to_string_lossy()
                            )
                        })?
                };
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
        }

        // Flush the cache. Font files will be loaded again when they are needed.
        self.assets.reset_cache();

        Ok(())
    }

    fn match_request(&self, mut request: fonts::Request) -> Result<fonts::Response, Error> {
        let mut font = self.families.get(&request.family).and_then(|family| {
            if request.fallback_group == fonts::FallbackGroup::None {
                request.fallback_group = family.fallback_group;
            }
            family.fonts.match_request(&request)
        });

        if (request.flags & fonts::REQUEST_FLAG_NO_FALLBACK) == 0 {
            font = font.or_else(|| self.fallback_collection.match_request(&request));
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
