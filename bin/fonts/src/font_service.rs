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
    width: u32,
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
            width: manifest.width,
            slant: manifest.slant,
            language: manifest
                .language
                .iter()
                .map(|x| x.to_ascii_lowercase())
                .collect(),
            info,
            fallback_group,
        }
    }
}

struct FontAndLangScore<'a> {
    font: &'a Font,
    lang_score: usize,
}

// Returns value in the range [0, 2*request_lang.len()]. First half is used
// for exact matches, the second half is for partial matches.
fn get_lang_match_score(font: &Font, request_lang: &[String]) -> usize {
    let mut best_partial_match_pos = None;
    for i in 0..request_lang.len() {
        let lang = &request_lang[i];

        // Iterate all language in the font that start with |lang|.
        for font_lang in font
            .language
            .range::<String, std::ops::RangeFrom<&String>>(lang..)
        {
            if !font_lang.starts_with(lang.as_str()) {
                break;
            }

            if font_lang.len() == lang.len() {
                // Exact match.
                return i;
            }

            // Partial match is valid only when it's followed by '-' character
            // (45 in ascii).
            if (font_lang.as_bytes()[lang.len()] == 45) & best_partial_match_pos.is_none() {
                best_partial_match_pos = Some(i);
                continue;
            }
        }
    }

    best_partial_match_pos.unwrap_or(request_lang.len()) + request_lang.len()
}

