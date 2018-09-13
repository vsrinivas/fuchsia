// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::manifest::FontsManifest;
use failure::{format_err, Error, ResultExt};
use fdio;
use fidl;
use fidl::encoding2::OutOfLine;
use fidl::endpoints2::RequestStream;
use fidl_fuchsia_fonts as fonts;
use fidl_fuchsia_mem as mem;
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use fuchsia_zircon::HandleBased;
use futures::prelude::*;
use futures::{future, Future, FutureExt};
use std::collections::BTreeMap;
use std::fs::File;
use std::io::Seek;
use std::path::Path;
use std::sync::{Arc, RwLock};

fn clone_buffer(buf: &mem::Buffer) -> Result<mem::Buffer, Error> {
    let vmo_rights = zx::Rights::BASIC | zx::Rights::READ | zx::Rights::MAP;
    let vmo = buf.vmo.duplicate_handle(vmo_rights)?;
    Ok(mem::Buffer {
        vmo,
        size: buf.size,
    })
}

struct Font {
    asset: String,
    slant: fonts::Slant,
    weight: u32,
    buffer: RwLock<Option<mem::Buffer>>,
}

fn load_asset_to_vmo(path: &str) -> Result<mem::Buffer, Error> {
    let mut file = File::open(path)?;
    let vmo = fdio::get_vmo_copy_from_file(&file)?;

    // TODO(US-527): seek() is used to get file size instead of
    // file.metadata().len() because libc::stat() is currently broken on arm64.
    let size = file.seek(std::io::SeekFrom::End(0))?;

    Ok(mem::Buffer { vmo, size })
}

impl Font {
    fn get_font_data(&self) -> Result<mem::Buffer, Error> {
        let cache_clone = self
            .buffer
            .read()
            .unwrap()
            .as_ref()
            .map(|b| clone_buffer(b));
        let buffer = match cache_clone {
            Some(buffer) => buffer?,
            None => {
                let buf = load_asset_to_vmo(self.asset.as_str())?;
                let buf_clone = clone_buffer(&buf)?;
                *self.buffer.write().unwrap() = Some(buf);
                buf_clone
            }
        };

        Ok(buffer)
    }
}

struct FontFamily {
    name: String,
    fonts: Vec<Font>,
}

fn abs_diff(a: u32, b: u32) -> u32 {
    a.max(b) - a.min(b)
}

fn compute_font_request_score(font: &Font, request: &fonts::Request) -> u32 {
    let slant_score = (font.slant != request.slant) as u32 * 1000;
    let weight_score = abs_diff(request.weight, font.weight);
    slant_score + weight_score
}

impl FontFamily {
    fn find_best_match(&self, request: &fonts::Request) -> Result<mem::Buffer, Error> {
        self.fonts
            .iter()
            .min_by_key(|f| compute_font_request_score(f, request))
            .unwrap()
            .get_font_data()
    }
}

struct FontCollection {
    fallback_family: String,
    families: BTreeMap<String, FontFamily>,
}

impl FontCollection {
    fn from_manifest(manifest: FontsManifest) -> Result<FontCollection, Error> {
        let mut result = FontCollection {
            fallback_family: String::new(),
            families: BTreeMap::new(),
        };
        result.add_from_manifest(manifest)?;
        if result.fallback_family == "" {
            return Err(format_err!(
                "Font manifest didn't contain a valid fallback family."
            ));
        }
        Ok(result)
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
                    name: family_manifest.family.clone(),
                    fonts: vec![],
                });

            for font in family_manifest.fonts.drain(..) {
                family.fonts.push(Font {
                    asset: font.asset,
                    slant: font.slant,
                    weight: font.weight,
                    buffer: RwLock::new(None),
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

        Ok(())
    }

    fn get_fallback_family<'a>(&'a self) -> &'a FontFamily {
        // fallback_family is expected to be always valid.
        self.families.get(&self.fallback_family).unwrap()
    }

    fn find_best_match(&self, request: &fonts::Request) -> Result<mem::Buffer, Error> {
        self.families
            .get(&request.family)
            .unwrap_or_else(|| self.get_fallback_family())
            .find_best_match(request)
    }
}

const FONT_MANIFEST_PATH: &str = "/pkg/data/manifest.json";
const VENDOR_FONT_MANIFEST_PATH: &str = "/system/data/vendor/fonts/manifest.json";

fn load_fonts() -> Result<FontCollection, Error> {
    let mut collection = FontsManifest::load_from_file(FONT_MANIFEST_PATH)
        .and_then(|manifest| FontCollection::from_manifest(manifest))
        .context(format!("Failed to load {}", FONT_MANIFEST_PATH))?;
    if Path::new(VENDOR_FONT_MANIFEST_PATH).exists() {
        FontsManifest::load_from_file(VENDOR_FONT_MANIFEST_PATH)
            .and_then(|manifest| collection.add_from_manifest(manifest))
            .context(format!("Failed to load {}", VENDOR_FONT_MANIFEST_PATH))?;
    }
    Ok(collection)
}

pub struct FontService {
    font_collection: FontCollection,
}

impl FontService {
    pub fn new() -> Result<FontService, Error> {
        let font_collection = load_fonts().context("Failed to initialize font collection.")?;
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
                let buf = self.font_collection.find_best_match(&request).ok();
                let mut response = buf.map(|buffer| fonts::Response { buffer });
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
