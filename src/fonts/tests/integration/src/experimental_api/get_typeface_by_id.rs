// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file

use {super::util::*, crate::MANIFEST_TEST_FONTS_SMALL, anyhow::format_err};

#[fasync::run_singlethreaded(test)]
async fn test_get_typeface_by_id() -> Result<(), Error> {
    let (_app, font_provider) = start_provider_with_manifest(MANIFEST_TEST_FONTS_SMALL)?;
    // There will always be a font with index 0 unless manifest loading fails.
    let response =
        font_provider.get_typeface_by_id(0).await?.map_err(|e| format_err!("{:#?}", e))?;
    assert_eq!(response.buffer_id, Some(0));
    assert!(response.buffer.is_some());
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_get_typeface_by_id_not_found() -> Result<(), Error> {
    let (_app, font_provider) = start_provider_with_manifest(MANIFEST_TEST_FONTS_SMALL)?;
    let response = font_provider.get_typeface_by_id(std::u32::MAX).await?;
    assert_eq!(response.unwrap_err(), fonts_exp::Error::NotFound);
    Ok(())
}
