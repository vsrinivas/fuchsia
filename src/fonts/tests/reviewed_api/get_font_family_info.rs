// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::util::*;

#[fasync::run_singlethreaded(test)]
async fn test_get_font_family_info() -> Result<(), Error> {
    let (_app, font_provider) = start_provider_with_default_fonts()?;

    let font_family_info = font_provider
        .get_font_family_info(&mut fonts::FamilyName { name: "materialicons".to_string() })
        .await?;

    assert!(!font_family_info.is_empty());

    assert_eq!(font_family_info.name.unwrap().name, "Material Icons");
    assert!(font_family_info.styles.unwrap().len() > 0);

    Ok(())
}
