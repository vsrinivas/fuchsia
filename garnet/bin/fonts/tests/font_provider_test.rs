// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro, futures_api)]
// This is only needed because GN's invocation of the Rust compiler doesn't recognize the test_
// methods as entry points, so it complains about the helper methods being "dead code".
#![cfg(test)]

use failure::{format_err, Error, ResultExt};
use fidl_fuchsia_fonts as fonts;
use fuchsia_async as fasync;
use fuchsia_component::client::{launch, launch_with_options, launcher, App, LaunchOptions};
use fuchsia_zircon as zx;
use fuchsia_zircon::AsHandleRef;

#[derive(Debug, Eq, PartialEq)]
struct FontInfo {
    vmo_koid: zx::Koid,
    buffer_id: u32,
    size: u64,
    index: u32,
}

async fn get_font_info(
    font_provider: &fonts::ProviderProxy, name: Option<String>, language: Option<Vec<String>>,
    character: char,
) -> Result<FontInfo, Error> {
    let font = await!(font_provider.get_font(&mut fonts::Request {
        family: name.clone(),
        weight: 400,
        width: 5,
        slant: fonts::Slant::Upright,
        character: character as u32,
        language: language,
        fallback_group: fonts::FallbackGroup::None,
        flags: 0,
    }))?;
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
    font_provider: &fonts::ProviderProxy, name: Option<String>,
) -> Result<FontInfo, Error> {
    await!(get_font_info(font_provider, name, None, '\0'))
}

fn start_provider_with_default_fonts() -> Result<(App, fonts::ProviderProxy), Error> {
    let launcher = launcher().context("Failed to open launcher service")?;
    let app = launch(&launcher, "fuchsia-pkg://fuchsia.com/fonts#meta/fonts.cmx".to_string(), None)
        .context("Failed to launch fonts::Provider")?;

    let font_provider = app
        .connect_to_service::<fonts::ProviderMarker>()
        .context("Failed to connect to fonts::Provider")?;

    Ok((app, font_provider))
}

#[fasync::run_singlethreaded]
#[test]
async fn test_basic() -> Result<(), Error> {
    let (_app, font_provider) = start_provider_with_default_fonts()?;

    let default =
        await!(get_font_info_basic(&font_provider, None)).context("Failed to load default font")?;
    let roboto = await!(get_font_info_basic(
        &font_provider,
        Some("Roboto".to_string())
    ))
    .context("Failed to load Roboto")?;
    let material_icons = await!(get_font_info_basic(
        &font_provider,
        Some("Material Icons".to_string())
    ))
    .context("Failed to load Material Icons")?;

    // Roboto should be returned by default.
    assert!(default == roboto);

    // Material Icons request should return a different font.
    assert!(default.vmo_koid != material_icons.vmo_koid);
    assert!(default.buffer_id != material_icons.buffer_id);

    Ok(())
}

#[fasync::run_singlethreaded]
#[test]
async fn test_aliases() -> Result<(), Error> {
    let (_app, font_provider) = start_provider_with_default_fonts()?;

    // Both requests should return the same font.
    let materialicons = await!(get_font_info_basic(
        &font_provider,
        Some("MaterialIcons".to_string())
    ))
    .context("Failed to load MaterialIcons")?;
    let material_icons = await!(get_font_info_basic(
        &font_provider,
        Some("Material Icons".to_string())
    ))
    .context("Failed to load Material Icons")?;
    assert!(materialicons == material_icons);

    Ok(())
}

fn start_provider_with_test_fonts() -> Result<(App, fonts::ProviderProxy), Error> {
    let mut launch_options = LaunchOptions::new();
    launch_options.add_dir_to_namespace(
        "/test_fonts".to_string(),
        std::fs::File::open("/pkg/data/testdata/test_fonts")?,
    )?;

    let launcher = launcher().context("Failed to open launcher service")?;
    let app = launch_with_options(
        &launcher,
        "fuchsia-pkg://fuchsia.com/fonts#meta/fonts.cmx".to_string(),
        Some(vec!["--font-manifest".to_string(), "/test_fonts/manifest.json".to_string()]),
        launch_options,
    )
    .context("Failed to launch fonts::Provider")?;

    let font_provider = app
        .connect_to_service::<fonts::ProviderMarker>()
        .context("Failed to connect to fonts::Provider")?;

    Ok((app, font_provider))
}

