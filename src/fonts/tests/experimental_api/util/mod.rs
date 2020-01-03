// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file

pub use {
    crate::FONTS_CMX,
    anyhow::{Context as _, Error},
    fidl::endpoints::create_proxy,
    fidl_fuchsia_fonts as fonts, fidl_fuchsia_fonts_experimental as fonts_exp,
    fidl_fuchsia_intl::LocaleId,
    fuchsia_async as fasync,
    fuchsia_component::client::{launch, launch_with_options, launcher, App, LaunchOptions},
};

// TODO(kpozin): "Default" fonts will be empty when we begin building fonts per target product.
pub fn start_provider_with_default_fonts() -> Result<(App, fonts_exp::ProviderProxy), Error> {
    let launcher = launcher().context("Failed to open launcher service")?;
    let app = launch(&launcher, FONTS_CMX.to_string(), None)
        .context("Failed to launch fonts_exp::Provider")?;

    let font_provider = app
        .connect_to_service::<fonts_exp::ProviderMarker>()
        .context("Failed to connect to fonts_exp::Provider")?;

    Ok((app, font_provider))
}

pub fn start_provider_with_test_fonts() -> Result<(App, fonts_exp::ProviderProxy), Error> {
    let mut launch_options = LaunchOptions::new();
    launch_options.add_dir_to_namespace(
        "/test_fonts".to_string(),
        std::fs::File::open("/pkg/data/testdata/test_fonts")?,
    )?;

    let launcher = launcher().context("Failed to open launcher service")?;
    let app = launch_with_options(
        &launcher,
        FONTS_CMX.to_string(),
        Some(vec!["--font-manifest".to_string(), "/test_fonts/test_manifest_v1.json".to_string()]),
        launch_options,
    )
    .context("Failed to launch fonts::Provider")?;

    let font_provider = app
        .connect_to_service::<fonts_exp::ProviderMarker>()
        .context("Failed to connect to fonts_exp::Provider")?;

    Ok((app, font_provider))
}

pub fn start_provider_with_all_fonts() -> Result<(App, fonts_exp::ProviderProxy), Error> {
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
            "/test_fonts/all_fonts_manifest_v1.json".to_string(),
        ]),
        launch_options,
    )
    .context("Failed to launch fonts::Provider")?;

    let font_provider = app
        .connect_to_service::<fonts_exp::ProviderMarker>()
        .context("Failed to connect to fonts_exp::Provider")?;

    Ok((app, font_provider))
}

pub fn roboto_info(id: u32, weight: u16) -> fonts_exp::TypefaceInfo {
    fonts_exp::TypefaceInfo {
        asset_id: Some(id),
        font_index: Some(0),
        family: Some(fonts::FamilyName { name: String::from("Roboto") }),
        style: Some(fonts::Style2 {
            slant: Some(fonts::Slant::Upright),
            weight: Some(weight),
            width: Some(fonts::Width::Normal),
        }),
        languages: Some(Vec::new()),
        generic_family: Some(fonts::GenericFontFamily::SansSerif),
    }
}

pub fn empty_list_typefaces_request() -> fonts_exp::ListTypefacesRequest {
    fonts_exp::ListTypefacesRequest {
        flags: None,
        family: None,
        slant: None,
        weight: None,
        width: None,
        languages: None,
        code_points: None,
        generic_family: None,
    }
}

pub fn name_query(name: &str) -> fonts_exp::ListTypefacesRequest {
    fonts_exp::ListTypefacesRequest {
        flags: None,
        family: Some(fonts::FamilyName { name: String::from(name) }),
        slant: None,
        weight: None,
        width: None,
        languages: None,
        code_points: None,
        generic_family: None,
    }
}

pub fn slant_query(lower: fonts::Slant, upper: fonts::Slant) -> fonts_exp::ListTypefacesRequest {
    fonts_exp::ListTypefacesRequest {
        flags: None,
        family: None,
        slant: Some(fonts_exp::SlantRange { lower, upper }),
        weight: None,
        width: None,
        languages: None,
        code_points: None,
        generic_family: None,
    }
}

pub fn weight_query(lower: u16, upper: u16) -> fonts_exp::ListTypefacesRequest {
    fonts_exp::ListTypefacesRequest {
        flags: None,
        family: None,
        slant: None,
        weight: Some(fonts_exp::WeightRange { lower, upper }),
        width: None,
        languages: None,
        code_points: None,
        generic_family: None,
    }
}

pub fn width_query(lower: fonts::Width, upper: fonts::Width) -> fonts_exp::ListTypefacesRequest {
    fonts_exp::ListTypefacesRequest {
        flags: None,
        family: None,
        slant: None,
        weight: None,
        width: Some(fonts_exp::WidthRange { lower, upper }),
        languages: None,
        code_points: None,
        generic_family: None,
    }
}

pub fn locale(lang: &str) -> LocaleId {
    LocaleId { id: String::from(lang) }
}

pub fn lang_query(langs: Vec<LocaleId>) -> fonts_exp::ListTypefacesRequest {
    fonts_exp::ListTypefacesRequest {
        flags: None,
        family: None,
        slant: None,
        weight: None,
        width: None,
        languages: Some(langs),
        code_points: None,
        generic_family: None,
    }
}

pub fn code_point_query(points: Vec<u32>) -> fonts_exp::ListTypefacesRequest {
    fonts_exp::ListTypefacesRequest {
        flags: None,
        family: None,
        slant: None,
        weight: None,
        width: None,
        languages: None,
        code_points: Some(points),
        generic_family: None,
    }
}

pub fn generic_family_query(
    generic_family: fonts::GenericFontFamily,
) -> fonts_exp::ListTypefacesRequest {
    fonts_exp::ListTypefacesRequest {
        flags: None,
        family: None,
        slant: None,
        weight: None,
        width: None,
        languages: None,
        code_points: None,
        generic_family: Some(generic_family),
    }
}
