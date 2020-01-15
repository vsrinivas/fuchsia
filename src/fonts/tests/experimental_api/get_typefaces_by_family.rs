// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file

use super::util::*;

#[fasync::run_singlethreaded(test)]
async fn test_get_typefaces_by_family() -> Result<(), Error> {
    let roboto = roboto_info(1, fonts::WEIGHT_NORMAL);
    let roboto_light = roboto_info(2, 300);
    let roboto_medium = roboto_info(3, 500);

    let (_app, font_provider) = start_provider_with_default_fonts()?;
    let mut family = fonts::FamilyName { name: String::from("Roboto") };

    let response = font_provider.get_typefaces_by_family(&mut family).await?;
    let faces = response.unwrap().results.unwrap();

    assert_eq!(faces.len(), 3);
    assert_eq!(faces[0], roboto);
    assert_eq!(faces[1], roboto_light);
    assert_eq!(faces[2], roboto_medium);
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_get_typefaces_by_family_alias() -> Result<(), Error> {
    let (_app, font_provider) = start_provider_with_default_fonts()?;
    let mut family = fonts::FamilyName { name: String::from("Material Icons") };
    let mut alias = fonts::FamilyName { name: String::from("MaterialIcons") };

    let by_family = font_provider.get_typefaces_by_family(&mut family).await?;
    let by_alias = font_provider.get_typefaces_by_family(&mut alias).await?;

    let by_family_faces = by_family.unwrap().results.unwrap();
    let by_alias_faces = by_alias.unwrap().results.unwrap();

    assert_eq!(by_family_faces.len(), 1);
    assert_eq!(by_alias_faces.len(), 1);
    assert_eq!(by_family_faces[0], by_alias_faces[0]);
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_get_typefaces_by_family_not_found() -> Result<(), Error> {
    let (_app, font_provider) = start_provider_with_default_fonts()?;
    let mut family = fonts::FamilyName { name: String::from("NoSuchFont") };
    let response = font_provider.get_typefaces_by_family(&mut family).await?;
    assert_eq!(response.unwrap_err(), fonts_exp::Error::NotFound);
    Ok(())
}
