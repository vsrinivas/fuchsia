// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::util::*,
    crate::{FONTS_ALIASED_CM, FONTS_SMALL_CM},
    futures::future::join_all,
    itertools::Itertools,
};

// Add new tests here so we don't overload component manager with requests (58150)
#[fasync::run_singlethreaded(test)]
async fn test_get_font_family_info() {
    test_get_font_family_info_basic().await.unwrap();
    test_get_font_family_info_aliases().await.unwrap();
}

async fn test_get_font_family_info_basic() -> Result<(), Error> {
    let font_provider = get_provider(FONTS_SMALL_CM).await?;

    let font_family_info = font_provider
        .get_font_family_info(&mut fonts::FamilyName { name: "materialicons".to_string() })
        .await?;

    assert!(!font_family_info.is_empty());

    assert_eq!(font_family_info.name.unwrap().name, "Material Design Icons");
    assert!(font_family_info.styles.unwrap().len() > 0);

    Ok(())
}

async fn test_get_font_family_info_aliases() -> Result<(), Error> {
    let font_provider = get_provider(FONTS_ALIASED_CM).await?;

    let known_aliases =
        vec!["AlphaSans", "alpha sans", "Alpha Sans Condensed", "Alpha Sans Hebrew"];

    let font_family_infos: Result<Vec<_>, _> = join_all(
        known_aliases
            .into_iter()
            .map(|alias| {
                font_provider
                    .get_font_family_info(&mut fonts::FamilyName { name: alias.to_string() })
            })
            .collect_vec(),
    )
    .await
    .into_iter()
    .collect();

    for font_family_info in font_family_infos? {
        assert!(!font_family_info.is_empty());
        assert_eq!(font_family_info.name.unwrap().name, "Alpha Sans");
        assert!(font_family_info.styles.unwrap().len() > 0);
    }

    Ok(())
}
