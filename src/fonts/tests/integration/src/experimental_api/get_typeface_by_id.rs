// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file

use {super::util::*, crate::FONTS_SMALL_CM, anyhow::format_err};

// Add new tests here so we don't overload component manager with requests (58150)
#[fasync::run_singlethreaded(test)]
async fn test_get_typeface_by_id() {
    test_get_typeface_by_id_basic().await.unwrap();
    test_get_typeface_by_id_not_found().await.unwrap();
}

async fn test_get_typeface_by_id_basic() -> Result<(), Error> {
    let font_provider = get_provider(FONTS_SMALL_CM).await?;
    // There will always be a font with index 0 unless manifest loading fails.
    let response =
        font_provider.get_typeface_by_id(0).await?.map_err(|e| format_err!("{:#?}", e))?;
    assert_eq!(response.buffer_id, Some(0));
    assert!(response.buffer.is_some());
    Ok(())
}

async fn test_get_typeface_by_id_not_found() -> Result<(), Error> {
    let font_provider = get_provider(FONTS_SMALL_CM).await?;
    let response = font_provider.get_typeface_by_id(std::u32::MAX).await?;
    assert_eq!(response.unwrap_err(), fonts_exp::Error::NotFound);
    Ok(())
}