#[fasync::run_singlethreaded]
#[test]
async fn test_font_collections() -> Result<(), Error> {
    let (_app, font_provider) = start_provider_with_test_fonts()?;

    // Request Japanese and Simplified Chinese versions of Noto Sans CJK. Both
    // fonts are part of the same TTC file, so font provider is expected to
    // return the same buffer with different font index values.
    let noto_sans_cjk_ja = await!(get_font_info(
        &font_provider,
        Some("NotoSansCJK".to_string()),
        Some(vec!["ja".to_string()]),
        '\0'
    ))
    .context("Failed to load NotoSansCJK font")?;
    let noto_sans_cjk_sc = await!(get_font_info(
        &font_provider,
        Some("NotoSansCJK".to_string()),
        Some(vec!["zh-Hans".to_string()]),
        '\0'
    ))
    .context("Failed to load NotoSansCJK font")?;

    assert!(noto_sans_cjk_ja.vmo_koid == noto_sans_cjk_sc.vmo_koid);
    assert!(noto_sans_cjk_ja.index != noto_sans_cjk_sc.index);

    Ok(())
}

#[fasync::run_singlethreaded]
#[test]
async fn test_fallback() -> Result<(), Error> {
    let (_app, font_provider) = start_provider_with_test_fonts()?;

    let noto_sans_cjk_ja = await!(get_font_info(
        &font_provider,
        Some("NotoSansCJK".to_string()),
        Some(vec!["ja".to_string()]),
        '\0'
    ))
    .context("Failed to load NotoSansCJK font")?;

    let noto_sans_cjk_ja_by_char = await!(get_font_info(
        &font_provider,
        Some("Roboto".to_string()),
        Some(vec!["ja".to_string()]),
        'な'
    ))
    .context("Failed to load NotoSansCJK font")?;

    // Same font should be returned in both cases.
    assert!(noto_sans_cjk_ja == noto_sans_cjk_ja_by_char);

    Ok(())
}

// Verify that the fallback group of the requested font is taken into account for fallback.
#[fasync::run_singlethreaded]
#[test]
async fn test_fallback_group() -> Result<(), Error> {
    let (_app, font_provider) = start_provider_with_test_fonts()?;

    let noto_serif_cjk_ja = await!(get_font_info(
        &font_provider,
        Some("Noto Serif CJK".to_string()),
        Some(vec!["ja".to_string()]),
        '\0'
    ))
    .context("Failed to load Noto Serif CJK font")?;

    let noto_serif_cjk_ja_by_char = await!(get_font_info(
        &font_provider,
        Some("Roboto Slab".to_string()),
        Some(vec!["ja".to_string()]),
        'な'
    ))
    .context("Failed to load Noto Serif CJK font")?;

    // The query above requested Roboto Slab, so it's expected to return
    // Noto Serif CJK instead of Noto Sans CJK because Roboto Slab and
    // Noto Serif CJK are both in serif fallback group.
    assert!(noto_serif_cjk_ja == noto_serif_cjk_ja_by_char);

    Ok(())
}

#[fasync::run_singlethreaded]
#[test]
async fn test_get_family_info() -> Result<(), Error> {
    let (_app, font_provider) = start_provider_with_default_fonts()?;

    let family_info = await!(font_provider.get_family_info("materialicons"))?;

    assert!(family_info.is_some());
    let family_info = family_info.unwrap();

    assert!(family_info.name == "Material Icons");
    assert!(family_info.styles.len() > 0);

    Ok(())
}
