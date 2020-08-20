// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::util,
    crate::{FONTS_MEDIUM_CM, FONTS_SMALL_CM},
    anyhow::{format_err, Context as _, Error},
    fidl_fuchsia_fonts as fonts, fuchsia_async as fasync, fuchsia_zircon as zx,
    fuchsia_zircon::AsHandleRef,
};

macro_rules! assert_buf_eq {
    ($font_info_a:ident, $font_info_b:ident) => {
        assert!(
            $font_info_a.buffer_id == $font_info_b.buffer_id,
            "{}.buffer_id == {}.buffer_id\n{0}: {:?}\n{1}: {:?}",
            stringify!($font_info_a),
            stringify!($font_info_b),
            $font_info_a,
            $font_info_b
        )
    };
}

#[derive(Debug, Eq, PartialEq)]
struct FontInfo {
    vmo_koid: zx::Koid,
    buffer_id: u32,
    size: u64,
    index: u32,
}

async fn get_font_info(
    font_provider: &fonts::ProviderProxy,
    name: Option<String>,
    language: Option<Vec<String>>,
    character: char,
) -> Result<FontInfo, Error> {
    let font = font_provider
        .get_font(&mut fonts::Request {
            family: name.clone(),
            weight: 400,
            width: 5,
            slant: fonts::Slant::Upright,
            character: character as u32,
            language,
            fallback_group: fonts::FallbackGroup::None,
            flags: 0,
        })
        .await?;
    let font = *font.ok_or_else(|| format_err!("Received empty response for {:?}", name))?;

    assert!(font.buffer.size > 0);
    assert!(font.buffer.size <= font.buffer.vmo.get_size()?);

    let vmo_koid = font.buffer.vmo.as_handle_ref().get_koid()?;
    Ok(FontInfo {
        vmo_koid,
        buffer_id: font.buffer_id,
        size: font.buffer.size,
        index: font.font_index,
    })
}

async fn get_font_info_basic(
    font_provider: &fonts::ProviderProxy,
    name: Option<String>,
) -> Result<FontInfo, Error> {
    get_font_info(font_provider, name, None, '\0').await
}

async fn get_provider(fonts_cm: &'static str) -> Result<fonts::ProviderProxy, Error> {
    util::get_provider::<fonts::ProviderMarker>(fonts_cm).await
}

// Add new tests here so we don't overload component manager with requests (58150)
#[fasync::run_singlethreaded(test)]
async fn test_old_api() {
    test_basic().await.unwrap();
    test_aliases().await.unwrap();
    test_font_collections().await.unwrap();
    test_fallback().await.unwrap();
    test_fallback_group().await.unwrap();
    test_get_family_info().await.unwrap();
}

async fn test_basic() -> Result<(), Error> {
    let font_provider = get_provider(FONTS_SMALL_CM).await?;

    let default =
        get_font_info_basic(&font_provider, None).await.context("Failed to load default font")?;
    let roboto = get_font_info_basic(&font_provider, Some("Roboto".to_string()))
        .await
        .context("Failed to load Roboto")?;
    let material_icons = get_font_info_basic(&font_provider, Some("Material Icons".to_string()))
        .await
        .context("Failed to load Material Icons")?;

    // Roboto should be returned by default.
    assert_buf_eq!(default, roboto);

    // Material Icons request should return a different font.
    assert_ne!(default.buffer_id, material_icons.buffer_id);

    Ok(())
}

async fn test_aliases() -> Result<(), Error> {
    let font_provider = get_provider(FONTS_SMALL_CM).await?;

    // Both requests should return the same font.
    let materialicons = get_font_info_basic(&font_provider, Some("MaterialIcons".to_string()))
        .await
        .context("Failed to load MaterialIcons")?;
    let material_icons =
        get_font_info_basic(&font_provider, Some("Material Design Icons".to_string()))
            .await
            .context("Failed to load Material Icons")?;

    assert_buf_eq!(materialicons, material_icons);

    Ok(())
}

async fn test_font_collections() -> Result<(), Error> {
    let font_provider = get_provider(FONTS_MEDIUM_CM).await?;

    // Request Japanese and Simplified Chinese versions of Noto Sans CJK. Both
    // fonts are part of the same TTC file, so font provider is expected to
    // return the same buffer with different font index values.
    let noto_sans_cjk_ja = get_font_info(
        &font_provider,
        Some("NotoSansCJK".to_string()),
        Some(vec!["ja".to_string()]),
        '\0',
    )
    .await
    .context("Failed to load NotoSansCJK font")?;
    let noto_sans_cjk_sc = get_font_info(
        &font_provider,
        Some("NotoSansCJK".to_string()),
        Some(vec!["zh-Hans".to_string()]),
        '\0',
    )
    .await
    .context("Failed to load NotoSansCJK font")?;

    assert_buf_eq!(noto_sans_cjk_ja, noto_sans_cjk_sc);

    assert!(
        noto_sans_cjk_ja.index != noto_sans_cjk_sc.index,
        "noto_sans_cjk_ja.index != noto_sans_cjk_sc.index\n \
         noto_sans_cjk_ja.index: {:?}\n \
         noto_sans_cjk_sc.index: {:?}",
        noto_sans_cjk_ja,
        noto_sans_cjk_sc
    );

    Ok(())
}

async fn test_fallback() -> Result<(), Error> {
    let font_provider = get_provider(FONTS_MEDIUM_CM).await?;

    let noto_sans_cjk_ja = get_font_info(
        &font_provider,
        Some("NotoSansCJK".to_string()),
        Some(vec!["ja".to_string()]),
        '\0',
    )
    .await
    .context("Failed to load NotoSansCJK font")?;

    let noto_sans_cjk_ja_by_char = get_font_info(
        &font_provider,
        Some("Roboto".to_string()),
        Some(vec!["ja".to_string()]),
        'な',
    )
    .await
    .context("Failed to load NotoSansCJK font")?;

    // Same font should be returned in both cases.
    assert_buf_eq!(noto_sans_cjk_ja, noto_sans_cjk_ja_by_char);

    Ok(())
}

// Verify that the fallback group of the requested font is taken into account for fallback.
async fn test_fallback_group() -> Result<(), Error> {
    let font_provider = get_provider(FONTS_MEDIUM_CM).await?;

    let noto_serif_cjk_ja = get_font_info(
        &font_provider,
        Some("Noto Serif CJK".to_string()),
        Some(vec!["ja".to_string()]),
        '\0',
    )
    .await
    .context("Failed to load Noto Serif CJK font")?;

    let noto_serif_cjk_ja_by_char = get_font_info(
        &font_provider,
        Some("Roboto Slab".to_string()),
        Some(vec!["ja".to_string()]),
        'な',
    )
    .await
    .context("Failed to load Noto Serif CJK font")?;

    // The query above requested Roboto Slab, so it's expected to return
    // Noto Serif CJK instead of Noto Sans CJK because Roboto Slab and
    // Noto Serif CJK are both in serif fallback group.
    assert_buf_eq!(noto_serif_cjk_ja, noto_serif_cjk_ja_by_char);

    Ok(())
}

async fn test_get_family_info() -> Result<(), Error> {
    let font_provider = get_provider(FONTS_SMALL_CM).await?;

    let family_info = font_provider.get_family_info("materialicons").await?;

    assert!(family_info.is_some());
    let family_info = family_info.unwrap();

    assert_eq!(family_info.name, "Material Design Icons");
    assert!(family_info.styles.len() > 0);

    Ok(())
}
