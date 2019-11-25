// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod asset;
mod family;
mod typeface;

use {
    self::{
        asset::Collection as AssetCollection,
        family::{FamilyOrAlias, FontFamily},
        typeface::{Collection as TypefaceCollection, Typeface, TypefaceInfoAndCharSet},
    },
    fuchsia_syslog::*,
    failure::{format_err, Error, ResultExt},
    fidl::{
        self,
        encoding::{Decodable, OutOfLine},
        endpoints::ServerEnd,
    },
    fidl_fuchsia_fonts as fonts, fidl_fuchsia_fonts_experimental as fonts_exp,
    fidl_fuchsia_fonts_ext::{FontFamilyInfoExt, RequestExt, TypefaceResponseExt},
    font_info::FontInfoLoaderImpl,
    fuchsia_async as fasync,
    fuchsia_component::server::{ServiceFs, ServiceObj},
    futures::prelude::*,
    itertools::Itertools,
    manifest::FontsManifest,
    std::{collections::BTreeMap, iter, path::Path, sync::Arc},
    unicase::UniCase,
};

/// Get a field out of a `TypefaceRequest`'s `query` field as a reference, or returns early with a
/// `failure::Error` if the query is missing.
macro_rules! query_field {
    ($request:ident, $field:ident) => {
        $request.query.as_ref().ok_or(format_err!("Missing query"))?.$field.as_ref()
    };
}

pub enum ProviderRequestStream {
    Stable(fonts::ProviderRequestStream),
    Experimental(fonts_exp::ProviderRequestStream),
}

pub struct FontService {
    assets: AssetCollection,
    /// Maps the font family name from the manifest (`families.family`) to a FamilyOrAlias.
    families: BTreeMap<UniCase<String>, FamilyOrAlias>,
    fallback_collection: TypefaceCollection,
}

impl FontService {
    pub fn new() -> FontService {
        FontService {
            assets: AssetCollection::new(),
            families: BTreeMap::new(),
            fallback_collection: TypefaceCollection::new(),
        }
    }

    /// Verify that we have a reasonable font configuration and can start.
    pub fn check_can_start(&self) -> Result<(), Error> {
        if self.fallback_collection.is_empty() {
            return Err(format_err!("Need at least one fallback font"));
        }

        Ok(())
    }

    pub async fn load_manifest(&mut self, manifest_path: &Path) -> Result<(), Error> {
        fx_vlog!(1, "Loading manifest {:?}", manifest_path);
        let manifest = FontsManifest::load_from_file(&manifest_path)?;
        self.add_fonts_from_manifest(manifest).await.with_context(|_| {
            format!("Failed to load fonts from {}", manifest_path.to_string_lossy())
        })?;

        Ok(())
    }

