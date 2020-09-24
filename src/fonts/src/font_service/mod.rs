// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(missing_docs)]

mod asset;
mod builder;
mod family;
mod inspect;
mod typeface;

use {
    self::{
        asset::AssetCollection,
        family::{FamilyOrAlias, FontFamily, TypefaceQueryOverrides},
        typeface::{Collection as TypefaceCollection, TypefaceInfoAndCharSet},
    },
    anyhow::{format_err, Context as _, Error},
    fidl::{self, encoding::Decodable, endpoints::ServerEnd},
    fidl_fuchsia_fonts::{self as fonts, CacheMissPolicy},
    fidl_fuchsia_fonts_experimental as fonts_exp,
    fidl_fuchsia_fonts_ext::{
        FontFamilyInfoExt, RequestExt, TypefaceRequestExt, TypefaceResponseExt,
    },
    fidl_fuchsia_intl::LocaleId,
    fuchsia_async as fasync,
    fuchsia_component::server::{ServiceFs, ServiceObj},
    fuchsia_syslog::*,
    fuchsia_trace as trace,
    futures::prelude::*,
    itertools::Itertools,
    std::{collections::BTreeMap, iter, sync::Arc},
    unicase::UniCase,
};

pub use {
    asset::{AssetId, AssetLoader, AssetLoaderImpl},
    builder::FontServiceBuilder,
};

/// Get a field out of a `TypefaceRequest`'s `query` field as a reference, or returns early with a
/// `anyhow::Error` if the query is missing.
macro_rules! query_field {
    ($request:ident, $field:ident) => {
        $request.query.as_ref().ok_or(format_err!("Missing query"))?.$field.as_ref()
    };
}

pub enum ProviderRequestStream {
    Stable(fonts::ProviderRequestStream),
    Experimental(fonts_exp::ProviderRequestStream),
}

/// The result of a successful lookup of a font family by name. Combines a `FontFamily` and,
/// if the `FontFamilyAlias` that the client requested turned out to include
/// `TypefaceQueryOverrides` then also contains those overrides.
struct FontFamilyMatch<'a> {
    family: &'a FontFamily,
    overrides: Option<Arc<TypefaceQueryOverrides>>,
}

/// Maintains state and handles request streams for the font server.
#[derive(Debug)]
pub struct FontService<L>
where
    L: AssetLoader,
{
    assets: AssetCollection<L>,
    /// Maps the font family name from the manifest (`families[x].name`) to a FamilyOrAlias.
    families: BTreeMap<UniCase<String>, FamilyOrAlias>,
    fallback_collection: TypefaceCollection,
    /// Holds Inspect data about manifests, families, and the fallback collection.
    inspect_data: inspect::ServiceInspectData,
}

