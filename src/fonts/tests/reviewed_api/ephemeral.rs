// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::util::*;

fn start_provider_with_ephemeral_fonts() -> Result<(App, fonts::ProviderProxy), Error> {
    let mut launch_options = LaunchOptions::new();
    launch_options.add_dir_to_namespace(
        "/test_fonts".to_string(),
        std::fs::File::open("/pkg/data/testdata/test_fonts")?,
    )?;

    let launcher = launcher().context("Failed to open launcher service")?;
    let app = launch_with_options(
        &launcher,
        FONTS_CMX.to_string(),
        Some(vec![
            "--no-default-fonts".to_string(),
            "--font-manifest".to_string(),
            "/test_fonts/ephemeral_manifest.json".to_string(),
        ]),
        launch_options,
    )
    .context("Failed to launch fonts::Provider")?;
    let font_provider = app
        .connect_to_service::<fonts::ProviderMarker>()
        .context("Failed to connect to fonts::Provider")?;

    Ok((app, font_provider))
}

#[fasync::run_singlethreaded(test)]
async fn test_ephemeral_get_font_family_info() -> Result<(), Error> {
    let (_app, font_provider) = start_provider_with_ephemeral_fonts()?;

    let mut family = fonts::FamilyName { name: "Ephemeral".to_string() };

    let response = font_provider.get_font_family_info(&mut family).await?;

    assert_eq!(response.name, Some(family));
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_ephemeral_get_typeface() -> Result<(), Error> {
    let (_app, font_provider) = start_provider_with_ephemeral_fonts()?;

    let family = Some(fonts::FamilyName { name: "Ephemeral".to_string() });
    let query = Some(fonts::TypefaceQuery {
        family,
        style: None,
        code_points: None,
        languages: None,
        fallback_family: None,
    });
    let request = fonts::TypefaceRequest { query, flags: None, cache_miss_policy: None };

    let response = font_provider.get_typeface(request).await?;

    assert!(response.buffer.is_some(), "{:?}", response);
    assert_eq!(response.buffer_id.unwrap(), 0, "{:?}", response);
    assert_eq!(response.font_index.unwrap(), 0, "{:?}", response);
    Ok(())
}
