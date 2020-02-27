// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub use {
    crate::{FONTS_ALIASED_CM},
    anyhow::{Context as _, Error},
    fidl_fuchsia_fonts as fonts,
    fidl_fuchsia_fonts_ext::DecodableExt,
    fidl_fuchsia_intl as intl, fuchsia_async as fasync,
    fuchsia_component::client::{create_scoped_dynamic_instance, ScopedInstance},
    fuchsia_zircon as zx,
    fuchsia_zircon::AsHandleRef,
};

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

// TODO: Instead of configuring fonts through a different manifest and command-line arguments,
// offer a service or directory with the right fonts to the new component instance. This will
// require support to dynamically offer a capability to a component.
pub async fn start_provider(
    fonts_cm: &str,
) -> Result<(ScopedInstance, fonts::ProviderProxy), Error> {
    let app = create_scoped_dynamic_instance("coll".to_string(), fonts_cm.to_string())
        .await
        .context("Failed to create dynamic component")?;
    let font_provider = app
        .connect_to_protocol_at_exposed_dir::<fonts::ProviderMarker>()
        .context("Failed to connect to fonts::Provider")?;
    Ok((app, font_provider))
}

#[derive(Debug, Eq, PartialEq)]
pub struct TypefaceInfo {
    pub vmo_koid: zx::Koid,
    pub buffer_id: u32,
    pub size: u64,
    pub index: u32,
}

pub async fn get_typeface_info(
    font_provider: &fonts::ProviderProxy,
    name: Option<String>,
    style: Option<fonts::Style2>,
    languages: Option<Vec<String>>,
    code_points: Option<Vec<char>>,
) -> Result<TypefaceInfo, Error> {
    let typeface = font_provider
        .get_typeface(fonts::TypefaceRequest {
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
            }),
            flags: Some(fonts::TypefaceRequestFlags::empty()),
            cache_miss_policy: None,
        })
        .await?;

    assert!(!typeface.is_empty(), "Received empty response for {:?}", name);
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

pub async fn get_typeface_info_basic(
    font_provider: &fonts::ProviderProxy,
    name: Option<String>,
) -> Result<TypefaceInfo, Error> {
    get_typeface_info(font_provider, name, None, None, None).await
}
