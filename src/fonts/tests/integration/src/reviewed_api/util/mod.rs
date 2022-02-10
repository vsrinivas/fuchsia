// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub use {
    crate::util,
    crate::FONTS_ALIASED_CM,
    anyhow::{Context as _, Error},
    fidl_fuchsia_fonts as fonts,
    fidl_fuchsia_fonts_ext::DecodableExt,
    fidl_fuchsia_intl as intl, fuchsia_async as fasync,
    fuchsia_component_test::ScopedInstance,
    fuchsia_zircon as zx,
    fuchsia_zircon::AsHandleRef,
};

pub type ProviderFactory = util::TypedProviderFactory<fonts::ProviderMarker>;

#[macro_export]
macro_rules! assert_buf_eq {
    ($typeface_info_a:ident, $typeface_info_b:ident) => {
        assert!(
            $typeface_info_a.buffer_id == $typeface_info_b.buffer_id,
            "{}.buffer_id == {}.buffer_id\n{0}: {:?}\n{1}: {:?}",
            stringify!($typeface_info_a),
            stringify!($typeface_info_b),
            $typeface_info_a,
            $typeface_info_b
        )
    };
}

#[derive(Debug, Eq, PartialEq)]
pub struct TypefaceInfo {
    pub vmo_koid: zx::Koid,
    pub buffer_id: u32,
    pub size: u64,
    pub index: u32,
}

pub async fn get_typeface_info_detailed(
    font_provider: &fonts::ProviderProxy,
    request: fonts::TypefaceRequest,
) -> Result<TypefaceInfo, Error> {
    let typeface = font_provider.get_typeface(request.clone()).await?;

    assert!(!typeface.is_empty(), "Received empty response for {:?}", request);
    let buffer = typeface.buffer.unwrap();
    assert!(buffer.size > 0);
    assert!(buffer.size <= buffer.vmo.get_size()?);

    let vmo_koid = buffer.vmo.as_handle_ref().get_koid()?;
    Ok(TypefaceInfo {
        vmo_koid,
        buffer_id: typeface.buffer_id.unwrap(),
        size: buffer.size,
        index: typeface.font_index.unwrap(),
    })
}

pub async fn get_typeface_info(
    font_provider: &fonts::ProviderProxy,
    name: Option<String>,
    style: Option<fonts::Style2>,
    languages: Option<Vec<String>>,
    code_points: Option<Vec<char>>,
) -> Result<TypefaceInfo, Error> {
    let typeface = get_typeface_info_detailed(
        font_provider,
        fonts::TypefaceRequest {
            query: Some(fonts::TypefaceQuery {
                family: name.as_ref().map(|name| fonts::FamilyName { name: name.to_string() }),
                style,
                code_points: code_points
                    .map(|code_points| code_points.into_iter().map(|ch| ch as u32).collect()),
                languages: languages.map(|languages| {
                    languages
                        .into_iter()
                        .map(|lang_code| intl::LocaleId { id: lang_code })
                        .collect()
                }),
                fallback_family: None,
                ..fonts::TypefaceQuery::EMPTY
            }),
            flags: Some(fonts::TypefaceRequestFlags::empty()),
            cache_miss_policy: None,
            ..fonts::TypefaceRequest::EMPTY
        },
    )
    .await?;
    Ok(typeface)
}

pub async fn get_typeface_info_basic(
    font_provider: &fonts::ProviderProxy,
    name: Option<String>,
) -> Result<TypefaceInfo, Error> {
    get_typeface_info(font_provider, name, None, None, None).await
}
