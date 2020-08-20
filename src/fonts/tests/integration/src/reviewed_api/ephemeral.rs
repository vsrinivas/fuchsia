// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {super::util::*, crate::FONTS_EPHEMERAL_CM};

// Add new tests here so we don't overload component manager with requests (58150)
#[fasync::run_singlethreaded(test)]
async fn test_ephemeral() {
    test_ephemeral_get_font_family_info().await.unwrap();
    test_ephemeral_get_typeface().await.unwrap();
}

async fn test_ephemeral_get_font_family_info() -> Result<(), Error> {
    let font_provider = get_provider(FONTS_EPHEMERAL_CM).await?;

    let mut family = fonts::FamilyName { name: "Ephemeral".to_string() };

    let response = font_provider.get_font_family_info(&mut family).await?;

    assert_eq!(response.name, Some(family));
    Ok(())
}

async fn test_ephemeral_get_typeface() -> Result<(), Error> {
    let font_provider = get_provider(FONTS_EPHEMERAL_CM).await?;

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