    async fn add_fonts_from_manifest(&mut self, mut manifest: FontsManifest) -> Result<(), Error> {
        let font_info_loader = FontInfoLoaderImpl::new()?;

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
                    family_manifest.generic_family,
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

            for mut font_manifest in family_manifest.fonts.drain(..) {
                let asset_path = font_manifest.asset.as_path();
                let asset_id =
                    self.assets.add_or_get_asset_id(asset_path, font_manifest.package.as_ref());

                // Read `code_points` from file if not provided by manifest.
                if font_manifest.code_points.is_empty() {
                    if !asset_path.exists() {
                        return Err(format_err!(
                            "Unable to load code point info for '{}'. Manifest entry has no \
                             code_points field and the file does not exist.",
                            asset_path.to_string_lossy(),
                        ));
                    }

                    let buffer = self.assets.get_asset(asset_id).await.with_context(|_| {
                        format!("Failed to load font from {}", asset_path.to_string_lossy())
                    })?;

                    let info = font_info_loader
                        .load_font_info(buffer, font_manifest.index)
                        .with_context(|_| {
                            format!(
                                "Failed to load font info from {}",
                                asset_path.to_string_lossy()
                            )
                        })?;

                    font_manifest.code_points = info.char_set;
                }

                let typeface = Arc::new(Typeface::new(
                    asset_id,
                    font_manifest,
                    family_manifest.generic_family,
                )?);
                family.faces.add_typeface(typeface.clone());
                if family_manifest.fallback {
                    self.fallback_collection.add_typeface(typeface);
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
        Ok(())
    }

    fn resolve_alias<'a>(&'a self, name: &'a FamilyOrAlias) -> Option<&'a FontFamily> {
        match name {
            FamilyOrAlias::Family(f) => Some(f),
            FamilyOrAlias::Alias(a) => match self.families.get(a) {
                Some(FamilyOrAlias::Family(f)) => Some(f),
                _ => None,
            },
        }
    }

    /// Get font family by name.
    fn match_family(&self, family_name: &UniCase<String>) -> Option<&FontFamily> {
        self.resolve_alias(self.families.get(family_name)?)
    }

    /// Get all font families whose name contains the requested string
    fn match_families_substr(&self, family_name: String) -> impl Iterator<Item = &FontFamily> {
        self.families
            .iter()
            .filter_map(move |(key, value)| {
                // Note: This might not work for some non-Latin strings
                if key.as_ref().to_lowercase().contains(&family_name.to_lowercase()) {
                    return self.resolve_alias(value);
                }
                None
            })
            .unique_by(|family| &family.name)
    }

    async fn match_request(
        &self,
        mut request: fonts::TypefaceRequest,
    ) -> Result<fonts::TypefaceResponse, Error> {
        let matched_family: Option<&FontFamily> = query_field!(request, family)
            .and_then(|family| self.match_family(&UniCase::new(family.name.clone())));
        let mut typeface = match matched_family {
            Some(family) => family.faces.match_request(&request)?,
            None => None,
        };

        // If an exact match wasn't found, but fallback families are allowed...
        if typeface.is_none()
            && request
                .flags
                .map_or(true, |flags| !flags.contains(fonts::TypefaceRequestFlags::ExactFamily))
        {
            // If fallback_family is not specified by the client explicitly then copy it from
            // the matched font family.
            if query_field!(request, fallback_family).is_none() {
                if let Some(family) = matched_family {
                    request
                        .query
                        .as_mut()
                        .ok_or_else(|| format_err!("This should never happen"))?
                        .fallback_family = family.generic_family;
                }
            }
            typeface = self.fallback_collection.match_request(&request)?;
        }

        let typeface_response = match typeface {
            Some(font) => self
                .assets
                .get_asset(font.asset_id)
                .await
                .and_then(|buffer| {
                    Ok(fonts::TypefaceResponse {
                        buffer: Some(buffer),
                        buffer_id: Some(font.asset_id),
                        font_index: Some(font.font_index),
                    })
                })
                .unwrap_or_else(|_| fonts::TypefaceResponse::new_empty()),
            None => {
                if let Some(code_points) = query_field!(request, code_points) {
                    fx_log_err!("Missing code points: {:?}", code_points);
                }
                fonts::TypefaceResponse::new_empty()
            }
        };

        // Note that not finding a typeface is not an error, as long as the query was legal.
        Ok(typeface_response)
    }

    fn get_family_info(&self, family_name: fonts::FamilyName) -> fonts::FontFamilyInfo {
        let family_name = UniCase::new(family_name.name);
        let family = self.match_family(&family_name);
        family.map_or_else(
            || fonts::FontFamilyInfo::new_empty(),
            |family| fonts::FontFamilyInfo {
                name: Some(fonts::FamilyName { name: family.name.clone() }),
                styles: Some(family.faces.get_styles().collect()),
            },
        )
    }

    async fn get_typeface_by_id(
        &self,
        id: u32,
    ) -> Result<fonts::TypefaceResponse, fonts_exp::Error> {
        match self.assets.get_asset(id).await {
            Ok(buffer) => {
                let response = fonts::TypefaceResponse {
                    buffer: Some(buffer),
                    buffer_id: Some(id),
                    font_index: None,
                };
                Ok(response)
            }
            Err(e) => {
                let msg = e.as_fail().to_string();
                if msg.starts_with("No asset found") {
                    return Err(fonts_exp::Error::NotFound);
                }
                Err(fonts_exp::Error::Internal)
            }
        }
    }

    fn get_typefaces_by_family(
        &self,
        family_name: fonts::FamilyName,
    ) -> Result<fonts_exp::TypefaceInfoResponse, fonts_exp::Error> {
        let family = self
            .match_family(&UniCase::new(family_name.name.clone()))
            .ok_or(fonts_exp::Error::NotFound)?;
        let faces = family.extract_faces().map_into().collect();
        let response = fonts_exp::TypefaceInfoResponse { results: Some(faces) };
        Ok(response)
    }

    /// Helper that runs the "match by family name" step of [`list_typefaces`].
    /// Returns a vector of all available font families whose name or alias equals (or contains, if
    /// the `MatchFamilyNameSubstring` flag is set) the name requested in `query`.
    /// If `query` or `query.family` is `None`, all families are matched.
    fn list_typefaces_match_families<'a>(
        &'a self,
        flags: fonts_exp::ListTypefacesFlags,
        request: &fonts_exp::ListTypefacesRequest,
    ) -> Box<dyn Iterator<Item = &FontFamily> + 'a> {
        match request.family.as_ref() {
            Some(fonts::FamilyName { name }) => {
                if flags.contains(fonts_exp::ListTypefacesFlags::MatchFamilyNameSubstring) {
                    Box::new(self.match_families_substr(name.clone()))
                } else {
                    match self.match_family(&UniCase::new(name.clone())) {
                        Some(matched) => Box::new(iter::once(matched)),
                        None => Box::new(iter::empty()),
                    }
                }
            }
            None => Box::new(self.families.iter().filter_map(move |(_, value)| match value {
                FamilyOrAlias::Family(family) => Some(family),
                FamilyOrAlias::Alias(_) => None,
            })),
        }
    }

