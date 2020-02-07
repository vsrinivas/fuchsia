// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{FONTS_CMX, MANIFEST_TEST_FONTS_MEDIUM, MANIFEST_TEST_FONTS_SMALL},
    anyhow::{format_err, Context as _, Error},
    fidl_fuchsia_fonts as fonts, fuchsia_async as fasync,
    fuchsia_component::client::{launch_with_options, launcher, App, LaunchOptions},
    fuchsia_zircon as zx,
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

pub fn start_provider_with_manifest(
    manifest_file_path: impl AsRef<str>,
) -> Result<(App, fonts::ProviderProxy), Error> {
    let launcher = launcher().context("Failed to open launcher service")?;
    let args = vec!["--font-manifest".to_string(), manifest_file_path.as_ref().to_string()];

    let app =
        launch_with_options(&launcher, FONTS_CMX.to_string(), Some(args), LaunchOptions::new())
            .context("Failed to launch fonts::Provider")?;
    let font_provider = app
        .connect_to_service::<fonts::ProviderMarker>()
        .context("Failed to connect to fonts::Provider")?;

    Ok((app, font_provider))
}

#[fasync::run_singlethreaded(test)]
async fn test_basic() -> Result<(), Error> {
    let (_app, font_provider) = start_provider_with_manifest(MANIFEST_TEST_FONTS_SMALL)?;

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

#[fasync::run_singlethreaded(test)]
async fn test_aliases() -> Result<(), Error> {
    let (_app, font_provider) = start_provider_with_manifest(MANIFEST_TEST_FONTS_SMALL)?;

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

#[fasync::run_singlethreaded(test)]
async fn test_font_collections() -> Result<(), Error> {
    let (_app, font_provider) = start_provider_with_manifest(MANIFEST_TEST_FONTS_MEDIUM)?;

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

#[fasync::run_singlethreaded(test)]
async fn test_fallback() -> Result<(), Error> {
    let (_app, font_provider) = start_provider_with_manifest(MANIFEST_TEST_FONTS_MEDIUM)?;

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
#[fasync::run_singlethreaded(test)]
async fn test_fallback_group() -> Result<(), Error> {
    let (_app, font_provider) = start_provider_with_manifest(MANIFEST_TEST_FONTS_MEDIUM)?;

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

#[fasync::run_singlethreaded(test)]
async fn test_get_family_info() -> Result<(), Error> {
    let (_app, font_provider) = start_provider_with_manifest(MANIFEST_TEST_FONTS_SMALL)?;

    let family_info = font_provider.get_family_info("materialicons").await?;

    assert!(family_info.is_some());
    let family_info = family_info.unwrap();

    assert_eq!(family_info.name, "Material Design Icons");
    assert!(family_info.styles.len() > 0);

    Ok(())
}
