// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file

use {
    anyhow::{Context as _, Result},
    fidl_fuchsia_fonts as fonts, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
};

#[fasync::run_singlethreaded(test)]
async fn test_font_provider_launch() -> Result<()> {
    let font_provider = connect_to_service::<fonts::ProviderMarker>()
        .context("Failed to connect to font server")?;

    let request = fonts::TypefaceRequest {
        query: Some(fonts::TypefaceQuery {
            family: None,
            style: None,
            languages: None,
            code_points: Some(vec!['0' as u32]),
            fallback_family: None,
        }),
        flags: None,
        cache_miss_policy: None,
    };

    // Note that an _empty_ response is not an error, and will be returned if the product
    // configuration has no fonts.
    font_provider.get_typeface(request).await?;

    Ok(())
}