impl<'a> FontAndLangScore<'a> {
    fn new(font: &'a Font, request: &fonts::Request) -> FontAndLangScore<'a> {
        FontAndLangScore {
            font,
            lang_score: get_lang_match_score(font, &request.language),
        }
    }
}

const FONT_WIDTH_NORMAL: u32 = 5;

// Selects between fonts |a| and |b| for the |request|. Fonts are passed in
// FontAndLangScore so the language match score is calculated only once for each
// font. If |a| and |b| are equivalent then |a| is returned.
// The style matching logic follows the CSS3 Fonts spec (see Section 5.2,
// Item 4: https://www.w3.org/TR/css-fonts-3/#font-style-matching ) with 2
// additions:
//   1. Fonts with higher language match score are preferred. The score value
//      is expected to be pre-calculated by get_lang_match_score(). Note that if
//      the request specifies a character then the fonts are expected to be
//      already filtered based on that character, i.e. they both contain that
//      character, so this function doesn't need to verify it.
//   2. If the request specifies |fallback_group| then fonts with the same
//      |fallback_group| are preferred.
fn select_best_match<'a>(
    mut a: FontAndLangScore<'a>, mut b: FontAndLangScore<'a>, request: &'a fonts::Request,
) -> FontAndLangScore<'a> {
    if a.lang_score != b.lang_score {
        if a.lang_score < b.lang_score {
            return a;
        } else {
            return b;
        }
    }

    if (request.fallback_group != fonts::FallbackGroup::None)
        & (a.font.fallback_group != b.font.fallback_group)
    {
        if a.font.fallback_group == request.fallback_group {
            return a;
        } else if b.font.fallback_group == request.fallback_group {
            return b;
        }
        // If fallback_group of a and b doesn't match the request then
        // fall-through to compare them based on style parameters.
    }

    // Select based on width, see CSS3 Section 5.2, Item 4.a.
    if a.font.width != b.font.width {
        // Reorder a and b, so a has lower width.
        if a.font.width > b.font.width {
            std::mem::swap(&mut a, &mut b)
        }
        if request.width <= FONT_WIDTH_NORMAL {
            if b.font.width <= FONT_WIDTH_NORMAL {
                if (a.font.width as i32 - request.width as i32).abs()
                    < (b.font.width as i32 - request.width as i32).abs()
                {
                    return a;
                } else {
                    return b;
                }
            }
            return a;
        } else {
            if a.font.width > FONT_WIDTH_NORMAL {
                if (a.font.width as i32 - request.width as i32).abs()
                    < (b.font.width as i32 - request.width as i32).abs()
                {
                    return a;
                } else {
                    return b;
                }
            }
            return b;
        }
    }

    // Select based on slant, CSS3 Section 5.2, Item 4.b.
    match (request.slant, a.font.slant, b.font.slant) {
        // If both fonts have the same slant then fall through to select based
        // on weight.
        (_, a_s, b_s) if a_s == b_s => (),

        // If we have a font that matches the request then use it.
        (r_s, a_s, _) if r_s == a_s => return a,
        (r_s, _, b_s) if r_s == b_s => return b,

        // In case italic or oblique font is requested pick italic or
        // oblique.
        (fonts::Slant::Italic, fonts::Slant::Oblique, _) => return a,
        (fonts::Slant::Italic, _, fonts::Slant::Oblique) => return b,

        (fonts::Slant::Oblique, fonts::Slant::Italic, _) => return a,
        (fonts::Slant::Oblique, _, fonts::Slant::Italic) => return b,

        // In case upright font is requested, but we have only italic and
        // oblique then fall through to select based on weight.
        (fonts::Slant::Upright, _, _) => (),

        // Patterns above cover all possible inputs, but exhaustiveness
        // checker doesn't see it.
        _ => (),
    }

    // Select based on weight, CSS3 Section 5.2, Item 4.c.
    if a.font.weight != b.font.weight {
        // Reorder a and b, so a has lower weight.
        if a.font.weight > b.font.weight {
            std::mem::swap(&mut a, &mut b)
        }

        if a.font.weight == request.weight {
            return a;
        }
        if b.font.weight == request.weight {
            return a;
        }

        if request.weight < 400 {
            // If request.weight < 400, then fonts with
            // weights <= request.weight are preferred.
            if b.font.weight <= request.weight {
                return b;
            } else {
                return a;
            }
        } else if request.weight > 500 {
            // If request.weight > 500, then fonts with
            // weights >= request.weight are preferred.
            if a.font.weight >= request.weight {
                return a;
            } else {
                return b;
            }
        } else {
            // request.weight is 400 or 500.
            if b.font.weight <= 500 {
                if (a.font.weight as i32 - request.weight as i32).abs()
                    < (b.font.weight as i32 - request.weight as i32).abs()
                {
                    return a;
                } else {
                    return b;
                }
            } else {
                return a;
            }
        }
    }

    // If a and b are equivalent then give priority according to the order in
    // the manifest.
    a
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

    fn match_request<'a>(&'a self, request: &'a fonts::Request) -> Option<&'a Font> {
        if (request.flags & fonts::REQUEST_FLAG_EXACT_MATCH) > 0 {
            return self
                .fonts
                .iter()
                .find(|font| {
                    (font.width == request.width)
                        & (font.weight == request.weight)
                        & (font.slant == request.slant)
                        & (request.language.is_empty() || request
                            .language
                            .iter()
                            .find(|lang| font.language.contains(*lang))
                            .is_some())
                        & ((request.character == 0) | font.info.charset.contains(request.character))
                }).map(|f| f as &Font);
        }

        fn fold<'a>(
            best: Option<FontAndLangScore<'a>>, x: &'a Font, request: &'a fonts::Request,
        ) -> Option<FontAndLangScore<'a>> {
            let x = FontAndLangScore::new(x, request);
            match best {
                Some(b) => Some(select_best_match(b, x, request)),
                None => Some(x),
            }
        }

        self.fonts
            .iter()
            .filter(|f| (request.character == 0) | f.info.charset.contains(request.character))
            .fold(None, |best, x| fold(best, x, request))
            .map(|a| a.font)
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
        for lang in request.language.iter_mut() {
            *lang = lang.to_ascii_lowercase();
        }

        let matched_family = self.families.get(&request.family);

        let mut font = None;

        if let Some(family) = matched_family {
            font = family.fonts.match_request(&request)
        }

        if font.is_none() & ((request.flags & fonts::REQUEST_FLAG_NO_FALLBACK) == 0) {
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