    fn list_typefaces_inner(
        &self,
        request: fonts_exp::ListTypefacesRequest,
    ) -> Result<Vec<fonts_exp::TypefaceInfo>, fonts_exp::Error> {
        let flags = request.flags.unwrap_or(fonts_exp::ListTypefacesFlags::new_empty());

        let matched_families = self.list_typefaces_match_families(flags, &request);

        // Flatten matches into Iter<TypefaceInfoAndCharSet>
        let matched_faces = matched_families.flat_map(|family| family.extract_faces());

        let slant_predicate = |face: &TypefaceInfoAndCharSet| -> bool {
            match request.slant {
                Some(fonts_exp::SlantRange { lower, upper }) => {
                    // Unwrap is safe because manifest loading assigns default values if needed
                    (lower..=upper).contains(&face.style.slant.unwrap())
                }
                None => true,
            }
        };

        let weight_predicate = |face: &TypefaceInfoAndCharSet| -> bool {
            match request.weight {
                Some(fonts_exp::WeightRange { lower, upper }) => {
                    // Unwrap is safe because manifest loading assigns default values if needed
                    (lower..=upper).contains(&face.style.weight.unwrap())
                }
                None => true,
            }
        };

        let width_predicate = |face: &TypefaceInfoAndCharSet| -> bool {
            match request.width {
                Some(fonts_exp::WidthRange { lower, upper }) => {
                    // Unwrap is safe because manifest loading assigns default values if needed
                    (lower..=upper).contains(&face.style.width.unwrap())
                }
                None => true,
            }
        };

        let lang_predicate = |face: &TypefaceInfoAndCharSet| -> bool {
            match request.languages.as_ref() {
                // This is O(face_langs.len() * fonts.MAX_FACE_QUERY_LANGUAGES). As of 06/2019,
                // MAX_FACE_QUERY_LANGAUGES == 8. face_langs.len() *should* be small as well.
                Some(langs) => langs.iter().all(|lang| face.languages.contains(&lang)),
                None => true,
            }
        };

        let code_point_predicate = |face: &TypefaceInfoAndCharSet| -> bool {
            match request.code_points.as_ref() {
                Some(points) => points.iter().all(|point| face.char_set.contains(*point)),
                None => true,
            }
        };

        let generic_family_predicate = |face: &TypefaceInfoAndCharSet| -> bool {
            match request.generic_family.as_ref() {
                Some(generic_family) => {
                    face.generic_family.map_or(false, |gf| generic_family == &gf)
                }
                None => true,
            }
        };

        let total_predicate = |face: &TypefaceInfoAndCharSet| -> bool {
            slant_predicate(face)
                && weight_predicate(face)
                && width_predicate(face)
                && lang_predicate(face)
                && code_point_predicate(face)
                && generic_family_predicate(face)
        };

        // Filter
        let matched_faces = matched_faces.filter(total_predicate).map_into().collect();

        Ok(matched_faces)
    }