impl<L> FontService<L>
where
    L: AssetLoader,
{
    /// Resolves a font family or alias either to itself (if it's a family), or to the canonical
    /// family. If it's an alias and contains `TypefaceQueryOverrides`, then the resulting
    /// `FontFamilyMatch` will contain those overrides.
    fn resolve_alias<'a>(
        &'a self,
        family_or_alias: &'a FamilyOrAlias,
    ) -> Option<FontFamilyMatch<'a>> {
        match family_or_alias {
            FamilyOrAlias::Family(family) => Some(FontFamilyMatch { family, overrides: None }),
            FamilyOrAlias::Alias(name, overrides) => match self.families.get(name) {
                Some(FamilyOrAlias::Family(family)) => {
                    Some(FontFamilyMatch { family, overrides: overrides.clone() })
                }
                _ => None,
            },
        }
    }

    /// Get font family by name.
    fn match_family(&self, family_name: &UniCase<String>) -> Option<FontFamilyMatch<'_>> {
        self.resolve_alias(self.families.get(family_name)?)
    }

    /// Get all font families whose name contains the requested string
    fn match_families_substr(&self, family_name: String) -> impl Iterator<Item = &FontFamily> {
        self.families
            .iter()
            .filter_map(move |(key, value)| {
                // Note: This might not work for some non-Latin strings
                if key.as_ref().to_lowercase().contains(&family_name.to_lowercase()) {
                    return self.resolve_alias(value).map(|matched| matched.family);
                }
                None
            })
            .unique_by(|family| family.name.to_owned())
    }

    fn apply_query_overrides(
        mut request: fonts::TypefaceRequest,
        overrides: Arc<TypefaceQueryOverrides>,
    ) -> fonts::TypefaceRequest {
        match &mut request.query {
            Some(query) => {
                if overrides.has_style_overrides() {
                    // If query has no style at all, use the values from TypefaceQueryOverrides
                    match &mut query.style {
                        None => query.style = Some(overrides.style.clone().into()),
                        Some(style) => {
                            style.slant = style.slant.or(overrides.style.slant);
                            style.width = style.width.or(overrides.style.width);
                            style.weight = style.weight.or(overrides.style.weight);
                        }
                    }
                }

                if overrides.has_language_overrides() {
                    match &mut query.languages {
                        None => {
                            query.languages = Some(
                                overrides
                                    .languages
                                    .iter()
                                    .map(|lang| LocaleId { id: lang.to_owned() })
                                    .collect_vec(),
                            )
                        }
                        Some(_) => (),
                    }
                }
            }
            None => (),
        }
        request
    }

    async fn match_request(
        &self,
        request: fonts::TypefaceRequest,
    ) -> Result<fonts::TypefaceResponse, Error> {
        let query_family = query_field!(request, family);
        let query_family_string =
            (&query_family).map(|family| family.name.clone()).unwrap_or_default();
        trace::duration!(
            "fonts",
            "service:match_request",
            "family" => &query_family_string[..]);
        // TODO(fxbug.dev/44328): If support for lazy trace args is added, include more query params, e.g.
        // code points.

        let matched_family: Option<FontFamilyMatch<'_>> =
            query_family.and_then(|family| self.match_family(&UniCase::new(family.name.clone())));

        let mut request = match &matched_family {
            Some(FontFamilyMatch { family: _, overrides: Some(overrides) }) => {
                Self::apply_query_overrides(request, overrides.clone())
            }
            _ => request,
        };

        let mut typeface = match &matched_family {
            Some(FontFamilyMatch { family, overrides: _ }) => {
                family.faces.match_request(&request)?
            }
            None => None,
        };

        // If an exact match wasn't found, but fallback families are allowed...
        if typeface.is_none() && !request.exact_family() {
            // If fallback_family is not specified by the client explicitly then copy it from
            // the matched font family.
            if query_field!(request, fallback_family).is_none() {
                if let Some(FontFamilyMatch { family, overrides: _ }) = matched_family {
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
            Some(typeface) => self
                .assets
                .get_asset(typeface.asset_id, request.cache_miss_policy())
                .await
                .and_then(|buffer| {
                    Ok(fonts::TypefaceResponse {
                        buffer: Some(buffer),
                        buffer_id: Some(typeface.asset_id.into()),
                        font_index: Some(typeface.font_index),
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
            |FontFamilyMatch { family, overrides: _ }| fonts::FontFamilyInfo {
                name: Some(fonts::FamilyName { name: family.name.clone() }),
                styles: Some(family.faces.get_styles().collect()),
            },
        )
    }

    async fn get_typeface_by_id(
        &self,
        id: AssetId,
        policy: CacheMissPolicy,
    ) -> Result<fonts::TypefaceResponse, fonts_exp::Error> {
        match self.assets.get_asset(id, policy).await {
            Ok(buffer) => {
                let response = fonts::TypefaceResponse {
                    buffer: Some(buffer),
                    buffer_id: Some(id.into()),
                    font_index: None,
                };
                Ok(response)
            }
            Err(e) => {
                let msg = e.to_string();
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
            .map(|matched| matched.family)
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
                        Some(matched) => Box::new(iter::once(matched.family)),
                        None => Box::new(iter::empty()),
                    }
                }
            }
            None => Box::new(self.families.iter().filter_map(move |(_, value)| match value {
                FamilyOrAlias::Family(family) => Some(family),
                FamilyOrAlias::Alias(_, _) => None,
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

        fasync::Task::spawn(
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
        )
        .detach();

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
                Ok(responder.send(response.as_mut())?)
            }
            // TODO(I18N-12): Remove when all clients have migrated to GetFontFamilyInfo
            GetFamilyInfo { family, responder } => {
                let mut font_info =
                    self.get_family_info(fonts::FamilyName { name: family }).into_family_info();
                Ok(responder.send(font_info.as_mut())?)
            }
            GetTypeface { request, responder } => {
                let response = self.match_request(request).await?;
                Ok(responder.send(response)?)
            }
            GetFontFamilyInfo { family, responder } => {
                let family_info = self.get_family_info(family);
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
                let mut response = self
                    .get_typeface_by_id(AssetId(id), CacheMissPolicy::BlockUntilDownloaded)
                    .await;
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
                .context("Error while handling font provider request")
                .map_err(|err| {
                    fx_log_err!("{:?}", err);
                    err
                })?;
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
                .context("Error while handling experimental font provider request")
                .map_err(|err| {
                    fx_log_err!("{:?}", err);
                    err
                })?;
        }
        Ok(())
    }
}