    fn list_typefaces(
        &self,
        request: fonts_exp::ListTypefacesRequest,
        iterator: ServerEnd<fonts_exp::ListTypefacesIteratorMarker>,
    ) -> Result<(), fonts_exp::Error> {
        let mut results = self.list_typefaces_inner(request)?;

        fasync::spawn(
            async move {
                let mut stream = iterator.into_stream()?;
                while let Some(request) = stream.try_next().await? {
                    match request {
                        fonts_exp::ListTypefacesIteratorRequest::GetNext { responder } => {
                            let split_at =
                                (fonts_exp::MAX_TYPEFACE_RESULTS as usize).min(results.len());
                            // Return results in order
                            let chunk = results.drain(..split_at).collect_vec();
                            let response = fonts_exp::TypefaceInfoResponse { results: Some(chunk) };
                            responder.send(response)?;
                        }
                    }
                }
                Ok(())
            }
                .unwrap_or_else(|e: Error| {
                    fx_log_err!("Error while running ListTypefacesIterator: {:?}", e)
                }),
        );

        Ok(())
    }

    async fn handle_font_provider_request(
        &self,
        request: fonts::ProviderRequest,
    ) -> Result<(), Error> {
        use fonts::ProviderRequest::*;

        match request {
            // TODO(I18N-12): Remove when all clients have migrated to GetTypeface
            GetFont { request, responder } => {
                let request = request.into_typeface_request();
                let mut response = self.match_request(request).await?.into_font_response();
                Ok(responder.send(response.as_mut().map(OutOfLine))?)
            }
            // TODO(I18N-12): Remove when all clients have migrated to GetFontFamilyInfo
            GetFamilyInfo { family, responder } => {
                let mut font_info =
                    self.get_family_info(fonts::FamilyName { name: family }).into_family_info();
                Ok(responder.send(font_info.as_mut().map(OutOfLine))?)
            }
            GetTypeface { request, responder } => {
                let response = self.match_request(request).await?;
                // TODO(kpozin): OutOfLine?
                Ok(responder.send(response)?)
            }
            GetFontFamilyInfo { family, responder } => {
                let family_info = self.get_family_info(family);
                // TODO(kpozin): OutOfLine?
                Ok(responder.send(family_info)?)
            }
            // TODO(34897): Implement font event dispatch
            RegisterFontSetEventListener { listener: _, responder: _ } => unimplemented!(),
        }
    }

    async fn handle_experimental_request(
        &self,
        request: fonts_exp::ProviderRequest,
    ) -> Result<(), Error> {
        use fonts_exp::ProviderRequest::*;

        match request {
            GetTypefaceById { id, responder } => {
                let mut response = self.get_typeface_by_id(id).await;
                Ok(responder.send(&mut response)?)
            }
            GetTypefacesByFamily { family, responder } => {
                let mut response = self.get_typefaces_by_family(family);
                Ok(responder.send(&mut response)?)
            }
            ListTypefaces { request, iterator, responder } => {
                let mut response = self.list_typefaces(request, iterator);
                Ok(responder.send(&mut response)?)
            }
        }
    }

    pub async fn run(self, fs: ServiceFs<ServiceObj<'static, ProviderRequestStream>>) {
        let self_ = Arc::new(self);
        fs.for_each_concurrent(None, move |stream| self_.clone().handle_stream(stream)).await;
    }

    async fn handle_stream(self: Arc<Self>, stream: ProviderRequestStream) {
        let self_ = self.clone();
        match stream {
            ProviderRequestStream::Stable(stream) => {
                self_.as_ref().handle_stream_stable(stream).await.unwrap_or_default()
            }
            ProviderRequestStream::Experimental(stream) => {
                self_.as_ref().handle_stream_experimental(stream).await.unwrap_or_default()
            }
        }
    }

    async fn handle_stream_stable(
        &self,
        mut stream: fonts::ProviderRequestStream,
    ) -> Result<(), Error> {
        while let Some(request) = stream.try_next().await.context("Error running provider")? {
            self.handle_font_provider_request(request)
                .await
                .context("Error while handling request")?;
        }
        Ok(())
    }

    async fn handle_stream_experimental(
        &self,
        mut stream: fonts_exp::ProviderRequestStream,
    ) -> Result<(), Error> {
        while let Some(request) = stream.try_next().await.context("Error running provider")? {
            self.handle_experimental_request(request)
                .await
                .context("Error while handling request")?;
        }
        Ok(())
    }
}
